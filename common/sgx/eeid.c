// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openenclave/bits/attestation.h>
#include <openenclave/bits/eeid.h>
#include <openenclave/bits/report.h>
#include <openenclave/bits/sgx/sgxtypes.h>
#include <openenclave/internal/constants_x64.h>
#include <openenclave/internal/crypto/sha.h>
#include <openenclave/internal/eeid.h>
#include <openenclave/internal/hexdump.h>
#include <openenclave/internal/raise.h>
#include <openenclave/internal/safecrt.h>
#include <openenclave/internal/types.h>

#include "sgxmeasure.h"

#ifdef OE_BUILD_ENCLAVE
#include <openenclave/enclave.h>
#include "../../enclave/crypto/mbedtls/key.h"
#include "../../enclave/crypto/mbedtls/rsa.h"
#else
#include <openenclave/host.h>
#ifdef _WIN32
#include "../../host/crypto/bcrypt/bcrypt.h"
#include "../../host/crypto/bcrypt/key.h"
#include "../../host/crypto/bcrypt/rsa.h"
#else
#include <openssl/opensslv.h>
#include <openssl/rsa.h>
#include "../crypto/openssl/key.h"
#include "../crypto/openssl/rsa.h"
#endif
#endif

int is_eeid_base_image(const oe_sgx_enclave_properties_t* properties)
{
    return properties->header.size_settings.num_heap_pages == 0 &&
           properties->header.size_settings.num_stack_pages == 0 &&
           properties->header.size_settings.num_tcs == 1;
}

oe_result_t oe_create_eeid_sgx(size_t data_size, oe_eeid_t** eeid)
{
    oe_result_t result = OE_UNEXPECTED;
    size_t signature_size = sizeof(sgx_sigstruct_t);

    if (!eeid)
        OE_RAISE(OE_INVALID_PARAMETER);

    *eeid = calloc(sizeof(oe_eeid_t) + data_size + signature_size, 1);
    if (!*eeid)
        OE_RAISE(OE_OUT_OF_MEMORY);
    (*eeid)->signature_size = signature_size;
    (*eeid)->data_size = data_size;

    result = OE_OK;
done:
    return result;
}

static oe_result_t _measure_page(
    oe_sha256_context_t* hctx,
    uint64_t base,
    void* page,
    uint64_t* vaddr,
    bool extend,
    bool readonly)
{
    oe_result_t result = OE_UNEXPECTED;

    uint64_t flags = SGX_SECINFO_REG | SGX_SECINFO_R;

    if (!readonly)
        flags |= SGX_SECINFO_W;

    OE_CHECK(oe_sgx_measure_load_enclave_data(
        hctx, base, base + *vaddr, (uint64_t)page, flags, extend));
    *vaddr += OE_PAGE_SIZE;
    result = OE_OK;

done:
    return result;
}

