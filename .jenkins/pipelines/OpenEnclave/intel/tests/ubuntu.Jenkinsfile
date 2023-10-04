// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

library "OpenEnclaveJenkinsLibrary@${params.OECI_LIB_VERSION}"

properties(
    [
        buildDiscarder(
            logRotator(
                artifactDaysToKeepStr: '90',
                artifactNumToKeepStr: '180',
                daysToKeepStr: '90',
                numToKeepStr: '180'
            )
        ),
        [$class: 'JobRestrictionProperty'],
        parameters(
            [
                string(name: "REPOSITORY", defaultValue: "openenclave/openenclave"),
                string(name: "BRANCH_NAME", defaultValue: "master"),
                string(name: "LOCAL_REPO_NAME_2004", description: "Intel SGX local repository for Ubuntu 20.04 (sgx_debian_local_repo.tgz)"),
                string(name: "OECI_LIB_VERSION", defaultValue: "master", description: 'Version of OE Libraries to use'),
                string(name: "OE_RELEASE_VERSION", description: "Open Enclave Release Version"),
                choice(name: "OE_PACKAGE", choices: ["open-enclave", "open-enclave-hostverify"], description: "Open Enclave package type to install"),
                choice(name: "RELEASE_SOURCE", choices: ["GitHub", "Azure"], description: "Source to download the OE Release from")
            ]
        )
    ]
)

parallel "Ubuntu 20.04 - Upgrade": { tests.TestIntelRCs(globalvars.AGENTS_LABELS["acc-ubuntu-20.04-us"], params.OE_RELEASE_VERSION, params.OE_PACKAGE, params.RELEASE_SOURCE, false, "", params.LOCAL_REPO_NAME_2004) },
         "Ubuntu 20.04 - Clean":   { tests.TestIntelRCs(globalvars.AGENTS_LABELS["acc-ubuntu-20.04-vanilla-us"], params.OE_RELEASE_VERSION, params.OE_PACKAGE, params.RELEASE_SOURCE, false, "", params.LOCAL_REPO_NAME_2004) }
