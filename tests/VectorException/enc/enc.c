// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/bits/sgx/sgxtypes.h>
#include <openenclave/edger8r/enclave.h>
#include <openenclave/enclave.h>
#include <openenclave/internal/calls.h>
#include <openenclave/internal/fault.h>
#include <openenclave/internal/globals.h>
#include <openenclave/internal/jump.h>
#include <openenclave/internal/print.h>
#include <openenclave/internal/sgx/td.h>
#include <openenclave/internal/tests.h>
#include <openenclave/internal/trace.h>
#include "VectorException_t.h"

#include <stdlib.h>
#include "exception_handler_stack.h"

#define OE_GETSEC_OPCODE 0x370F
#define OE_GETSEC_CAPABILITIES 0x00

static void* _stack;
static uint64_t _stack_size;

static void* _exception_handler_stack;
static uint64_t _exception_handler_stack_size;

static int _check_exception_handler_stack;
static int _use_exception_handler_stack;

void get_stack(void** stack, uint64_t* stack_size)
{
    oe_sgx_td_t* td = oe_sgx_get_td();
    void* tcs = td_to_tcs(td);
    *stack_size = STACK_SIZE;
    *stack = (void*)((uint64_t)tcs - PAGE_SIZE - STACK_SIZE);
}

int initialize_exception_handler_stack(
    void** stack,
    uint64_t* stack_size,
    uint64_t exception_type,
    int register_exception_type)
{
    oe_sgx_td_t* td = oe_sgx_get_td();

    *stack_size = EXCEPTION_HANDLER_STACK_SIZE;
    *stack = memalign(PAGE_SIZE, *stack_size);

    if (!*stack)
        return -1;

    if (!oe_sgx_td_set_exception_handler_stack(td, *stack, *stack_size))
        return -1;

    oe_host_printf(
        "set exception handler stack [0x%lx, 0x%lx]\n",
        (uint64_t)*stack,
        (uint64_t)*stack + *stack_size);

    if (register_exception_type)
    {
        if (!oe_sgx_td_register_exception_handler_stack(td, exception_type))
            return -1;
    }

    return 0;
}

void cleaup_exception_handler_stack(void** stack, uint64_t* stack_size)
{
    oe_sgx_td_t* td = oe_sgx_get_td();

    oe_sgx_td_set_exception_handler_stack(td, NULL, 0);
    free(*stack);

    *stack = NULL;
    *stack_size = 0;
}

// This function will generate the divide by zero function.
// The handler will catch this exception and fix it, and continue execute.
// It will return 0 if success.
int divide_by_zero_exception_function(void)
{
    // Making ret, f and d volatile to prevent optimization
    volatile int ret = 1;
    volatile float f = 0;
    volatile double d = 0;

    f = 0.31f;
    d = 0.32;

    // Using inline assembly for idiv to prevent it being optimized out
    // completely. Specify edi as the used register to ensure that 32-bit
    // division is done. 64-bit division generates a 3 byte instruction rather
    // than 2 bytes.
    register int edi __asm__("edi") = 0;
    asm volatile("idiv %1"
                 : "=a"(ret)
                 : "r"(edi) // Divisor of 0 is hard-coded
                 : "%1",
                   "cc"); // cc indicates that flags will be clobbered by ASM

    // Check if the float registers are recovered correctly after the exception
    // is handled.
    if (f < 0.309 || f > 0.321 || d < 0.319 || d > 0.321)
    {
        return -1;
    }

    return 0;
}

uint64_t test_divide_by_zero_handler(oe_exception_record_t* exception_record)
{
    if (exception_record->code != OE_EXCEPTION_DIVIDE_BY_ZERO)
    {
        return OE_EXCEPTION_CONTINUE_SEARCH;
    }

    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));

    uint64_t stack;
    uint64_t stack_size;

    if (_check_exception_handler_stack)
    {
        stack = (uint64_t)_exception_handler_stack;
        stack_size = _exception_handler_stack_size;
    }
    else
    {
        stack = (uint64_t)_stack;
        stack_size = _stack_size;
    }

    oe_host_printf(
        "Check rsp (0x%lx) against stack [0x%lx, 0x%lx]\n",
        rsp,
        stack,
        stack + stack_size);

    if (rsp < stack || rsp > stack + stack_size)
        return OE_EXCEPTION_ABORT_EXECUTION;

    // Skip the idiv instruction - 2 is tied to the size of the idiv instruction
    // and can change with a different compiler/build. Minimizing this with the
    // use of the inline assembly for integer division
    exception_record->context->rip += 2;
    return OE_EXCEPTION_CONTINUE_EXECUTION;
}

