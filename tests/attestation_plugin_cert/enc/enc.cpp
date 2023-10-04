// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifdef OE_USE_OPENSSL
#include <openssl/evp.h>
#define OE_KEY_TYPE_EC EVP_PKEY_EC
#define OE_KEY_TYPE_RSA EVP_PKEY_RSA
#else /* Default to mbedtls */
#include <mbedtls/pk.h>
#define OE_KEY_TYPE_EC MBEDTLS_PK_ECKEY
#define OE_KEY_TYPE_RSA MBEDTLS_PK_RSA
#endif
#include <openenclave/attestation/attester.h>
#include <openenclave/attestation/sgx/evidence.h>
#include <openenclave/attestation/verifier.h>
#include <openenclave/bits/evidence.h>
#include <openenclave/edger8r/enclave.h>
#include <openenclave/enclave.h>
#include <openenclave/internal/raise.h>
#include <openenclave/internal/report.h>
#include <openenclave/internal/sgx/plugin.h>
#include <openenclave/internal/tests.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsa.h"
#include "tls_t.h"

// This is the claims validation callback. A TLS connecting party (client or
// server) can verify the passed in claims to decide whether to
// accept a connection request
oe_result_t enclave_claims_verifier(
    oe_claim_t* claims,
    size_t claims_length,
    void* arg)
{
    oe_result_t result = OE_VERIFY_FAILED;

    (void)arg;
    OE_TRACE_INFO("enclave_claims_verifier is called with claims:\n");

    for (size_t i = 0; i < claims_length; i++)
    {
        oe_claim_t* claim = &claims[i];
        if (strcmp(claim->name, OE_CLAIM_SECURITY_VERSION) == 0)
        {
            uint32_t security_version = *(uint32_t*)(claim->value);
            // Check the enclave's security version
            if (security_version < 1)
            {
                OE_TRACE_ERROR(
                    "identity->security_version checking failed (%d)\n",
                    security_version);
                goto done;
            }
        }
        // Dump an enclave's unique ID, signer ID and Product ID. They are
        // MRENCLAVE, MRSIGNER and ISVPRODID for SGX enclaves. In a real
        // scenario, custom id checking should be done here
        else if (
            strcmp(claim->name, OE_CLAIM_SIGNER_ID) == 0 ||
            strcmp(claim->name, OE_CLAIM_UNIQUE_ID) == 0 ||
            strcmp(claim->name, OE_CLAIM_PRODUCT_ID) == 0)
        {
            OE_TRACE_INFO("Enclave %s:\n", claim->name);
            for (size_t j = 0; j < claim->value_size; j++)
            {
                OE_TRACE_INFO("0x%0x ", claim->value[j]);
            }
        }
    }

    result = OE_OK;
done:
    return result;
}

// input: input_data and input_data_len
// output: key, key_size
oe_result_t generate_key_pair(
    int key_type,
    uint8_t** public_key,
    size_t* public_key_size,
    uint8_t** private_key,
    size_t* private_key_size)
{
    oe_result_t result = OE_FAILURE;
    oe_asymmetric_key_params_t params;
    char user_data[] = "test user data!";
    size_t user_data_size = sizeof(user_data) - 1;

    OE_TRACE_INFO("Generate key pair");

    if (key_type == OE_KEY_TYPE_EC)
    {
        params.type =
            OE_ASYMMETRIC_KEY_EC_SECP256P1; // MBEDTLS_ECP_DP_SECP256R1
        params.format = OE_ASYMMETRIC_KEY_PEM;
        params.user_data = user_data;
        params.user_data_size = user_data_size;
        result = oe_get_public_key_by_policy(
            OE_SEAL_POLICY_UNIQUE,
            &params,
            public_key,
            public_key_size,
            nullptr,
            nullptr);
        OE_CHECK(result);

        result = oe_get_private_key_by_policy(
            OE_SEAL_POLICY_UNIQUE,
            &params,
            private_key,
            private_key_size,
            nullptr,
            nullptr);
        OE_CHECK(result);
    }
    else if (key_type == OE_KEY_TYPE_RSA)
    {
        OE_CHECK(generate_rsa_pair(
            public_key, public_key_size, private_key, private_key_size));
    }
    else
    {
        OE_RAISE_MSG(OE_FAILURE, "Unsupported key type [%d]\n", key_type);
    }

    result = OE_OK;

done:
    return result;
}

