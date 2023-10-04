# Open Enclave Support for OpenSSL

#### OpenSSL 1.1.1 on OE is configured with following options:
- no-afalgeng
  - Disable the AF_ALG hardware engine.
- no-aria
  - Disable ARIA block cipher.
- no-autoerrinit
- no-autoload-config
- no-bf
  - Disable Blowfish.
- no-blake2
  - Disable Blake2.
- no-camellia
  - Disable camellia.
- no-capieng
  - Disable the Microsoft CryptoAPI engine.
- no-cast
  - Disable CAST5 block cipher.
- no-chacha
  - Disable CHACHA.
- no-cms
  - Disable cryptographic message syntax that handles S/MIME v3.1 mail.
- no-ct
  - Disable certificate transparency as it depends on host file system (CT log files).
- no-dso
  - Disable the OpenSSL DSO API.
- no-gost
  - Disable the Russian GOST crypto engine that requires dynamic loading.
- no-hw
- no-idea
  - Disable IDEA.
- no-md2
  - Disable MD2.
- no-md4
  - Disable MD4.
- no-mdc2
  - Disable MDC2.
- no-nextprotoneg
  - Disable Next Protocol Negotiation (NPN).
- no-poly1305
  - Disable Poly-1305.
- no-psk
  - Disable PSK.
- no-rc4
  - Disable RC4.
- no-rfc3779
  - Disable RFC 3379 (Delegated Path Discovery).
- no-rmd160
  - Disable RIPEMD-160.
- no-scrypt
  - Disable scrypt KDF.
- no-seed
  - Disable SEED ciphersuites.
- no-shared
- no-siphash
  - Disable SipHash.
- no-sm2
  - Disable Chinese cryptographic algorithms.
- no-sm3
  - Disable Chinese cryptographic algorithms.
- no-sm4
  - Disable Chinese cryptographic algorithms
- no-srp
  - Disable Secure Remote Password (SRP).
- no-ssl2
- no-ssl3
- no-ui-console
  - Disable support for the openssl command-line tool that is not required by OE.
- no-whirlpool
  - Disable Whirlpool hash.
- no-zlib
  - Disable the ZLIb support.
- --with-rand-seed=none

*Note:* The autoalginit option is required by APIs (e.g., EVP) that retrieve algorithms by name so can not be disabled.

OE also explicitly disables the following feature:
- SECURE_MEMORY
  - Require mman features (e.g., madvise, mlock, mprotect) that are not supported inside an enclave.

In addition, OpenSSL by default disables the following algorithms/features
- MD2
- RC5
- EC_NISTP_64_GCC_128
- EGD (Entropy Gathering Daemon)
- Heartbeats extension
- SCTP (Stream Control Transimission Protocol) protocol

#### OpenSSL 3.1 on OE is configured with the following additional options:

- --with-rand-seed=rdcpu
  - Build with rdcpu provider. Engines in OpenSSL 3.1 are deprecated so we no longer use RDRAND engine as with OpenSSL 1.1.1.