oe_result_t oe_remeasure_memory_pages(
    const oe_eeid_t* eeid,
    struct _OE_SHA256* computed_enclave_hash,
    bool with_eeid_pages)
{
    oe_result_t result;
    oe_sha256_context_t hctx;
    OE_CHECK(oe_sha256_restore(&hctx, eeid->hash_state.H, eeid->hash_state.N));

    uint64_t base = 0x0ab0c0d0e0f;
    oe_page_t blank_pg, stack_pg, tcs_pg;
    OE_CHECK(oe_memset_s(&blank_pg, sizeof(blank_pg), 0, sizeof(blank_pg)));
    OE_CHECK(oe_memset_s(&stack_pg, sizeof(stack_pg), 0xcc, sizeof(stack_pg)));

    uint64_t vaddr = eeid->vaddr;

    // This is where we replay the addition of memory pages, both, for
    // verification of the extended image hash (with_eeid_pages=true) and
    // the base image hash, for which there are no EEID pages, but one TCS
    // page.

    for (size_t i = 0; i < eeid->size_settings.num_heap_pages; i++)
        OE_CHECK(_measure_page(&hctx, base, &blank_pg, &vaddr, false, false));

    for (size_t i = 0; i < eeid->size_settings.num_tcs; i++)
    {
        vaddr += OE_PAGE_SIZE; /* guard page */

        for (size_t i = 0; i < eeid->size_settings.num_stack_pages; i++)
            OE_CHECK(
                _measure_page(&hctx, base, &stack_pg, &vaddr, true, false));

        vaddr += OE_PAGE_SIZE; /* guard page */

        sgx_tcs_t* tcs;
        OE_CHECK(oe_memset_s(&tcs_pg, sizeof(tcs_pg), 0, sizeof(tcs_pg)));
        tcs = (sgx_tcs_t*)&tcs_pg;
        tcs->flags = 0;
        tcs->ossa = vaddr + OE_PAGE_SIZE;
        tcs->cssa = 0;
        tcs->nssa = 2;
        tcs->oentry = eeid->entry_point;
        tcs->fsbase =
            vaddr +
            (eeid->tls_page_count + OE_SGX_TCS_CONTROL_PAGES) * OE_PAGE_SIZE;
        tcs->gsbase = tcs->fsbase;
        tcs->fslimit = 0xFFFFFFFF;
        tcs->gslimit = 0xFFFFFFFF;

        OE_CHECK(oe_sgx_measure_load_enclave_data(
            &hctx,
            base,
            base + vaddr,
            (uint64_t)&tcs_pg,
            SGX_SECINFO_TCS,
            true));

        vaddr += OE_PAGE_SIZE;

        for (size_t i = 0; i < 2; i++)
            _measure_page(&hctx, base, &blank_pg, &vaddr, true, false);

        vaddr += OE_PAGE_SIZE; /* guard page */

        for (size_t i = 0; i < 2; i++)
            _measure_page(&hctx, base, &blank_pg, &vaddr, true, false);
    }

    if (with_eeid_pages)
    {
        char* eeid_bytes = (char*)eeid;
        size_t num_bytes = oe_eeid_byte_size(eeid);
        size_t num_pages =
            num_bytes / OE_PAGE_SIZE + ((num_bytes % OE_PAGE_SIZE) ? 1 : 0);

        oe_page_t page;
        for (size_t i = 0; i < num_pages; i++)
        {
            OE_CHECK(
                oe_memset_s(page.data, OE_PAGE_SIZE, 0, sizeof(oe_page_t)));
            size_t n = (i != num_pages - 1) ? OE_PAGE_SIZE
                                            : (num_bytes % OE_PAGE_SIZE);
            OE_CHECK(oe_memcpy_s(
                page.data, OE_PAGE_SIZE, eeid_bytes + OE_PAGE_SIZE * i, n));
            OE_CHECK(_measure_page(&hctx, base, page.data, &vaddr, true, true));
        }
    }

    oe_sha256_final(&hctx, computed_enclave_hash);

done:
    return OE_OK;
}

static bool is_zero(const uint8_t* buf, size_t sz)
{
    while (sz != 0)
        if (buf[--sz] != 0)
            return false;
    return true;
}

static oe_result_t _verify_signature(
    const OE_SHA256* hash,
    const uint8_t* modulus,
    const uint8_t* exponent,
    const uint8_t* signature)
{
    oe_result_t result = OE_UNEXPECTED;
    oe_rsa_public_key_t public_key;

    OE_CHECK(oe_rsa_public_key_from_modulus(
        modulus, OE_KEY_SIZE, exponent, OE_EXPONENT_SIZE, &public_key));

    OE_CHECK(oe_rsa_public_key_verify(
        &public_key,
        OE_HASH_TYPE_SHA256,
        hash->buf,
        sizeof(hash->buf),
        signature,
        OE_KEY_SIZE));

    result = OE_OK;

done:
#ifdef OE_BUILD_ENCLAVE
    // The mbedTLS flavour of oe_rsa_public_key_init copies the key,
    // so we need both, an oe_rsa_public_key_free and an mbedtls_pk_free.
    {
        oe_public_key_t* pk = (oe_public_key_t*)&public_key;
        mbedtls_pk_free(&pk->pk);
    }
#endif

    if (result == OE_OK)
        result = oe_rsa_public_key_free(&public_key);

    return result;
}

static oe_result_t _verify_base_image_signature(
    const sgx_sigstruct_t* sigstruct)
{
    oe_result_t result = OE_UNEXPECTED;
    unsigned char buf[sizeof(sgx_sigstruct_t)];
    size_t n = 0;

    OE_CHECK(oe_memcpy_s(
        buf,
        sizeof(buf),
        sgx_sigstruct_header(sigstruct),
        sgx_sigstruct_header_size()));
    n += sgx_sigstruct_header_size();
    OE_CHECK(oe_memcpy_s(
        &buf[n],
        sizeof(buf) - n,
        sgx_sigstruct_body(sigstruct),
        sgx_sigstruct_body_size()));
    n += sgx_sigstruct_body_size();

    OE_SHA256 hash;
    oe_sha256_context_t context;

    oe_sha256_init(&context);
    oe_sha256_update(&context, buf, n);
    oe_sha256_final(&context, &hash);

    uint8_t modulus[OE_KEY_SIZE];
    for (size_t i = 0; i < OE_KEY_SIZE; i++)
        modulus[i] = sigstruct->modulus[OE_KEY_SIZE - 1 - i];

    uint8_t exponent[OE_KEY_SIZE];
    for (size_t i = 0; i < OE_EXPONENT_SIZE; i++)
        exponent[i] = sigstruct->exponent[OE_EXPONENT_SIZE - 1 - i];

    uint8_t signature[OE_KEY_SIZE];
    for (size_t i = 0; i < OE_KEY_SIZE; i++)
        signature[i] = sigstruct->signature[OE_KEY_SIZE - 1 - i];

    OE_CHECK(_verify_signature(&hash, modulus, exponent, signature));

    result = OE_OK;

done:
    return result;
}