oe_result_t get_tls_cert_signed_with_key(
    int key_type,
    unsigned char** cert,
    size_t* cert_size)
{
    oe_result_t result = OE_FAILURE;
    uint8_t* host_certificate_buffer = nullptr;

    uint8_t* output_certificate = nullptr;
    size_t output_certificate_size = 0;

    uint8_t* private_key = nullptr;
    size_t private_key_size = 0;
    uint8_t* public_key = nullptr;
    size_t public_key_size = 0;
    uint8_t* optional_parameters = nullptr;
    size_t optional_parameters_size = 0;
    const oe_uuid_t format = {OE_FORMAT_UUID_SGX_ECDSA};

    oe_claim_t* claims = nullptr;
    size_t claims_length = 0;

    OE_TRACE_INFO("called into enclave\n");

    // generate public/private key pair
    result = generate_key_pair(
        key_type,
        &public_key,
        &public_key_size,
        &private_key,
        &private_key_size);
    if (result != OE_OK)
    {
        OE_TRACE_ERROR(" failed with %s\n", oe_result_str(result));
        goto done;
    }
    if (result != OE_OK)
    {
        OE_TRACE_ERROR(" failed with %s\n", oe_result_str(result));
        goto done;
    }

    OE_TRACE_INFO("private key:[%s]\n", private_key);
    OE_TRACE_INFO("public key:[%s]\n", public_key);

    // Initialize built-in OE attesters.
    oe_attester_initialize();

    result = oe_get_attestation_certificate_with_evidence_v2(
        &format,
        (const unsigned char*)"CN=Open Enclave SDK,O=OESDK TLS,C=US",
        private_key,
        private_key_size,
        public_key,
        public_key_size,
        optional_parameters,
        optional_parameters_size,
        &output_certificate,
        &output_certificate_size);
    if (result != OE_OK)
    {
        OE_TRACE_ERROR(
            "oe_get_attestation_certificate_with_evidence_v2 failed with %s\n",
            oe_result_str(result));
        goto done;
    }

    OE_TRACE_INFO("output_certificate_size = 0x%x", output_certificate_size);

    oe_verifier_initialize();
    // validate cert inside the enclave
    result = oe_verify_attestation_certificate_with_evidence_v2(
        output_certificate,
        output_certificate_size,
        nullptr,
        0,
        nullptr,
        0,
        &claims,
        &claims_length);

    OE_TRACE_INFO(
        "\nFrom inside enclave: "
        "oe_verify_attestation_certificate_with_evidence_v2 verifying the "
        "certificate... %s\n",
        oe_result_str(result));

    OE_CHECK(result);

    result = enclave_claims_verifier(claims, claims_length, nullptr);

    OE_TRACE_INFO(
        "\nFrom inside enclave: enclave_claims_verifier verifying the "
        "claims... %s\n",
        oe_result_str(result));

    OE_CHECK(result);

    // copy cert to host memory
    host_certificate_buffer = (uint8_t*)oe_host_malloc(output_certificate_size);
    if (host_certificate_buffer == nullptr)
    {
        result = OE_OUT_OF_MEMORY;
        goto done;
    }

    // copy to the host for host-side validation test
    memcpy(
        host_certificate_buffer, output_certificate, output_certificate_size);
    *cert_size = output_certificate_size;
    *cert = host_certificate_buffer;
    OE_TRACE_INFO("*cert = %p", *cert);
    OE_TRACE_INFO("*cert_size = 0x%x", *cert_size);

done:

    free(private_key);
    free(public_key);
    oe_free_claims(claims, claims_length);

    oe_attester_shutdown();
    oe_verifier_shutdown();

    oe_free_attestation_certificate(output_certificate);

    return result;
}

oe_result_t get_tls_cert_signed_with_ec_key(
    unsigned char** cert,
    size_t* cert_size)
{
    return get_tls_cert_signed_with_key(OE_KEY_TYPE_EC, cert, cert_size);
}

oe_result_t get_tls_cert_signed_with_rsa_key(
    unsigned char** cert,
    size_t* cert_size)
{
    return get_tls_cert_signed_with_key(OE_KEY_TYPE_RSA, cert, cert_size);
}

OE_SET_ENCLAVE_SGX(
    1,    /* ProductID */
    1,    /* SecurityVersion */
    true, /* Debug */
    2048, /* NumHeapPages */
    128,  /* NumStackPages */
    1);   /* NumTCS */
