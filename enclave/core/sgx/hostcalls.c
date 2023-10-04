// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/edger8r/enclave.h>
#include <openenclave/enclave.h>
#include <openenclave/internal/safecrt.h>
#include <openenclave/internal/safemath.h>
#include <openenclave/internal/sgx/ecall_context.h>
#include <openenclave/internal/sgx/td.h>
#include "td.h"

/**
 * Validate and fetch this thread's ecall context.
 */
static oe_ecall_context_t* _get_ecall_context()
{
    oe_sgx_td_t* td = oe_sgx_get_td();
    /* __oe_handle_main has already validated the alignment of ecall_context
     * for mitigating the xAPIC vulnerability */
    return td->host_ecall_context;
}

/**
 * Fetch the ocall_args field if an ecall context has been passed in.
 */
oe_call_host_function_args_t* oe_ecall_context_get_ocall_args()
{
    oe_ecall_context_t* ecall_context = _get_ecall_context();
    return ecall_context ? &ecall_context->ocall_args : NULL;
}

/**
 * Get the ecall context's buffer if it is of an equal or larger size than the
 * given size.
 */
void* oe_ecall_context_get_ocall_buffer(uint64_t size)
{
    oe_ecall_context_t* ecall_context = _get_ecall_context();
    if (ecall_context)
    {
        /* ecall_context is 16-byte aligned. Thus the fields ocall_buffer and
         * and ocall_buffer_size are guaranteed to be 8-byte aligned due to
         * their statically determined offsets (for xAPIC vulnerability
         * mitigation). Also, copy to volatile variables to prevent TOCTOU
         * attacks. */
        uint8_t* ocall_buffer = (uint8_t*)ecall_context->ocall_buffer;
        uint64_t ocall_buffer_size = ecall_context->ocall_buffer_size;

        /* Validate the ocall_buffer and ocall_buffer_size */
        if (ocall_buffer_size >= size &&
            oe_is_outside_enclave(ocall_buffer, ocall_buffer_size) &&
            ((uint64_t)ocall_buffer % 8) == 0 && (ocall_buffer_size % 8) == 0)
            return (void*)ocall_buffer;
    }
    return NULL;
}

void* oe_host_calloc(size_t nmemb, size_t size)
{
    size_t total_size;
    if (oe_safe_mul_sizet(nmemb, size, &total_size) != OE_OK)
        return NULL;

    void* ptr = oe_host_malloc(total_size);

    if (ptr)
        oe_memset_s_with_barrier(ptr, nmemb * size, 0, nmemb * size);

    return ptr;
}

char* oe_host_strndup(const char* str, size_t n)
{
    char* p;
    size_t len;

    if (!str)
        return NULL;

    len = oe_strlen(str);

    if (n < len)
        len = n;

    /* Would be an integer overflow in the next statement. */
    if (len == OE_SIZE_MAX)
        return NULL;

    if (!(p = oe_host_malloc(len + 1)))
        return NULL;

    if (oe_memcpy_s_with_barrier(p, len + 1, str, len) != OE_OK)
        return NULL;

    OE_WRITE_VALUE_WITH_BARRIER((void*)&p[len], (char)'\0');

    return p;
}

// Function used by oeedger8r for allocating ocall buffers.
void* oe_allocate_ocall_buffer(size_t size)
{
    // Fetch the ecall context's ocall buffer if it is equal to or larger than
    // given size. Use it if available.
    void* buffer = oe_ecall_context_get_ocall_buffer(size);
    if (buffer)
    {
        return buffer;
    }

    // Perform host allocation by making an ocall.
    return oe_host_malloc(size);
}

// Function used by oeedger8r for freeing ocall buffers.
void oe_free_ocall_buffer(void* buffer)
{
    oe_ecall_context_t* ecall_context = _get_ecall_context();

    // ecall context's buffer is managed by the host and does not have to be
    // freed.
    if (ecall_context && buffer == ecall_context->ocall_buffer)
        return;

    // Even though ecall_context is memory controlled by the host, there
    // is nothing the host can exploit to disclose information or modify
    // behavior of the enclave to do something insecure. Even still, this
    // analysis depends on the implementation of oe_host_free. For additional
    // safety, ensure host cannot bypass the above check via speculative
    // execution.
    oe_lfence();

    oe_host_free(buffer);
}

void* oe_allocate_arena(size_t capacity)
{
    return oe_host_malloc(capacity);
}

void oe_deallocate_arena(void* buffer)
{
    oe_host_free(buffer);
}
