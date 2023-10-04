# Manually Installing Open Enclave Prerequisites for Windows on a System which supports SGX

## Platform requirements
- A system with support for SGX1 or SGX1 with Flexible Launch Control (FLC).

 Note: To check if your system has support for SGX1 with or without FLC, please look [here](../SGXSupportLevel.md).
 
- A version of Windows OS with native support for SGX features:
   - For server: Windows Server 2019
   - For client: Windows 10 64-bit version 1709 or newer
   - To check your Windows version, run `winver` on the command line.

## Software prerequisites
This guide will help you install Software prerequisites on Windows systems that support SGX.
> Note: see [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md) for a full list of current prerequisite versions, and download links.

The following will be installed as part of this guide:
- [Microsoft Visual Studio Build Tools 2019](#microsoft-visual-studio-build-tools-2019)
- [Git for Windows 64-bit](#git-for-windows-64-bit)
- [Clang/LLVM 10.0.0 for Windows 64-bit](#clang)
- [Python 3](#python-3)
- [ShellCheck 0.7.0](#shellcheck)
- [OpenSSL 1.1.1](#openssl)
- [cmake format](#cmake-format)

> Note: the list above does not contain the additional prerequisites specific to SGX that will need to be installed. See [Platform requirements](#platform-requirements) to determine your system type and [Prerequisites specific to SGX support on your system](#prerequisites-specific-to-sgx-support-on-your-system) to install the additional prerequisites for your system.

## Prerequisites specific to SGX support on your system

For systems with support for SGX1  - [Intel PSW, Intel Enclave Common API library](WindowsManualSGX1Prereqs.md)

For systems with support for SGX1 + FLC - [Intel PSW, Intel Data Center Attestation Primitives, and related dependencies](WindowsManualSGX1FLCDCAPPrereqs.md)

## Microsoft Visual Studio Build Tools 2019
Download Visual Studio Build Tools 2019 from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md). Choose the "C++ build tools" workload. Visual Studio Build Tools 2019 has support for CMake Version 3.15 (CMake ver 3.12 or above is required for building Open Enclave SDK). For more information about CMake support, look [here](https://blogs.msdn.microsoft.com/vcblog/2016/10/05/cmake-support-in-visual-studio/).

## Git for Windows 64-bit

Install Git and add Git Bash to the PATH environment variable.
Typically, Git Bash is located in `C:\Program Files\Git\bin`.
Currently the Open Enclave SDK build system uses bash scripts to configure
and build Linux-based 3rd-party libraries.

Open a command prompt and ensure that Git Bash is added to PATH.

```cmd
C:\>where bash
C:\Program Files\Git\bin\bash.exe
```

Tools available in the Git bash environment are also used for test and sample
builds. It is also useful to have the `Git\mingw64\bin` folder added to PATH.

## Clang

 Open Enclave SDK uses Clang to build the enclave binaries. Follow these steps to install Clang:

1. Download Clang from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md). 
2. Install Clang and add the LLVM folder (typically C:\Program Files\LLVM\bin)
to PATH.
4. Open up a command prompt and ensure that clang is added to PATH.

```cmd
C:\> where clang
C:\Program Files\LLVM\bin\clang.exe
C:\> where llvm-ar
C:\Program Files\LLVM\bin\llvm-ar.exe
C:\> where ld.lld
C:\Program Files\LLVM\bin\ld.lld.exe
```

## Python 3

Install [Python 3 for Windows](https://www.python.org/downloads/windows/) and ensure that python.exe is available in your PATH.
Make sure the checkbox for PIP is checked when installing.

Python 3 is used as part of the mbedtls tests and for cmake-format.

## ShellCheck

[ShellCheck](https://www.shellcheck.net/) is used to check the format of shell scripts. Download ShellCheck from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md). 

For example, [ShellCheck v0.7.0](https://oejenkins.blob.core.windows.net/oejenkins/shellcheck-v0.7.0.zip) can be installed as follows.

Download and extract the ShellCheck zip file. Inside it there is shellcheck-v0.7.0.exe which must be copied to a directory in your PATH and renamed to shellcheck.exe.

## OpenSSL

Download and install OpenSSL for Windows from our [direct download link here](https://oejenkins.blob.core.windows.net/oejenkins/openssl-1.1.1-latest.nupkg) which is distributed under the [dual OpenSSL and SSLeay license](https://www.openssl.org/source/license-openssl-ssleay.txt) without further legal obligations.

Alternatively, you can obtain OpenSSL at one of the [locations listed in the OpenSSL wiki](https://wiki.openssl.org/index.php/Binaries), or build and install it from source.

After installing OpenSSL, add `openssl` to your `PATH`.

```cmd
C:\Users\test> where openssl
C:\oe_prereqs\OpenSSL\x64\release\bin\openssl.exe
```

## cmake format

Install `cmake-format` as follows.

```cmd
pip install cmake_format
```

Open up a command prompt and ensure that `cmake-format` is added to the `PATH`.

```cmd
C:\Users\test> where cmake-format
C:\Users\test\AppData\Local\Programs\Python\Python37-32\Scripts\cmake-format.exe
```