#define MAX_EXCEPTION_HANDLER_COUNT 64

#define PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_) \
    uint64_t __exception_handler_name_(                          \
        oe_exception_record_t* exception_record)                 \
    {                                                            \
        OE_UNUSED(exception_record);                             \
        return OE_EXCEPTION_CONTINUE_SEARCH;                     \
    }

#define TEN_PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_) \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_0)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_1)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_2)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_3)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_4)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_5)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_6)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_7)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_8)     \
    PASSTHROUGH_EXCEPTION_HANDLER(__exception_handler_name_prefix_##_9)

// Define 64 pass through exception handlers.
TEN_PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler0)
TEN_PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler1)
TEN_PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler2)
TEN_PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler3)
TEN_PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler4)
TEN_PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler5)
PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler6_0)
PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler6_2)
PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler6_1)
PASSTHROUGH_EXCEPTION_HANDLER(TestPassThroughHandler6_3)

#define TEN_EXCEPTION_HANDLER_POINTERS(__exception_handler_name_prefix_) \
    __exception_handler_name_prefix_##_0,                                \
        __exception_handler_name_prefix_##_1,                            \
        __exception_handler_name_prefix_##_2,                            \
        __exception_handler_name_prefix_##_3,                            \
        __exception_handler_name_prefix_##_4,                            \
        __exception_handler_name_prefix_##_5,                            \
        __exception_handler_name_prefix_##_6,                            \
        __exception_handler_name_prefix_##_7,                            \
        __exception_handler_name_prefix_##_8,                            \
        __exception_handler_name_prefix_##_9,

static oe_vectored_exception_handler_t
    g_test_pass_through_handlers[MAX_EXCEPTION_HANDLER_COUNT] = {
        TEN_EXCEPTION_HANDLER_POINTERS(TestPassThroughHandler0)
            TEN_EXCEPTION_HANDLER_POINTERS(TestPassThroughHandler1)
                TEN_EXCEPTION_HANDLER_POINTERS(TestPassThroughHandler2)
                    TEN_EXCEPTION_HANDLER_POINTERS(TestPassThroughHandler3)
                        TEN_EXCEPTION_HANDLER_POINTERS(TestPassThroughHandler4)
                            TEN_EXCEPTION_HANDLER_POINTERS(
                                TestPassThroughHandler5)
                                TestPassThroughHandler6_0,
        TestPassThroughHandler6_1,
        TestPassThroughHandler6_2,
        TestPassThroughHandler6_3};

static oe_vectored_exception_handler_t g_test_div_by_zero_handler;

