# Build and Sign an Enclave

## Build options for using Open Enclave SDK libraries

As with the Intel SGX SDK, Open Enclave SDK currently only supports building single-binary enclaves.
Also like the Intel SGX SDK, these binaries must be built with a specific set of includes and build flags.

To simplify the process of the specifying the correct build parameters, Open Enclave SDK provides
a set of `pkg-config` settings that you can use in your build scripts.

There are 8 configurations provided to handle all permutations of the following parameters:
* Building enclave or host binary
* Using GCC or Clang build tools
* Compiling C or C++ code

The provided configurations use the following naming syntax:

```
oe<enclave|host>-<gcc|g++|clang|clang++>
```

For example, if you have added the Open Enclave SDK `pkgconfig` to your `PKG_CONFIG_PATH`,
you can specify in your Makefile how to build your C enclave using Clang-8:

```make
CFLAGS=$(shell pkg-config oeenclave-clang --cflags)
LDFLAGS=$(shell pkg-config oeenclave-clang --libs)

$(CC) -c $(CFLAGS) my_enclave.c -o my_enclave.o
$(CC) -o my_enclave my_enclave.o $(LDFLAGS)
```

You can also display in the shell what the options are by running `pkg-config`
directly. For example, to see the host linker options when building C++ code with GCC:

```bash
pkg-config opt/openenclave/share/pkgconfig/oehost-g++.pc --libs
```
## Build options for using Open Enclave SDK with a CMake project
If you have a CMake project and would like to bring in Open Enclave targets,
see the [Open Enclave SDK CMake Package instructions](https://github.com/openenclave/openenclave/blob/master/cmake/sdk_cmake_targets_readme.md).

## Signing an SGX Enclave

Before an SGX enclave can be run, the properties that define how the enclave should
be loaded need to be specified for the enclave. These properties, along with the
signing key, define the enclave identity that is used for attestation and sealing
operations.

In the Open Enclave SDK, these properties can be attached to the enclave as part
of the signing process. To do so, you will need to use the oesign tool, which
takes the following parameters:

```bash
Usage: oesign sign --enclave-image ENCLAVE --config-file CONFFILE --key-file KEYFILE
```

For example, to sign the helloworld sample enclave in the output folder:
```bash
/opt/openenclave/bin/oesign sign --enclave-image helloworld_enc --config-file enc.conf --key-file private.pem
```

**When signing the enclave, the `KEYFILE` specified must contain a 3072-bit RSA keys
with exponent 3.**

To generate your own private keypair yourself, you can install the OpenSSL package and run:

```bash
openssl genrsa -out myprivate.pem -3 3072
```

The `CONFFILE` is a simple text file that defines enclave settings.
All the settings must be provided for the enclave to be successfully loaded:

- **Debug**: Is the enclave allowed to load in debug mode?
- **NumTCS**: The number of thread control structures (TCS) to allocate in the enclave.
  This determines the maximum number of concurrent threads that can be executing in the enclave.
- **NumStackPages**: The number of stack pages to allocate for each thread in the enclave.
- **NumHeapPages**: The number of pages to allocate for the enclave to use as heap memory.

All these properties will also be reflected in the UniqueID (MRENCLAVE) of the resulting enclave.
In addition, the following two properties are defined by the developer and map directly to the following SGX identity properties:

- **ProductID**: The product identity (ISVPRODID) for the developer to distinguish
  between different enclaves signed with the same MRSIGNER value.
- **SecurityVersion**: The security version number for the enclave (ISVSVN), which
  can be used to prevent rollback attacks against sealing keys. This value should be
  incremented whenever a security fix is made to the enclave code.

Optionally, to create a 0-based enclave (currently only available in SGX on Linux, with PSW version 2.14.1 or above) the following setting can be provided:

- **CreateZeroBaseEnclave**: Should the enclave be created with base address 0x0? Defaults to 0.
- **StartAddress**: When the enclave base address is 0x0 (CreateZeroBaseEnclave=1), the enclave image will be created at this address. The value needs to be aligned to OE_PAGE_SIZE (0x1000) and greater than mmap min address (/proc/sys/vm/mmap_min_addr).

0-based enclaves guarantee NullPointerException behavior when 0-page is accessed. Applications that depend on this behavior can now be run inside an enclave (example, .NET runtime).

Here is the example from helloworld.conf used in the helloworld sample:
```
# Enclave settings:
ProductID=1
SecurityVersion=1
Debug=1
NumHeapPages=1024
NumStackPages=1024
NumTCS=1
```
Additionally, a developer can specify additional Key Sharing and Separation (KSS) identity properties
for use on the platforms that support it (for SGX enclave only):
- **FamilyID**: The product family identity (ISVFAMILYID for SGX) a developer can specify to group
different enclaves under a common identity such as an identifier for the application suite
which includes several enclave apps.
- **ExtendedProductID**: The extended product identity (ISVEXTPRODID for SGX) value that a developer
can use as 128-bit globally unique identifier for the enclave where the 16-bit ProductID proves too restrictive.
For more details, see Table 37-19 of the
[Intel's Software Developers Manual](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html#combined/).

For example, in kss_valid.conf:
```
FamilyID=47183823-2574-4bfd-b411-99ed177d3e43
ExtendedProductID=2768c720-1e28-11eb-adc1-0242ac120002
```

On SGX2 machines, developers can also specify whether to handle in-enclave exceptions or not using `CapturePFGPExceptions` field.
- **CapturePFGPExceptions**: Whether in-enclave exception handler should be enabled (1) or not (0) to capture #PF and #GP exceptions (SGX2 feature, default value: 0)

Also as a convenience, developers can specify the enclave properties in code using the `OE_SET_ENCLAVE_SGX2` macro to leverage SGX2 properties and enable KSS using `RequireKSS` field in the macro. Note that to set `FamilyID` and `ExtendedProductID` through `OE_SET_ENCLAVE_SGX2` macro, the corresponding fields should be enclosed in the parenthesis as shown below. For example, the equivalent properties could be defined in any .c or .cpp file compiled into the enclave:

```c
OE_SET_ENCLAVE_SGX2(
    1,     /* ProductID */
    1,     /* SecurityVersion */
    ({0x47, 0x18, 0x38, 0x23, 0x25, 0x74, 0x4b, 0xfd, 0xb4, 0x11, 0x99, 0xed, 0x17, 0x7d, 0x3e, 0x43}),   /* ExtendedProductID */
    ({0x27, 0x68, 0xc7, 0x20, 0x1e, 0x28, 0x11, 0xeb, 0xad, 0xc1, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02}),   /* FamilyID */
    true,  /* Debug */
    true,  /* CapturePFGPExceptions */
    true   /* RequireKSS */
    false, /* CreateZeroBaseEnclave */
    0,     /* StartAddress */
    1024,  /* NumHeapPages */
    1024,  /* NumStackPages */
    1);    /* NumTCS */
```

You can also specify the enclave properties in code using the
`OE_SET_ENCLAVE_SGX` macro if no KSS properties needed.  For example, the equivalent properties could be
defined in any .c or .cpp file compiled into the enclave:

```c
OE_SET_ENCLAVE_SGX(
    1,    /* ProductID */
    1,    /* SecurityVersion */
    1,    /* Debug */
    1024, /* NumHeapPages: heap size in units of 4KB pages */
    1024, /* NumStackPages: stack size, in units of 4KB pages */
    1);   /* NumTCS */
```

Specifying the enclave properties using the `OE_SET_ENCLAVE_SGX` also allows you
to run an enclave in debug mode without signing it first. In this case, the enclave
is treated as having the standard signer ID (MRSIGNER) value of:

> CA9AD7331448980AA28890CE73E433638377F179AB4456B2FE237193193A8D0A

**Since enclaves that run in debug mode are not confidential, you should disable
the ability to run the enclave in debug mode before deploying it into production.**

For example, to toggle an enclave to disable debug mode when signed, you could
specify a `CONFFILE` that only changed that property:

```
# Enclave settings:
Debug=0
```

## Signing an OP-TEE Enclave

The `oesign` tool is currently SGX-only.  For OP-TEE enclaves, signing
is instead performed by the `sign.py` script that comes with OP-TEE.
The signing key
and script, among other artifacts, are exported to a "TA Dev Kit" during
OP-TEE's build process. The Open Enclave SDK takes a `OE_TA_DEV_KIT_DIR`
CMake parameter at
build time that specifies where to find the TA Dev Kit. This enables
the `add_enclave` CMake function to locate sign.py and the TA signing key
and apply them automatically to every TA built as part of the build process.

There is currently no equivalent of a .conf file for OP-TEE, so all settings
must be specified using the `OE_SET_ENCLAVE_OPTEE` macro:

```c
#define TA_UUID                                            \
    { /* 126830b9-eb9f-412a-89a7-bcc8a517c12e */           \
        0x126830b9, 0xeb9f, 0x412a,                        \
        {                                                  \
            0x89, 0xa7, 0xbc, 0xc8, 0xa5, 0x17, 0xc1, 0x2e \
        }                                                  \
    }

OE_SET_ENCLAVE_OPTEE(
    TA_UUID,               /* UUID */
    4 * 1024 * 1024,       /* Heap size, in bytes */
    4 * 1024,              /* Stack size, in bytes */
    TA_FLAG_MULTI_SESSION, /* Flags */
    "1.0.0",               /* Version */
    "Sample enclave");     /* Description */
```

The flags property can contain any of the flags defined in the
`include/user_ta_header.h` file in the TA Dev Kit:

| Flag                        | Meaning                                     |
| :-------------------------- | :------------------------------------------ |
| TA_FLAG_SINGLE_INSTANCE     | all host apps use the same enclave instance |
| TA_FLAG_MULTI_SESSION       | allow multiple sessions from host apps      |
| TA_FLAG_INSTANCE_KEEP_ALIVE | keep enclave running after sessions end     |
| TA_FLAG_SECURE_DATA_PATH    | accesses SDP memory                         |
| TA_FLAG_CACHE_MAINTENANCE   | use cache flush syscall                     |

For more details, see sections 2.1.6 and 4.5 of the
[GlobalPlatform Internal Core API Specification v1.2.1](https://globalplatform.org/specs-library/tee-internal-core-api-specification-v1-2/).
