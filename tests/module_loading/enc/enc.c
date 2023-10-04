// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/enclave.h>
#include <openenclave/internal/tests.h>
#include <stdio.h>

#include "module_loading_t.h"

int square(volatile int a);
__attribute__((weak)) int add_with_constant(volatile int a, volatile int b);
__attribute__((weak)) int sub(int a, int b);
int test_libc_symbols();

int is_enclave_init;

__attribute__((visibility("default"))) int debugger_test;
__attribute__((visibility("default"))) int is_module_init;

__attribute__((constructor)) void init_enclave()
{
    is_enclave_init = 1;
}

__attribute__((destructor)) void fini_enclave()
{
    notify_enclave_done();
}

__attribute__((visibility("default"))) void notify_module_done_wrapper()
{
    notify_module_done();
}

void enc_module_test()
{
    OE_TEST(square(8) == 64);

    OE_TEST(add_with_constant && add_with_constant(8, 7) == 515);

    OE_TEST(!sub);

    OE_TEST(is_enclave_init == 1);
    OE_TEST(is_module_init == 1);

    OE_TEST(test_libc_symbols() == 1);
}

OE_SET_ENCLAVE_SGX(
    1,    /* ProductID */
    1,    /* SecurityVersion */
    true, /* Debug */
    1024, /* NumHeapPages */
    64,   /* NumStackPages */
    2);   /* NumTCS */