int vector_exception_setup()
{
    oe_result_t result;

    // Add one exception handler.
    result =
        oe_add_vectored_exception_handler(false, test_divide_by_zero_handler);
    if (result != OE_OK)
    {
        return -1;
    }

    // Remove the exception handler.
    if (oe_remove_vectored_exception_handler(test_divide_by_zero_handler) !=
        OE_OK)
    {
        return -1;
    }

    // Insert the exception handler to the front.
    result =
        oe_add_vectored_exception_handler(true, test_divide_by_zero_handler);
    if (result != OE_OK)
    {
        return -1;
    }

    // Remove the exception handler.
    if (oe_remove_vectored_exception_handler(test_divide_by_zero_handler) !=
        OE_OK)
    {
        return -1;
    }

    // Append one by one till reach the max.
    for (uint32_t i = 0; i < OE_COUNTOF(g_test_pass_through_handlers); i++)
    {
        result = oe_add_vectored_exception_handler(
            false, g_test_pass_through_handlers[i]);
        if (result != OE_OK)
        {
            return -1;
        }
    }

    // Can't add one more.
    result =
        oe_add_vectored_exception_handler(false, test_divide_by_zero_handler);
    if (result == OE_OK)
    {
        return -1;
    }

    // Remove all registered handlers.
    for (uint32_t i = 0; i < OE_COUNTOF(g_test_pass_through_handlers); i++)
    {
        if (oe_remove_vectored_exception_handler(
                g_test_pass_through_handlers[i]) != OE_OK)
        {
            return -1;
        }
    }

    // Add handles to the front one by one till reach the max.
    for (uint32_t i = 0; i < OE_COUNTOF(g_test_pass_through_handlers); i++)
    {
        result = oe_add_vectored_exception_handler(
            true, g_test_pass_through_handlers[i]);
        if (result != OE_OK)
        {
            return -1;
        }
    }

    // Can't add one more.
    result =
        oe_add_vectored_exception_handler(true, test_divide_by_zero_handler);
    if (result == OE_OK)
    {
        return -1;
    }

    // Remove all registered handlers.
    for (uint32_t i = 0; i < OE_COUNTOF(g_test_pass_through_handlers); i++)
    {
        if (oe_remove_vectored_exception_handler(
                g_test_pass_through_handlers[i]) != OE_OK)
        {
            return -1;
        }
    }

    // Add the test pass through handlers.
    for (uint32_t i = 0; i < OE_COUNTOF(g_test_pass_through_handlers) - 1; i++)
    {
        result = oe_add_vectored_exception_handler(
            false, g_test_pass_through_handlers[i]);
        if (result != OE_OK)
        {
            return -1;
        }
    }

    // Add the real handler to the end.
    g_test_div_by_zero_handler = test_divide_by_zero_handler;
    result =
        oe_add_vectored_exception_handler(false, test_divide_by_zero_handler);
    if (result != OE_OK)
    {
        return -1;
    }

    return 0;
}

int vector_exception_cleanup()
{
    // Remove all handlers.
    if (oe_remove_vectored_exception_handler(g_test_div_by_zero_handler) !=
        OE_OK)
    {
        return -1;
    }

    for (uint32_t i = 0; i < OE_COUNTOF(g_test_pass_through_handlers) - 1; i++)
    {
        if (oe_remove_vectored_exception_handler(
                g_test_pass_through_handlers[i]) != OE_OK)
        {
            return -1;
        }
    }

    return 0;
}

int enc_test_vector_exception(
    int use_exception_handler_stack,
    int register_exception_type)
{
    if (vector_exception_setup() != 0)
    {
        return -1;
    }

    oe_host_printf(
        "enc_test_vector_exception: will generate a hardware exception inside "
        "enclave!\n");

    _check_exception_handler_stack = 0;

    get_stack(&_stack, &_stack_size);

    if (use_exception_handler_stack)
    {
        OE_TEST(
            initialize_exception_handler_stack(
                &_exception_handler_stack,
                &_exception_handler_stack_size,
                OE_EXCEPTION_DIVIDE_BY_ZERO,
                register_exception_type) == 0);

        if (register_exception_type)
            _check_exception_handler_stack = 1;
    }

    if (divide_by_zero_exception_function() != 0)
    {
        return -1;
    }

    oe_host_printf("enc_test_vector_exception: hardware exception is handled "
                   "correctly!\n");

    if (vector_exception_cleanup() != 0)
    {
        return -1;
    }

    if (use_exception_handler_stack)
        cleaup_exception_handler_stack(
            &_exception_handler_stack, &_exception_handler_stack_size);

    return 0;
}

void call_invalid_instruction()
{
    asm volatile("ud2;");
}

uint64_t test_sigill_handler_with_ocall(oe_exception_record_t* exception_record)
{
    if (exception_record->code != OE_EXCEPTION_ILLEGAL_INSTRUCTION)
    {
        return OE_EXCEPTION_CONTINUE_SEARCH;
    }

    host_set_was_ocall_called();

    // Skip the ud2 instruction
    exception_record->context->rip += 2;
    return OE_EXCEPTION_CONTINUE_EXECUTION;
}

