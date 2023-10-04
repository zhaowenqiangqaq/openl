// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/host.h>
#include <openenclave/internal/error.h>
#include <openenclave/internal/load.h>
#include <openenclave/internal/sgx/tests.h>
#include <openenclave/internal/tests.h>
#include <stdio.h>

#include "oesign_test_u.h"

int main(int argc, const char* argv[])
{
    oe_result_t result;
    oe_result_t ecall_result;
    oe_enclave_image_t oeimage;
    oe_sgx_enclave_properties_t properties;
    oe_enclave_t* enclave = NULL;
    bool is_signed = false;

    if (argc != 2)
    {
        oe_put_err("Usage: %s enclave_image_path\n", argv[0]);
    }

    // Create the enclave
    uint32_t flags = oe_get_create_flags();
    bool is_kss_supported = oe_sgx_is_kss_supported();

    /* Load the ELF image */
    if ((result = oe_load_enclave_image(argv[1], &oeimage)) != OE_OK)
    {
        oe_put_err("oe_load_enclave_image(): result=%u", result);
    }

    /* Load the SGX enclave properties */
    if ((result = oe_sgx_load_enclave_properties(&oeimage, &properties)) !=
        OE_OK)
    {
        oe_put_err("oe_sgx_load_enclave_properties(): result=%u", result);
    }

    if (properties.config.flags.create_zero_base_enclave &&
        (properties.config.attributes & OE_ENCLAVE_FLAG_SIMULATE))
    {
        printf("0-base enclave creation is not supported in simulation-mode. "
               "Test not run.");
        return 0;
    }

    if ((result = oe_create_oesign_test_enclave(
             argv[1], OE_ENCLAVE_TYPE_AUTO, flags, NULL, 0, &enclave)) != OE_OK)
    {
        if (!is_kss_supported &&
            (result == OE_UNSUPPORTED &&
             properties.config.attributes & OE_SGX_FLAGS_KSS))
        {
            // Skip the test as it is not supported
            printf("Skipping enclave test with kss as it is not supported by "
                   "current platform...\n");
            return 0;
        }
        else
            oe_put_err("oe_create_crypto_enclave(): result=%u", result);
    }

    if (flags & OE_ENCLAVE_FLAG_SIMULATE)
    {
        /* Skip MRSIGNER check because the enclave call to oe_get_report is not
         * supported in simulation mode
         */
        printf(
            "Skipping enclave report MRSIGNER check in simulation mode...\n");
    }
    else
    {
        if ((result = is_test_signed(enclave, &is_signed)) != OE_OK)
        {
            oe_put_err("verify_signed() failed: result=%u", result);
        }

        if (!is_signed)
        {
            oe_put_err("%s is signed with a default debug signature", argv[1]);
        }
    }

    /* check_kss_extended_ids currently assumes the quote provider is available.
     * Skip if there is no quote provider for now.
     */
    if (is_kss_supported && oe_sgx_has_quote_provider())
    {
        result = check_kss_extended_ids(
            enclave,
            &ecall_result,
            (oe_uuid_t*)properties.config.family_id,
            (oe_uuid_t*)properties.config.extended_product_id);
        if (result != OE_OK || ecall_result != OE_OK)
        {
            oe_put_err(
                "verify_signed() failed: Enclave: %s, Host: %s\n",
                oe_result_str(ecall_result),
                oe_result_str(result));
        }
    }

    if ((result = oe_terminate_enclave(enclave)) != OE_OK)
    {
        oe_put_err("oe_terminate_enclave() failed: %u", result);
    }

    printf("=== passed all tests (%s)\n", argv[0]);

    return 0;
}