oe_result_t verify_eeid(
    const oe_eeid_relevant_base_claims_t* relevant_claims,
    const uint8_t** base_enclave_hash,
    const oe_eeid_t* eeid)
{
    oe_result_t result = OE_UNEXPECTED;
    bool base_debug, extended_debug;
    const sgx_sigstruct_t* sigstruct;
    oe_eeid_t tmp_eeid;

    if (!eeid)
        OE_RAISE(OE_INVALID_PARAMETER);

    if (eeid->signature_size != 1808) // We only support SGX sigstructs for now.
        OE_RAISE(OE_VERIFY_FAILED);

    // Compute expected enclave hash
    OE_SHA256 computed_enclave_hash;
    oe_remeasure_memory_pages(eeid, &computed_enclave_hash, true);

    // Check recomputed enclave hash against reported enclave hash
    if (memcmp(
            computed_enclave_hash.buf,
            relevant_claims->enclave_hash,
            OE_SHA256_SIZE) != 0)
        OE_RAISE(OE_VERIFY_FAILED);

    if (memcmp(
            OE_DEBUG_PUBLIC_KEY,
            relevant_claims->signer_id,
            OE_SIGNER_ID_SIZE) != 0)
        OE_RAISE(OE_VERIFY_FAILED);

    sigstruct =
        (const sgx_sigstruct_t*)((uint8_t*)&eeid->data + eeid->data_size);

    // Compute and check base image hash
    *base_enclave_hash = sigstruct->enclavehash;
    OE_SHA256 computed_base_enclave_hash;
    tmp_eeid = *eeid;
    // If we saved non-zero heap/stack sizes for the base image, we could
    // add them here.
    tmp_eeid.size_settings.num_heap_pages = 0;
    tmp_eeid.size_settings.num_stack_pages = 0;
    tmp_eeid.size_settings.num_tcs = 1;
    oe_remeasure_memory_pages(&tmp_eeid, &computed_base_enclave_hash, false);

    if (memcmp(
            computed_base_enclave_hash.buf,
            *base_enclave_hash,
            OE_SHA256_SIZE) != 0)
        OE_RAISE(OE_VERIFY_FAILED);

    // Check other image properties have not changed
    base_debug = sigstruct->attributes.flags & SGX_FLAGS_DEBUG;
    extended_debug = relevant_claims->attributes & OE_REPORT_ATTRIBUTES_DEBUG;

    if (base_debug != extended_debug ||
        sigstruct->isvprodid != relevant_claims->product_id ||
        sigstruct->isvsvn != relevant_claims->security_version)
        OE_RAISE(OE_VERIFY_FAILED);

    // Check old signature (new signature has been checked above)
    if (base_debug && is_zero(sigstruct->signature, OE_KEY_SIZE))
        return OE_OK; // Unsigned debug image is ok.
    else
        OE_CHECK(_verify_base_image_signature(sigstruct));

    result = OE_OK;

done:
    return result;
}

static oe_result_t _hton_uint32_t(
    uint32_t x,
    uint8_t** position,
    size_t* remaining)
{
    if (*remaining < 4)
        return OE_INVALID_PARAMETER;

    for (size_t i = 0; i < 4; i++)
        (*position)[i] = (x >> (32 - ((i + 1) * 8))) & 0xFF;

    *position += 4;
    *remaining -= 4;

    return OE_OK;
}

static oe_result_t _ntoh_uint32_t(
    const uint8_t** position,
    size_t* remaining,
    uint32_t* x)
{
    if (*remaining < 4)
        return OE_INVALID_PARAMETER;

    *x = 0;
    for (size_t i = 0; i < 4; i++)
        *x = (*x << 8) | (*position)[i];

    *position += 4;
    *remaining -= 4;

    return OE_OK;
}