int enc_test_ocall_in_handler(
    int use_exception_handler_stack,
    int register_exception_type)
{
    long long rbx;
    long long rbp;
    long long rsp;
    long long r12;
    long long r13;
    long long r14;

    oe_result_t result = oe_add_vectored_exception_handler(
        false, test_sigill_handler_with_ocall);
    if (result != OE_OK)
    {
        return -1;
    }

    _check_exception_handler_stack = 0;

    get_stack(&_stack, &_stack_size);

    if (use_exception_handler_stack)
    {
        OE_TEST(
            initialize_exception_handler_stack(
                &_exception_handler_stack,
                &_exception_handler_stack_size,
                OE_EXCEPTION_DIVIDE_BY_ZERO,
                register_exception_type) == 0);

        if (register_exception_type)
            _check_exception_handler_stack = 1;
    }

    oe_host_printf(
        "enc_test_ocall_in_handler: will generate a hardware exception inside "
        "enclave!\n");

    // Save callee-saved registers
    // Memory is used rather than registers for storing these values to ensure
    // the below asm doesn't clobber a register before it is saved.
    asm volatile(
        "mov %%rbx, %0;"
        "mov %%rbp, %1;"
        "mov %%rsp, %2;"
        "mov %%r12, %3;"
        "mov %%r13, %4;"
        "mov %%r14, %5;"
        : "=m"(rbx), "=m"(rbp), "=m"(rsp), "=m"(r12), "=m"(r13), "=m"(r14));

    call_invalid_instruction();

    // Ensure callee-saved registers are properly restored
    long long after_rbx;
    long long after_rbp;
    long long after_rsp;
    long long after_r12;
    long long after_r13;
    long long after_r14;

    // Memory is used rather than registers for storing these values to ensure
    // the below asm doesn't clobber a register before it is saved.
    asm volatile("mov %%rbx, %0;"
                 "mov %%rbp, %1;"
                 "mov %%rsp, %2;"
                 "mov %%r12, %3;"
                 "mov %%r13, %4;"
                 "mov %%r14, %5;"
                 : "=m"(after_rbx),
                   "=m"(after_rbp),
                   "=m"(after_rsp),
                   "=m"(after_r12),
                   "=m"(after_r13),
                   "=m"(after_r14));

    OE_TEST(rbx == after_rbx);
    OE_TEST(rbp == after_rbp);
    OE_TEST(rsp == after_rsp);
    OE_TEST(r12 == after_r12);
    OE_TEST(r13 == after_r13);
    OE_TEST(r14 == after_r14);

    oe_host_printf("enc_test_ocall_in_handler: hardware exception is handled "
                   "correctly!\n");

    if (oe_remove_vectored_exception_handler(test_sigill_handler_with_ocall) !=
        OE_OK)
    {
        return -1;
    }

    if (use_exception_handler_stack)
        cleaup_exception_handler_stack(
            &_exception_handler_stack, &_exception_handler_stack_size);

    return 0;
}

static bool test_getsec_instruction()
{
    // Arbitrary constants to verify r1/r2 have not been clobbered
    const uint32_t c_r1 = 0xDEADBEEF;
    const uint32_t c_r2 = 0xBEEFCAFE;

    uint32_t r1 = c_r1;
    uint32_t r2 = c_r2;

    // Invoke GETSEC instruction (illegal in SGX) on CAPABILITIES leaf
    asm volatile("mov %0, %%rax\n\t" /* GETSEC */
                 "mov %1, %%rbx\n\t" /* reserved 1 */
                 "mov %2, %%rcx\n\t" /* reserved 2 */
                 "GETSEC\n\t"
                 :
                 : "i"(OE_GETSEC_CAPABILITIES), "m"(r1), "m"(r2)
                 : "rax", "rbx", "rcx");

    // Verify that unused variables are untouched on continue
    if (r1 != c_r1 || r2 != c_r2)
    {
        return false;
    }

    return true;
}

static void get_cpuid(
    unsigned int leaf,
    unsigned int subleaf,
    unsigned int* eax,
    unsigned int* ebx,
    unsigned int* ecx,
    unsigned int* edx)
{
    asm volatile("cpuid"
                 // CPU id instruction returns values in the following registers
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 // __leaf is passed in eax (0) and __subleaf in ecx (2)
                 : "0"(leaf), "2"(subleaf));
}

