// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifndef _OE_HOST_EXCEPTION_H
#define _OE_HOST_EXCEPTION_H

#include <openenclave/bits/exception.h>
#include <openenclave/internal/calls.h>

/* Compatiblie with OE_EXCEPTION_CONTINUE_EXECUTION */
#define OE_SGX_EXCEPTION_ENCLAVE_HANDLED 0xFFFFFFFF
/* Compatible with OE_EXCEPTION_CONTINUE_SEARCH */
#define OE_SGX_EXCEPTION_ENCLAVE_NOT_HANDLED 0x0
#define OE_SGX_EXCEPTION_HOST 0x1

typedef struct _host_exception_context
{
    uint64_t rax;
    uint64_t rbx;
    uint64_t rip;
    uint64_t signal_number;
    uint64_t faulting_address;
} oe_host_exception_context_t;

/* Initialize the exception processing. */
void oe_initialize_host_exception(void);

/* Platform neutral exception handler */
uint64_t oe_host_handle_exception(oe_host_exception_context_t* context);

#endif // _OE_HOST_EXCEPTION_H
