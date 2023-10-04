# SGX1 with Flexible Launch Control (FLC) Prerequisites on Windows

## Intel Platform Software for Windows (PSW)

Intel PSW only needs to be manually installed if you are running a version of Windows client lower than 1709. It should be installed automatically with Windows Update on newer versions of Windows client and Windows Server 2019. You can check your version of Windows by running `winver` on the command line.

Ensure that you have the latest drivers on Windows 10 and Windows Server 2019 by checking for updates and installing all updates.

### Manual installation for client versions lower than 1709
To manually install Intel SGX PSW on Windows for client versions lower than 1709:

1. Download Intel SGX PSW from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md). For this example we will use Intel SGX PSW for Windows v2.12.100.4, but your commands should substitute the version with the version downloaded.

2. Unpack the self-extracting ZIP executable, and run the installer under `PSW_EXE_RS2_and_before`:

```cmd
"C:\Intel SGX PSW for Windows v2.12.100.4\PSW_EXE_RS2_and_before\Intel(R)_SGX_Windows_x64_PSW_2.12.100.4.exe"
```

### Manual installation for client versions 1709 and above
If you would like to manually update Intel PSW on Windows Server 2019 or Windows clients >= 1709 without relying on Windows Update, you can update the PSW components as follows:

1. Download devcon from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md) and install. Alternatively it is available as part of the [Windows Driver Kit for Windows 10](https://go.microsoft.com/fwlink/?linkid=2026156).
   -  Note that `devcon.exe` is usually installed to `C:\Program Files (x86)\Windows Kits\10\tools\x64`
   which is not in the `PATH` environment variable by default.

2. Download the Intel SGX PSW from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md).

3. Unpack the self-extracting ZIP executable. In an elevated command prompt, run the following command from the extracted PSW package under the `PSW_INF_RS3_and_above` folder:
  ```cmd
  devcon.exe update sgx_psw.inf "SWC\VEN_INT&DEV_0E0C"
  ```

### Verifying PSW installation
You can verify that the correct version of Intel SGX PSW is installed by using
Windows Explorer to open `C:\Windows\System32`. You should be able to find
file `sgx_urts.dll` if PSW is installed. Right click on `sgx_urts.dll`,
choose `Properties` and then find `Product version` on the `Details` tab.
The version should be `2.12.xxx.xxx` or above.

To verify that Intel SGX PSW is running, use the following command:

```cmd
sc query aesmservice
```

The state of the service should be "running" (4). If there are any errors, follow Intel's documentation for
troubleshooting.

If the AESM Service is stopped for any reason, it can be started by using the following command from Powershell.
```powershell
Start-Service "AESMService"
```

To restart the AESM Service, use the following Powershell command:
```powershell
Restart-Service "AESMService"
```

## Azure DCAP client for Windows [optional]

Note that this is optional since you can choose an alternate implementation of the DCAP client or create your own.
The Azure DCAP client for Windows is necessary if you would like to perform enclave attestation on a Azure Confidential Computing VM. The latest supported version can be found in [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md). Alternatively, other versions are available from [nuget.org](https://www.nuget.org/packages/Microsoft.Azure.DCAP/) and can be downloaded directly via the command below.

```cmd
nuget.exe install Microsoft.Azure.DCAP -ExcludeVersion -OutputDirectory C:\oe_prereqs
```

This example assumes you would like to download the package to `C:\oe_prereqs`. Complete the installation by following the instructions in the file `C:\oe_prereqs\Microsoft.Azure.DCAP\README.txt`.

Verify successful installation of Azure DCAP by ensuring that the file `dcap_quotprov.dll` is on the PATH and located in the `C:\Windows\System32` directory.

```cmd
C:\>where dcap_quoteprov.dll
C:\Windows\System32\dcap_quoteprov.dll
```

## Intel Data Center Attestation Primitives (DCAP) Libraries

Windows Server 2019 should have this package installed by default via Windows Update. In that case, it is only necessary to follow step #1 in [Install the Intel DCAP driver](#install-the-intel-dcap-driver) to allow the SGX Launch Configuration driver to run.

### Manual Installation
To manually install Intel DCAP on Windows, download Intel SGX DCAP from [Windows Open Enclave SDK Prerequisites](WindowsPrerequisites.md). For this example we will use Intel SGX DCAP for Windows v1.9.100.3, but your commands should substitute the version with the version downloaded.

Unpack the self-extracting ZIP executable, and it is recommended to refer to the *Intel SGX DCAP Windows SW Installation Guide.pdf* for more details on how to install the contents of the package. The following summary will assume that the contents were extracted to `C:\Intel SGX DCAP for Windows v1.9.100.3`:

### Install the Intel DCAP driver

1. Allow the SGX Launch Configuration driver (LC_driver) to run:
    - From an elevated command prompt:

      ```cmd
      reg add HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\sgx_lc_msr\Parameters /v "SGX_Launch_Config_Optin" /t REG_DWORD /d 1
      reg query HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\sgx_lc_msr\Parameters /v "SGX_Launch_Config_Optin"
      ```

    - If the driver is already installed and running, the machine will need to be rebooted for the change to take effect.

2. Install or update the drivers:
    - Refer to the PSW section above for notes on acquiring and using `devcon.exe`.
    - Please note that the following commands will be ran from the `C:\Intel SGX DCAP for Windows v1.9.100.3` folder.

    - On Windows Server 2019, the drivers can be manually updated using:

      ```cmd
      devcon.exe update base\WindowsServer2019_Windows10\sgx_base.inf *INT0E0C
      devcon.exe update dcap\WindowsServer2019_Windows10\sgx_dcap.inf "SWC\VEN_INT&DEV_0E0C_DCAP"
      ```
