// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifndef _OE_SGXCREATE_H
#define _OE_SGXCREATE_H

#include <openenclave/bits/eeid.h>
#include <openenclave/bits/result.h>
#include <openenclave/bits/sgx/sgxtypes.h>
#include "../host.h"
#include "crypto/sha.h"
#include "load.h"

OE_EXTERNC_BEGIN

typedef struct _oe_enclave oe_enclave_t;
typedef oe_sgx_enclave_setting_config_data oe_config_data_t;

typedef enum _oe_sgx_load_type
{
    OE_SGX_LOAD_TYPE_UNDEFINED,
    OE_SGX_LOAD_TYPE_CREATE,
    OE_SGX_LOAD_TYPE_MEASURE,
    __OE_SGX_LOAD_TYPE_MAX = OE_ENUM_MAX,
} oe_sgx_load_type_t;

OE_STATIC_ASSERT(sizeof(oe_sgx_load_type_t) == sizeof(unsigned int));

typedef enum _oe_sgx_load_state
{
    OE_SGX_LOAD_STATE_UNINITIALIZED,
    OE_SGX_LOAD_STATE_INITIALIZED,
    OE_SGX_LOAD_STATE_ENCLAVE_CREATED,
    OE_SGX_LOAD_STATE_ENCLAVE_INITIALIZED,
    __OE_SGX_LOAD_STATE_MAX = OE_ENUM_MAX,
} oe_sgx_load_state_t;

OE_STATIC_ASSERT(sizeof(oe_sgx_load_state_t) == sizeof(unsigned int));

typedef struct _oe_sgx_load_context oe_sgx_load_context_t;

struct _oe_sgx_load_context
{
    oe_sgx_load_type_t type;
    oe_sgx_load_state_t state;

    /* attributes includes:
     *  - OE_FLAG bits to be applied to the enclave, such as debug.
     *  - XFRM supported by the OS to be used in enclave creation.
     */
    sgx_attributes_t attributes;

    /* Fields used when attributes contain OE_FLAG_SIMULATION */
    struct
    {
        /* Base address of enclave */
        void* addr;

        /* Size of enclave in bytes */
        size_t size;
    } sim;

    /* Hash context used to measure enclave as it is loaded */
    oe_sha256_context_t hash_context;

#ifdef OE_WITH_EXPERIMENTAL_EEID
    /* EEID data needed during enclave creation */
    oe_eeid_t* eeid;
#endif

    const oe_config_data_t* config_data;
    bool use_config_id;

    bool capture_pf_gp_exceptions_enabled;

    bool create_zero_base_enclave;
    uint64_t start_address; /* Valid only if create_zero_base_enclave is True */
};

oe_result_t oe_sgx_initialize_load_context(
    oe_sgx_load_context_t* context,
    oe_sgx_load_type_t type,
    uint64_t attributes);

void oe_sgx_cleanup_load_context(oe_sgx_load_context_t* context);

oe_result_t oe_sgx_build_enclave(
    oe_sgx_load_context_t* context,
    const char* path,
    const oe_sgx_enclave_properties_t* properties,
    oe_enclave_t* enclave);

/**
 * Validate certain fields of an SGX enclave properties structure.
 *
 * This function checks whether the following fields of the
 * **oe_sgx_enclave_properties_t** structure have valid values.
 *
 *     - product_id
 *     - security_version
 *     - num_stack_pages
 *     - num_heap_pages
 *     - num_tcs
 *
 * If not the **field_name** output parameter points to the name of the first
 * field with an invalid value.
 *
 * @param properties SGX enclave properties
 * @param field_name[output] name of first invalid field (may be null)
 *
 * @returns OE_OK
 * @returns OE_INVALID_PARAMETER a parameter is null
 * @returns OE_FAILURE at least one field is invalid
 *
 */
oe_result_t oe_sgx_validate_enclave_properties(
    const oe_sgx_enclave_properties_t* properties,
    const char** field_name);

bool oe_sgx_is_kss_supported(void);
bool oe_sgx_is_misc_region_supported(void);

OE_EXTERNC_END

#endif /* _OE_SGXCREATE_H */