static oe_result_t _hton_uint64_t(
    uint64_t x,
    uint8_t** position,
    size_t* remaining)
{
    if (*remaining < 8)
        return OE_INVALID_PARAMETER;

    for (size_t i = 0; i < 8; i++)
        (*position)[i] = (x >> (64 - ((i + 1) * 8))) & 0xFF;

    *position += 8;
    *remaining -= 8;

    return OE_OK;
}

static oe_result_t _ntoh_uint64_t(
    const uint8_t** position,
    size_t* remaining,
    uint64_t* x)
{
    if (*remaining < 8)
        return OE_INVALID_PARAMETER;

    *x = 0;
    for (size_t i = 0; i < 8; i++)
        *x = (*x << 8) | (*position)[i];

    *position += 8;
    *remaining -= 8;

    return OE_OK;
}

static oe_result_t _hton_buffer(
    const uint8_t* buffer,
    size_t buffer_size,
    uint8_t** position,
    size_t* remaining)
{
    oe_result_t result = OE_UNEXPECTED;
    if (*remaining < buffer_size)
        return OE_INVALID_PARAMETER;
    OE_CHECK(oe_memcpy_s(*position, *remaining, buffer, buffer_size));
    *position += buffer_size;
    *remaining -= buffer_size;
    result = OE_OK;
done:
    return result;
}

static oe_result_t _ntoh_buffer(
    const uint8_t** position,
    size_t* remaining,
    uint8_t* buffer,
    size_t buffer_size)
{
    oe_result_t result = OE_UNEXPECTED;
    if (*remaining < buffer_size)
        return OE_INVALID_PARAMETER;
    OE_CHECK(oe_memcpy_s(buffer, *remaining, *position, buffer_size));
    *position += buffer_size;
    *remaining -= buffer_size;
    result = OE_OK;
done:
    return result;
}

size_t oe_eeid_byte_size(const oe_eeid_t* eeid)
{
    return sizeof(eeid->version) + sizeof(eeid->hash_state) +
           sizeof(eeid->signature_size) + sizeof(eeid->size_settings) +
           sizeof(eeid->vaddr) + sizeof(eeid->entry_point) +
           sizeof(eeid->tls_page_count) + sizeof(eeid->data_size) +
           eeid->data_size + eeid->signature_size;
}

oe_result_t oe_eeid_hton(
    const oe_eeid_t* eeid,
    uint8_t* buffer,
    size_t buffer_size)
{
    oe_result_t result = OE_UNEXPECTED;
    uint8_t* position = buffer;
    size_t remaining = buffer_size;

    if (!eeid || !buffer || buffer_size == 0 ||
        eeid->version != OE_EEID_VERSION)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(_hton_uint32_t(eeid->version, &position, &remaining));

    for (size_t i = 0; i < 8; i++)
        OE_CHECK(_hton_uint32_t(eeid->hash_state.H[i], &position, &remaining));
    for (size_t i = 0; i < 2; i++)
        OE_CHECK(_hton_uint32_t(eeid->hash_state.N[i], &position, &remaining));

    OE_CHECK(_hton_uint64_t(eeid->signature_size, &position, &remaining));

    OE_CHECK(_hton_uint64_t(
        eeid->size_settings.num_heap_pages, &position, &remaining));
    OE_CHECK(_hton_uint64_t(
        eeid->size_settings.num_stack_pages, &position, &remaining));
    OE_CHECK(
        _hton_uint64_t(eeid->size_settings.num_tcs, &position, &remaining));

    OE_CHECK(_hton_uint64_t(eeid->vaddr, &position, &remaining));
    OE_CHECK(_hton_uint64_t(eeid->entry_point, &position, &remaining));
    OE_CHECK(_hton_uint64_t(eeid->tls_page_count, &position, &remaining));

    OE_CHECK(_hton_uint64_t(eeid->data_size, &position, &remaining));
    OE_CHECK(_hton_buffer(
        eeid->data,
        eeid->data_size + eeid->signature_size,
        &position,
        &remaining));

    result = OE_OK;
done:
    return result;
}