- no-cmp
  - Disable Certificate Management Protocol (CMP) and Certificate Request Message Format (CRMF). See [Security Guidance](#security-guidance-for-using-openssl-apismacros) for more information about how we recommend managing X509 certificates in the enclave.
- no-legacy
  - Disable legacy provider.
- no-padlockeng
  - This is synonomous with `no-hw-padlock` and `no-hw` flag used with OpenSSL 1.1.1.
- no-siv
  - Disable SIV block cipher.
- no-ssl-trace
  - Disable SSL Trace capabilities. (OpenSSL 1.1.1 builds without SSL Trace by default.)
- no-ssl
  - `no-ssl` is synonymous with `no-ssl3`; `no-ssl2` is deprecated.
- no-uplink
  - Disable UPLINK interface.

# OpenSSL 3.1 Support

The default OpenSSL enclave build option uses version 1.1.1 of OpenSSL. There are separate libraries and headers for building OpenSSL 3.1 in an enclave.

For users who want to continue to use OpenSSL 1.1.1, you do not need to make any changes.

### CMake

Use target `openssl_3` instead of target `openssl`.

### GNU Make

When using pkgconfig, use `openssl_3libs` or `openssl_3libslvicfg` (based on configuration) *in addition to* `libs` to get OpenSSL 3.1 specific libraries. Use `openssl_3flags` *instead of* `flags` to include OpenSSL 3.1 specific header directories.

Example using clang:
```
cflags=`pkg-config oeenclave-clang++ --variable=openssl_3flags`
libs=`pkg-config oeenclave-clang++ --libs`
cryptolibs=`pkg-config oeenclave-clang++ --variable=openssl_3libs`
$ clang++-11 ${cflags} -o enc enc.cpp ${libs} ${cryptolibs}
```

You should not build with `openssllibs` or `openssllibslvicfg`, which should only be used with OpenSSL 1.1.1.

See also: [pkgconfig README](/pkgconfig/README.md#building-enclave-applications).

# Threads Support

*Note:* Only the version after v0.17.2 has the `threads` support. Previous versions of
OpenSSL are not built with this support and therefore are not suitable for multi-threaded
applications.

OE SDK configures the OpenSSL with `threads` support, which uses OE's thread lock
primitives, to ensure thread-safe when accessing internal objects. Note that the lock
primitives are based on internal OCALLS (`OE_OCALL_THREAD_WAIT` and `OE_OCALL_THREAD_WAKE`)
given that SGX does not support such mechanisms. This support allows the developers
to use the library in multi-threaded enclave applications. Note that the thread saftey
holds only if the host is not compromised.

# How to use RAND APIs

*Note:* Starting from v0.13, users no longer need to manually opt into the RDRAND engine (as described in this
section) when linking an enclave against `oecryptoopenssl`.

*Note:* This information is specific to OpenSSL 1.1.1. OpenSSL 3.1 uses the rdcpu provider instead of the RDRAND engine.

Currently, the default RAND method used by RAND APIs is not supported by OE. More specifically,
the default OpenSSL RAND method relies on the `rdtsc` instruction, which is not supported by SGXv1 enclaves.
Therefore, OE currently does not support RAND APIs if users try to use them directly (by default, the RAND APIs depend on the default
RAND method). To enable RAND APIs, OE recommends to use the OpenSSL RDRAND engine, which explicitly replaces the
default RAND method with the `rdrand`-based method. That is, the RAND APIs will obtain the random bytes directly using the
`rdrand` instruction. To opt-in the RDRAND engine, see the following example.

```c
void enc_rand()
{
    int data = 0;
    ENGINE* eng = NULL;

    /* Initialize and opt-in the RDRAND engine. */
    ENGINE_load_rdrand();
    eng = ENGINE_by_id("rdrand");
    if (eng == NULL)
    {
        goto done;
    }

    if (!ENGINE_init(eng))
    {
        goto done;
    }

    if (!ENGINE_set_default(eng, ENGINE_METHOD_RAND))
    {
        goto done;
    }

    /* Now RAND APIs are available. */

    /* Test the RAND_bytes API. */
    if (!RAND_bytes((unsigned char*)&data, sizeof(data)))
    {
        goto done;
    }

    printf("RAND_bytes() %d\n", data);

done:

    /* cleanup to avoid memory leak. */
    ENGINE_finish(eng);
    ENGINE_free(eng);
    ENGINE_cleanup();

    return;
}
```

Note that the code snippet for the RDRAND engine opt-in is required to use not only RAND APIs
but also other OpenSSL APIs that internally depend on the RAND APIs. Alternatively, developers
can implement their own RAND method to replace the default method via `RAND_set_rand_method` API.

## Security Guidance

#### OpenSSL APIs/Macros

OpenSSL provides APIs that allow users to configure sensitive settings like certificate trust and cipher suite preference from files.
Because the host file system is considered untrusted in contexts such as SGX enclaves, OE SDK marks these APIs as unsupported to discourage their use.
OE SDK does this by patching the OpenSSL headers that include such APIs or macros (i.e., appending the inclusion of an "*_unsupported.h" file to these headers).
This ensures that when an enclave includes a patched OpenSSL header and uses the specific API or macro, user will receive compile-time errors.
The errors can be disabled by specifying the `OE_OPENSSL_SUPPRESS_UNSUPPORTED` option to the compiler.

See the following table for the detailed list of APIs/Macros.

API / Macro | Original header | Comments | Guidance |
:---:|:---:|:---|:---|
OPENSSL_INIT_LOAD_CONFIG | crypto.h | The macro represents an option to the OPENSSL_init_ssl and OPENSSL_init_crypto APIs that initializes an application based on the openssl.cnf (loaded from the host filesystem). Therefore, the use of this option would allow an untrusted host to fully control the initialization of the application. | The recommendation is not to invoke initialization APIs with this option. Note that starting from v1.1.0, the explicit initialization is not required. |
SSL_CTX_load_verify_locations, SSL_CTX_load_verify_dir, SSL_CTX_load_verify_file, SSL_CTX_load_verify_store | ssl.h | These APIs specify locations, directories, files, or stores from which an application adds CA certificates for verification purposes. These APIs would allow an untrusted host to control what certificates the application will trust. | The recommendation is using the SSL_CTX_set_cert_store API that specifies an in-memory certificate verification storage (`X509_STORE`). Another alternative is to implement a customized verification callback and sets the callback via the SSL_CTX_set_verify API, which effectively bypasses the default implementation. |
SSL_CTX_set_default_verify_paths, SSL_CTX_set_default_verify_dir, SSL_CTX_set_default_verify_file, SSL_CTX_set_default_verify_store | ssl.h | These APIs specify the default locations, directories, files, or stores from which an application looks up CA certificates for verification purposes. These APIs would allow an untrusted host to control what certificates the application will trust. | The recommendation is using the SSL_CTX_set_cert_store API that specifies an in-memory certificate verification storage (`X509_STORE`). Another alternative is to implement a customized verification callback and sets the callback via the SSL_CTX_set_verify API, which effectively bypasses the default implementation. |
X509_LOOKUP_file, X509_LOOKUP_hash_dir, X509_LOOKUP_hash_store | x509_vfy.h | These APIs return a `X509_LOOKUP_METHOD` method that loads files or stores from the path specified by the `SSL_CERT_DIR` environment variable. This would allow an untrusted host to control what files the enclave will load. These APIs are also used internally by SSL_CTX_set_default_verify_dir and SSL_CTX_set_default_verify_paths. | The recommendation is to implement a customized `X509_LOOKUP_METHOD` method based on in-memory operations. Note that to opt-in the new method, the user needs to explicitly register the method to a `X509_STORE` via the X509_STORE_add_lookup API and the set the `X509_STORE` to an `SSL_CTX` via SSL_CTX_set_cert_store. |
X509_STORE_load_file_ex, X509_STORE_load_file, X509_STORE_load_path, X509_STORE_load_store_ex, X509_STORE_load_store, X509_STORE_load_locations_ex, X509_STORE_load_locations | x509_vfy.h | These APIs add X509_LOOKUP methods that can access files, directories, or stores to a `X509_STORE` via X509_STORE_add_lookup API. The API is used internally by SSL_CTX_load_verify_locations. | The recommendation is to add a customized `X509_LOOKUP_METHOD` method to the `X509_STORE` via X509_STORE_add_lookup. |
X509_STORE_set_default_paths, X509_STORE_set_default_paths_ex | x509_vfy.h | These APIs add X509_LOOKUP_file or X509_LOOKUP_hash_dir to a `X509_STORE` via X509_STORE_add_lookup. This API is used internally by SSL_CTX_set_default_verify_paths. | The recommendation is to add a customized `X509_LOOKUP_METHOD` method to the `X509_STORE` via X509_STORE_add_lookup. |
X509_load_cert_file_ex, X509_load_cert_file, X509_load_crl_file, X509_load_cert_crl_file_ex, X509_load_cert_crl_file | x509_vfy.h | These APIs load certificates from the untrusted host filesystem and add the certificates to the `X509_STORE` via the X509_STORE_add_cert API. Some of these APIs are used internally by X509_LOOKUP_hash_dir and X509_LOOKUP_file methods. | The recommendation is not to use this API. An alternative is obtaining in-memory certificates in a secure manner (e.g., secure channel, encrypted storage) and adding the certificates to the `X509_STORE` via X509_STORE_add_cert or X509_STORE_add_crl. |
X509_LOOKUP_ctrl_ex, X509_LOOKUP_ctrl, X509_LOOKUP_load_file_ex, X509_LOOKUP_load_file, X509_LOOKUP_add_dir, X509_LOOKUP_add_store_ex, X509_LOOKUP_add_store, X509_LOOKUP_load_store_ex, X509_LOOKUP_load_store | x509_vfy.h | These APIs can add additional files, directories, or stores to existing X509_LOOKUP_file, X509_LOOKUP_dir, or X509_LOOKUP_store methods. Note that the latter functions are just macro wrappers for X509_LOOKUP_ctrl_ex and X509_LOOKUP_ctrl. | The recommendation is to use a customized `X509_LOOKUP_METHOD` method that should not need to be modified using these X509_LOOKUP_ APIs. |

#### OpenSSL TLS/SSL Configuration

Given that TLS 1.0 and 1.1 are no longer considered secure (have been deprecated by major browsers),
OE SDK recommends users to use TLS 1.2 and above. However, the default set of cipher suites
and elliptic curves come with TLS 1.2 and 1.3 configurations in OpenSSL still include less secure ones.
To help reducing the risk, OE SDK recommends users to configure a TLS/SSL server to use the following cipher suites
(with the exact order) and elliptic curves.

- TLS 1.3 cipher suites
  ```
  TLS13-AES-256-GCM-SHA384
  TLS13-AES-128-GCM-SHA256
  ```
- TLS 1.2 cipher suites:
  ```
  ECDHE-ECDSA-AES128-GCM-SHA256
  ECDHE-ECDSA-AES256-GCM-SHA384
  ECDHE-RSA-"AES128-GCM-SHA256
  ECDHE-RSA-AES256-GCM-SHA384
  ECDHE-ECDSA-AES128-SHA256
  ECDHE-ECDSA-AES256-SHA384
  ECDHE-RSA-AES128-SHA256
  ECDHE-RSA-AES256-SHA384
  ```
- Elliptic curve algorithms
  ```
  P-521
  P-384
  P-256
  ```

Refer to the [attested tls sample](/samples/attested_tls/README.md) as an example of how to
configure a TLS/SSL server with the recommended configuration.

## API Support

### Headers in OpenSSL 1.1.1

Header | Supported | Comments |
:---:|:---:|:---|
aes.h | Yes | - |
asn1.h | Yes | ASN1_TIME_* APIs tests (asn1_test_time) is disabled. Refer to the [unsupported test list](/tests/openssl/tests.unsupported) for more detail. |
asn1_mac.h | Yes | - |
asn1err.h | Yes | - |
asn1t.h | Yes | - |
async.h | Yes | - |
asyncerr.h | Yes | - |
bio.h | Partial | SCTP support is disabled by default. |
bioerr.h | Yes | - |
blowfish.h | No | Blowfish is disabled by default. |
bn.h | Yes | - |
bnerr.h | Yes | - |
buffer.h | Yes | - |
buffererr.h | Yes | - |
camellia.h | No | Camellia is disabled by OE. |
cast.h | No | CAST5 is disabled by OE. |
cmac.h | Yes | - |
cms.h | Yes | - |
cmserr.h | Yes | - |
comp.h | Yes | - |
comperr.h | Yes | - |
conf.h | Yes | - |
conf_api.h | Yes | - |
conferr.h | Yes | - |
crypto.h | Yes | SECURE_MEMORY APIs (e.g., CRYPTO_secure_malloc, CRYPT_secure_free) are disabled. The `OPENSSL_INIT_LOAD_CONFIG` macro is disabled for security concerns. Refer to [Security Guidance for using OpenSSL APIs/Macros](#security-guidance) for more detail. |
cryptoerr.h | Yes | - |
ct.h | No | Certificate Transparency is disabled by OE. |
cterr.h | Yes | - |
des.h | Yes | - |
dh.h | Yes | - |
dherr.h | Yes | - |
dsa.h | Yes | - |
dsaerr.h | Yes | - |
dtls1.h | Yes | - |
e_os2.h | Yes | - |
ebcdic.h | Yes | - |
ec.h | Partial | EC_NISTP_64_GCC_128 is disabled by default. |
ecdh.h | Yes | - |
ecdsa.h | Yes | - |
ecerr.h | Yes | - |
engine.h | Yes | - |
engineerr.h | Yes | - |
err.h | Yes | - |
evp.h | Partial | Multiple algorithms, including MD2 and RC5, are disabled by default or by OE. Refer to [Open Enclave Support for OpenSSL](#open-enclave-support-for-openssl) for a complete list. |
evperr.h | Yes | - |
hmac.h | Yes | - |
idea.h | No | IDEA is disabled by OE. |
kdf.h | Yes | - |
kdferr.h | Yes | - |
lhash.h | Yes | The lhash test is disabled because of requiring too much heap size. Refer to the [unsupported test list](/tests/openssl/tests.unsupported) for more detail. |
md2.h | No | MD2 is disabled by default (header is present). |
md4.h | No | MD4 is disabled by OE. |
md5.h | Yes | - |
mdc2.h | No | MDC2 is disabled by OE. |
modes.h | Yes | - |
obj_mac.h | Yes | - |
objects.h | Yes | - |
objectserr.h | Yes | - |
ocsp.h | Yes | - |
ocsperr.h | Yes | - |
opensslv.h | Yes | - |
ossl_typ.h | Yes | - |
pem.h | Yes | - |
pem2.h | Yes | - |
pemerr.h | Yes | - |
pkcs12.h | Yes | - |
pkcs12err.h | Yes | - |
pkcs7.h | Yes | - |
pkcs7err.h | Yes | - |
rand.h | Partial | EGD is disabled by default. The default method (RAND_OpenSSL) does not work because the depending `rdtsc` instruction is not supported by SGXv1. Refer to [How to use RAND APIs](#how-to-use-rand-apis) for more detail. |
rand_drbg.h | Partial | OE by default does not depend on the default rand method. Therefore, rand_drbg APIs are supported but have no impact on rand APIs. The drbg test is disabled. Refer to the [unsupported test list](/tests/openssl/tests.unsupported) for more detail. |
randerr.h | Yes | - |
rc2.h | Yes | - |
rc4.h | No | RC4 is disabled by OE. |
rc5.h | No | RC5 is disabled by default (header is present). |
ripemd.h | No | RIPEMD-160 is disabled by OE.  |
rsa.h | Yes | - |
rsaerr.h | Yes | - |
safestack.h | Yes | - |
seed.h | No | SEED is disabled by OE. |
sha.h | Yes | - |
srp.h | No | SRP is disabled by OE. |
srtp.h | Yes | - |
ssl.h | Partial | SSL2 and SSL3 methods are disabled. Heartbeats extension is disabled by default. Functions that are unsupported by OE for security concerns include: `SSL_CTX_set_default_verify_paths`, `SSL_CTX_set_default_verify_dir`, `SSL_CTX_set_default_verify_file`, `SSL_CTX_load_verify_locations`. Refer to [Security Guidance for using OpenSSL APIs/Macros](#security-guidance) for more detail |
ssl2.h | Yes | - |
ssl3.h | Yes | - |
sslerr.h | Yes | - |
stack.h | Yes | - |
store.h | Yes | - |
storeerr.h | Yes | - |
symhacks.h | Yes | - |
tls1.h | Partial | Heartbeats extension is disabled by default. |
ts.h | Yes | - |
tserr.h | Yes | - |
txt_db.h | Yes | - |
ui.h | No | Configured with no-ui-console. |
uierr.h | Yes | - |
whrlpool.h | No | Whirlpool is disabled by OE. |
x509.h | Partial | SCRYPT is disabled by OE. |
x509_vfy.h | Partial | Functions that are unsupported by OE for security concerns include: `X509_load_cert_file`, `X509_load_crl_file`, `X509_LOOKUP_hash_dir`, `X509_LOOKUP_file`, `X509_load_cert_crl_file`, `X509_STORE_load_locations`, `X509_STORE_set_default_paths`. Refer to [Security Guidance for using OpenSSL APIs/Macros](#security-guidance) for more detail. |
x509err.h | Yes | - |
x509v3.h | Partial | RFC3779 is disabled by OE. |
x509v3err.h | Yes | - |

### Additional headers in OpenSSL 3.1

Header | Supported | Comments |
:---:|:---:|:---|
bio.h | Partial | SCTP and Kernal TLS support are disabled by default. |
cmp.h | No | CMP (Certificate Management Protocol) is disabled by OE. |
cmp_util.h | No | CMP (Certificate Management Protocol) is disabled by OE.
cmperr.h | No | CMP (Certificate Management Protocol) is disabled by OE.
configuration.h | Yes | - |
conftypes.h | Yes | - |
core.h | Yes | - |
core_dispatch.h | Yes | - |
core_names.h | Yes | - |
core_object.h | Yes | - |
crmf.h | No | CRMF (Certificate Request Message Format) is disabled by OE. |
crmferr.h | No | CRMF (Certificate Request Message Format) is disabled by OE. |
cryptoerr_legacy.h | Partial | Some crypto components, including CMS and CT, are disabled by default or by OE. Refer to [Open Enclave Support for OpenSSL](#open-enclave-support-for-openssl) for a complete list. |
decoder.h | Yes | - |
decodererr.h | Yes | - |
encoder.h | Yes | - |
encodererr.h | Yes | - |
ess.h | Yes | - |
esserr.h | Yes | - |
fips_names.h | Yes | Note that fips provider is not enabled by OE. |
fipskey.h | Yes | Note that fips provider is not enabled by OE. |
http.h | Yes | - |
httperr.h | Yes | - |
macros.h | Yes | - |
param_build.h | Yes | - |
params.h | Yes | - |
prov_ssl.h | Yes | - |
proverr.h | Yes | - |
provider.h | Yes | - |
self_test.h | Yes | - |
ssl.h | Partial | SSL3 methods are disabled. Heartbeats extension is disabled by default. SSL Tracing is disabled. Functions that are unsupported by OE for security concerns include: `SSL_CTX_set_default_verify_paths`, `SSL_CTX_set_default_verify_dir`, `SSL_CTX_set_default_verify_file`, `SSL_CTX_load_verify_locations`. Refer to [Security Guidance for using OpenSSL APIs/Macros](#security-guidance) for more detail. |
sslerr_legacy.h | Yes | - |
trace.h | No | Tracing is disabled by default. |
types.h | Yes | - |
