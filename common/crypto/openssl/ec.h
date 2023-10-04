// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifndef _OE_COMMON_CRYPTO_OPENSSL_EC_H
#define _OE_COMMON_CRYPTO_OPENSSL_EC_H

#include <openenclave/internal/ec.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#include <openssl/encoder.h>
#include <openssl/param_build.h>
#endif

/* Caller is responsible for validating parameters */
void oe_ec_public_key_init(oe_ec_public_key_t* public_key, EVP_PKEY* pkey);

void oe_ec_private_key_init(oe_ec_private_key_t* private_key, EVP_PKEY* pkey);

#endif /* _OE_COMMON_CRYPTO_OPENSSL_EC_H */