static uint64_t test_nested_exception_handler(
    oe_exception_record_t* exception_record)
{
    uint64_t rsp;
    oe_sgx_td_t* td = oe_sgx_get_td();

    asm volatile("mov %%rsp, %0" : "=r"(rsp));

    if (exception_record->code == OE_EXCEPTION_DIVIDE_BY_ZERO)
    {
        if (_check_exception_handler_stack)
            OE_TEST(oe_sgx_td_exception_handler_stack_registered(
                td, OE_EXCEPTION_DIVIDE_BY_ZERO));

        if (_check_exception_handler_stack)
            OE_TEST(
                rsp > (uint64_t)_exception_handler_stack &&
                rsp < (uint64_t)_exception_handler_stack +
                          _exception_handler_stack_size);
        else
            OE_TEST(
                rsp > (uint64_t)_stack && rsp < (uint64_t)_stack + _stack_size);

        /* Test nested exception that does not uses alternative
         * stack */
        OE_TEST(test_getsec_instruction() == true);

        oe_sgx_td_t* td = oe_sgx_get_td();

        if (!oe_sgx_td_register_exception_handler_stack(
                td, OE_EXCEPTION_ILLEGAL_INSTRUCTION))
            return OE_EXCEPTION_ABORT_EXECUTION;

        /* Test nested exception that uses alternative stack */
        OE_TEST(test_getsec_instruction() == true);

        /* Test nested exception of the internal cpuid emulation flow */
        {
            uint32_t cpuid_rax = 0;
            uint32_t ebx = 0;
            uint32_t ecx = 0;
            uint32_t edx = 0;

            get_cpuid(0, 0, &cpuid_rax, &ebx, &ecx, &edx);
        }

        exception_record->context->rip += 2;

        return OE_EXCEPTION_CONTINUE_EXECUTION;
    }
    else if (exception_record->code == OE_EXCEPTION_ILLEGAL_INSTRUCTION)
    {
        if (_check_exception_handler_stack ||
            (_use_exception_handler_stack &&
             oe_sgx_td_exception_handler_stack_registered(
                 td, OE_EXCEPTION_ILLEGAL_INSTRUCTION)))
            OE_TEST(
                rsp > (uint64_t)_exception_handler_stack &&
                rsp < (uint64_t)_exception_handler_stack +
                          _exception_handler_stack_size);

        else
            OE_TEST(
                rsp > (uint64_t)_stack && rsp < (uint64_t)_stack + _stack_size);

        OE_TEST(
            *((uint16_t*)exception_record->context->rip) == OE_GETSEC_OPCODE);

        exception_record->context->rip += 2;

        return OE_EXCEPTION_CONTINUE_EXECUTION;
    }

    return OE_EXCEPTION_ABORT_EXECUTION;
}

int enc_test_nested_exception(
    int use_exception_handler_stack,
    int register_exception_type)
{
    get_stack(&_stack, &_stack_size);

    if (use_exception_handler_stack)
    {
        OE_TEST(
            initialize_exception_handler_stack(
                &_exception_handler_stack,
                &_exception_handler_stack_size,
                OE_EXCEPTION_DIVIDE_BY_ZERO,
                register_exception_type) == 0);

        _use_exception_handler_stack = 1;

        if (register_exception_type)
            _check_exception_handler_stack = 1;
    }

    if (oe_add_vectored_exception_handler(
            false, test_nested_exception_handler) != OE_OK)
        return -1;

    divide_by_zero_exception_function();

    if (oe_remove_vectored_exception_handler(test_nested_exception_handler) !=
        OE_OK)
    {
        return -1;
    }

    if (use_exception_handler_stack)
        cleaup_exception_handler_stack(
            &_exception_handler_stack, &_exception_handler_stack_size);

    return 0;
}

OE_SET_ENCLAVE_SGX(
    1,    /* ProductID */
    1,    /* SecurityVersion */
    true, /* Debug */
    1024, /* NumHeapPages */
    1024, /* NumStackPages */
    2);   /* NumTCS */
