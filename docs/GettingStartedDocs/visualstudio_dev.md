Open Enclave Wizard - Preview
=============

This Visual Studio extension includes preview support for Trusted Execution Environment (TEE) platforms,
including ARM TrustZone and Intel SGX, with a Windows or Linux host application. In addition, this preview
includes support for testing your enclave under simulation when developing for SGX or TrustZone.

The following matrix shows what combinations are supported in this version:
```
OS       Platform           Mode         Supported?
-------  ----------------   -----------  ----------
Windows  Intel non-SGX      Simulation   Yes
Windows  SGX1               any          Yes
Windows  SGX1+FLC           any          Yes
Windows  OP-TEE/TrustZone   any          Not yet
Linux    Intel non-SGX      Simulation   Yes
Linux    SGX1               any          Not yet (in progress)
Linux    SGX1+FLC           any          Not yet (in progress)
Linux    OP-TEE/TrustZone   any          Not yet (in progress)
```

## Getting Started Guides
- [Developing Linux applications](VisualStudioLinux.md)
- [Developing Windows applications](VisualStudioWindows.md)