oe_result_t oe_eeid_ntoh(
    const uint8_t* buffer,
    size_t buffer_size,
    oe_eeid_t* eeid)
{
    oe_result_t result = OE_UNEXPECTED;
    const uint8_t* position = buffer;
    size_t remaining = buffer_size;

    if (!buffer || buffer_size == 0 || !eeid)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(_ntoh_uint32_t(&position, &remaining, &eeid->version));

    if (eeid->version != OE_EEID_VERSION)
        OE_RAISE(OE_INVALID_PARAMETER);

    for (size_t i = 0; i < 8; i++)
        OE_CHECK(_ntoh_uint32_t(&position, &remaining, &eeid->hash_state.H[i]));
    for (size_t i = 0; i < 2; i++)
        OE_CHECK(_ntoh_uint32_t(&position, &remaining, &eeid->hash_state.N[i]));

    OE_CHECK(_ntoh_uint64_t(&position, &remaining, &eeid->signature_size));

    OE_CHECK(_ntoh_uint64_t(
        &position, &remaining, &eeid->size_settings.num_heap_pages));
    OE_CHECK(_ntoh_uint64_t(
        &position, &remaining, &eeid->size_settings.num_stack_pages));
    OE_CHECK(
        _ntoh_uint64_t(&position, &remaining, &eeid->size_settings.num_tcs));

    OE_CHECK(_ntoh_uint64_t(&position, &remaining, &eeid->vaddr));
    OE_CHECK(_ntoh_uint64_t(&position, &remaining, &eeid->entry_point));
    OE_CHECK(_ntoh_uint64_t(&position, &remaining, &eeid->tls_page_count));

    OE_CHECK(_ntoh_uint64_t(&position, &remaining, &eeid->data_size));
    OE_CHECK(_ntoh_buffer(
        &position,
        &remaining,
        eeid->data,
        eeid->data_size + eeid->signature_size));

    result = OE_OK;
done:
    return result;
}

oe_result_t oe_eeid_evidence_hton(
    const oe_eeid_evidence_t* evidence,
    uint8_t* buffer,
    size_t buffer_size)
{
    oe_result_t result = OE_UNEXPECTED;
    uint8_t* position = buffer;
    size_t remaining = buffer_size;
    size_t data_size = 0;

    if (!buffer || buffer_size == 0 || !evidence)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(
        _hton_uint64_t(evidence->base_evidence_size, &position, &remaining));
    OE_CHECK(_hton_uint64_t(evidence->eeid_size, &position, &remaining));

    data_size = evidence->base_evidence_size + evidence->eeid_size;

    OE_CHECK(_hton_buffer(evidence->data, data_size, &position, &remaining));

    result = OE_OK;
done:
    return result;
}

oe_result_t oe_eeid_evidence_ntoh(
    const uint8_t* buffer,
    size_t buffer_size,
    oe_eeid_evidence_t* evidence)
{
    oe_result_t result = OE_UNEXPECTED;
    const uint8_t* position = buffer;
    size_t remaining = buffer_size;
    size_t data_size = 0;

    if (!buffer || buffer_size == 0 || !evidence)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(
        _ntoh_uint64_t(&position, &remaining, &evidence->base_evidence_size));
    OE_CHECK(_ntoh_uint64_t(&position, &remaining, &evidence->eeid_size));

    data_size = evidence->base_evidence_size + evidence->eeid_size;

    OE_CHECK(_ntoh_buffer(
        &position, &remaining, (uint8_t*)&evidence->data, data_size));

    result = OE_OK;
done:
    return result;
}

oe_result_t oe_eeid_endorsements_hton(
    const oe_eeid_endorsements_t* endorsements,
    uint8_t* buffer,
    size_t buffer_size)
{
    oe_result_t result = OE_UNEXPECTED;
    uint8_t* position = buffer;
    size_t remaining = buffer_size;
    size_t data_size = 0;

    if (!buffer || buffer_size == 0 || !endorsements)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(_hton_uint64_t(
        endorsements->sgx_endorsements_size, &position, &remaining));
    OE_CHECK(_hton_uint64_t(
        endorsements->eeid_endorsements_size, &position, &remaining));

    data_size = endorsements->sgx_endorsements_size +
                endorsements->eeid_endorsements_size;

    OE_CHECK(
        _hton_buffer(endorsements->data, data_size, &position, &remaining));

    result = OE_OK;
done:
    return result;
}

oe_result_t oe_eeid_endorsements_ntoh(
    const uint8_t* buffer,
    size_t buffer_size,
    oe_eeid_endorsements_t* endorsements)
{
    oe_result_t result = OE_UNEXPECTED;
    const uint8_t* position = buffer;
    size_t remaining = buffer_size;
    size_t data_size = 0;

    if (!buffer || buffer_size == 0 || !endorsements)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(_ntoh_uint64_t(
        &position, &remaining, &endorsements->sgx_endorsements_size));
    OE_CHECK(_ntoh_uint64_t(
        &position, &remaining, &endorsements->eeid_endorsements_size));

    data_size = endorsements->sgx_endorsements_size +
                endorsements->eeid_endorsements_size;

    OE_CHECK(_ntoh_buffer(
        &position, &remaining, (uint8_t*)&endorsements->data, data_size));

    result = OE_OK;
done:
    return result;
}