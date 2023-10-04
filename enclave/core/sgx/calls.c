// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include "../calls.h"
#include <openenclave/advanced/allocator.h>
#include <openenclave/attestation/attester.h>
#include <openenclave/attestation/verifier.h>
#include <openenclave/bits/sgx/sgxtypes.h>
#include <openenclave/corelibc/stdlib.h>
#include <openenclave/corelibc/string.h>
#include <openenclave/edger8r/enclave.h>
#include <openenclave/enclave.h>
#include <openenclave/internal/atomic.h>
#include <openenclave/internal/calls.h>
#include <openenclave/internal/crypto/init.h>
#include <openenclave/internal/fault.h>
#include <openenclave/internal/globals.h>
#include <openenclave/internal/jump.h>
#include <openenclave/internal/malloc.h>
#include <openenclave/internal/print.h>
#include <openenclave/internal/raise.h>
#include <openenclave/internal/safecrt.h>
#include <openenclave/internal/safemath.h>
#include <openenclave/internal/sgx/ecall_context.h>
#include <openenclave/internal/sgx/td.h>
#include <openenclave/internal/thread.h>
#include <openenclave/internal/trace.h>
#include <openenclave/internal/types.h>
#include <openenclave/internal/utils.h>
#include "../../../common/sgx/sgxmeasure.h"
#include "../../sgx/report.h"
#include "../atexit.h"
#include "../tracee.h"
#include "arena.h"
#include "asmdefs.h"
#include "core_t.h"
#include "cpuid.h"
#include "handle_ecall.h"
#include "init.h"
#include "openenclave/bits/result.h"
#include "openenclave/internal/backtrace.h"
#include "platform_t.h"
#include "report.h"
#include "switchlesscalls.h"
#include "td.h"
#include "xstate.h"

void oe_abort_with_td(oe_sgx_td_t* td) OE_NO_RETURN;

oe_result_t __oe_enclave_status = OE_OK;
uint8_t __oe_initialized = 0;

/*
**==============================================================================
**
** Glossary:
**
**     TCS      - Thread control structure. The TCS is an address passed to
**                EENTER and passed onto the entry point (_start). The TCS
**                is the address of a TCS page in the enclave memory. This page
**                is not accessible to the enclave itself. The enclave stores
**                state about the execution of a thread in this structure,
**                such as the entry point (TCS.oentry), which refers to the
**                _start function. It also maintains the index of the
**                current SSA (TCS.cssa) and the number of SSA's (TCS.nssa).
**
**     oe_sgx_td_t       - Thread data. Per thread data as defined by the
**                oe_thread_data_t structure and extended by the oe_sgx_td_t
*structure.
**                This structure records the stack pointer of the last EENTER.
**
**     SP       - Stack pointer. Refers to the enclave's stack pointer.
**
**     BP       - Base pointer. Refers to the enclave's base pointer.
**
**     HOSTSP   - Host stack pointer. Refers to the host's stack pointer as
**                received in the EENTER call.
**
**     HOSTBP   - Host base pointer. Refers to the host's base pointer as
**                received in the EENTER call.
**
**     AEP      - Asynchronous Exception Procedure. This procedure is passed
**                by the host to EENTER. If a fault occurs while in the enclave,
**                the hardware calls this procedure. The procedure may
**                terminate or call ERESUME to continue executing in the
**                enclave.
**
**     AEX      - Asynchronous Exception (occurs when enclave faults). The
**                hardware transfers control to a host AEP (passed as a
**                parameter to EENTER).
**
**     SSA      - State Save Area. When a fault occurs in the enclave, the
**                hardware saves the state here (general purpose registers)
**                and then transfers control to the host AEP. If the AEP
**                executes the ERESUME instruction, the hardware restores the
**                state from the SSA.
**
**     EENTER   - An untrusted instruction that is executed by the host to
**                enter the enclave. The caller passes the address of a TCS page
**                within the enclave, an AEP, and any parameters in the RDI and
**                RSI registers. This implementation passes the operation
**                number (FUNC) in RDI and a pointer to the arguments structure
**                (ARGS) in RSI.
**
**     EEXIT    - An instruction that is executed by the host to exit the
**                enclave and return control to the host. The caller passes
**                the address of some instruction to jump to (RETADDR) in the
**                RBX register and an AEP in the RCX register (null at this
**                time).
**
**     RETADDR  - Refers to the address of the return instruction that the
**                hardware jumps to from EEXIT. This is an instruction in
**                host immediately following the instruction that executed
**                EENTER.
**
**     CSSA     - The current SSA slot index (as given by TCS.cssa). EENTER
**                passes a CSSA parameter (RAX) to _start(). A CSSA of zero
**                indicates a normal entry. A non-zero CSSA indicates an
**                exception entry (an AEX has occurred).
**
**     NSSA     - The number of SSA slots in the thread section (of this
**                enclave. If CSSA == NSSA, then the SSA's have been exhausted
**                and the EENTER instruction will fault.
**
**     ECALL    - A function call initiated by the host and carried out by
**                the enclave. The host executes the EENTER instruction to
**                enter the enclave.
**
**     ERET     - A return from an ECALL initiated by the enclave. The
**                enclave executes the EEXIT instruction to exit the enclave.
**
**     OCALL    - A function call initiated by the enclave and carried out
**                by the host. The enclave executes the EEXIT instruction to
**                exit the enclave.
**
**     ORET     - A return from an OCALL initiated by the host. The host
**                executes the EENTER instruction to enter the enclave.
**
**==============================================================================
*/

/*
**==============================================================================
** oe_libc_initialize()
**
**   Weak implementation of libc initialization function.
**
**==============================================================================
*/
OE_WEAK void oe_libc_initialize(void)
{
}

/*
**==============================================================================
**
** _handle_init_enclave()
**
**     Handle the OE_ECALL_INIT_ENCLAVE from host and ensures that each state
**     initialization function in the enclave only runs once.
**
**==============================================================================
*/
static oe_result_t _handle_init_enclave(uint64_t arg_in)
{
    static bool _once = false;
    oe_result_t result = OE_OK;
    /* Double checked locking (DCLP). */
    bool o = _once;

    /* DCLP Acquire barrier. */
    OE_ATOMIC_MEMORY_BARRIER_ACQUIRE();
    if (o == false)
    {
        static oe_spinlock_t _lock = OE_SPINLOCK_INITIALIZER;
        oe_spin_lock(&_lock);

        if (_once == false)
        {
            oe_enclave_t* enclave = (oe_enclave_t*)arg_in;

            if (!oe_is_outside_enclave(enclave, 1))
                OE_RAISE(OE_INVALID_PARAMETER);

            oe_enclave = enclave;

            /* Initialize the CPUID table before calling global constructors. */
            OE_CHECK(oe_initialize_cpuid());

            /* Initialize the xstate settings
             * Depends on TD and sgx_create_report, so can't happen earlier */
            OE_CHECK(oe_set_is_xsave_supported());

            /* Initialize libc */
            oe_libc_initialize();

            /* Initialize the OE crypto library. */
            oe_crypto_initialize();

            /* Call global constructors. Now they can safely use simulated
             * instructions like CPUID. */
            oe_call_init_functions();

            /* DCLP Release barrier. */
            OE_ATOMIC_MEMORY_BARRIER_RELEASE();
            _once = true;
            __oe_initialized = 1;
        }

        oe_spin_unlock(&_lock);
    }
done:
    return result;
}

/**
 * This is the preferred way to call enclave functions.
 */
oe_result_t oe_handle_call_enclave_function(uint64_t arg_in)
{
    oe_call_enclave_function_args_t args = {0}, *args_host_ptr = NULL;
    oe_call_function_return_args_t* return_args_ptr = NULL;
    oe_result_t result = OE_OK;
    oe_ecall_func_t func = NULL;
    uint8_t* buffer = NULL;
    uint8_t* input_buffer = NULL;
    uint8_t* output_buffer = NULL;
    size_t buffer_size = 0;
    size_t output_bytes_written = 0;
    ecall_table_t ecall_table;

    // Ensure that args lies outside the enclave and is 8-byte aligned
    // (against the xAPIC vulnerability).
    // The size of oe_call_enclave_function_args_t is guaranteed to be
    // 8-byte aligned via compile-time checks.
    if (!oe_is_outside_enclave(
            (void*)arg_in, sizeof(oe_call_enclave_function_args_t)) ||
        (arg_in % 8) != 0)
        OE_RAISE(OE_INVALID_PARAMETER);

    // Copy args to enclave memory to avoid TOCTOU issues.
    args_host_ptr = (oe_call_enclave_function_args_t*)arg_in;
    oe_memcpy_aligned(
        &args, args_host_ptr, sizeof(oe_call_enclave_function_args_t));

    // Ensure that input buffer is valid (oe_is_outside_enclave ensures
    // the buffer is not NULL).
    // The buffer size must at least equal to oe_call_function_args_t
    if (!oe_is_outside_enclave(args.input_buffer, args.input_buffer_size) ||
        args.input_buffer_size < sizeof(oe_call_function_return_args_t))
        OE_RAISE(OE_INVALID_PARAMETER);

    // Ensure that output buffer is valid (oe_is_outside_enclave ensures
    // the buffer is not NULL).
    // The buffer size must at least equal to oe_call_function_return_args_t
    if (!oe_is_outside_enclave(args.output_buffer, args.output_buffer_size) ||
        args.output_buffer_size < sizeof(oe_call_function_return_args_t))
        OE_RAISE(OE_INVALID_PARAMETER);

    // Validate output and input buffer addresses and sizes.
    // Both of them must be correctly aligned (against the xAPIC vulnerability).
    if ((args.input_buffer_size % OE_EDGER8R_BUFFER_ALIGNMENT) != 0 ||
        ((uint64_t)args.input_buffer % 8) != 0)
        OE_RAISE(OE_INVALID_PARAMETER);

    if ((args.output_buffer_size % OE_EDGER8R_BUFFER_ALIGNMENT) != 0 ||
        ((uint64_t)args.output_buffer % 8) != 0)
        OE_RAISE(OE_INVALID_PARAMETER);

    OE_CHECK(oe_safe_add_u64(
        args.input_buffer_size, args.output_buffer_size, &buffer_size));

    // The __oe_ecall_table is defined in the oeedger8r-generated
    // code.
    ecall_table.ecalls = oe_ecalls_table;
    ecall_table.num_ecalls = oe_ecalls_table_size;

    // Fetch matching function.
    if (args.function_id >= ecall_table.num_ecalls)
        OE_RAISE(OE_NOT_FOUND);

    func = ecall_table.ecalls[args.function_id];

    if (func == NULL)
        OE_RAISE(OE_NOT_FOUND);

    // Allocate buffers in enclave memory
    buffer = input_buffer = oe_malloc(buffer_size);
    if (buffer == NULL)
        OE_RAISE(OE_OUT_OF_MEMORY);

    // Copy input buffer from the host to enclave buffer.
    oe_memcpy_aligned(input_buffer, args.input_buffer, args.input_buffer_size);

    // Clear out output buffer.
    // This ensures reproducible behavior if say the function is reading from
    // output buffer.
    output_buffer = buffer + args.input_buffer_size;
    memset(output_buffer, 0, args.output_buffer_size);

    // Call the function.
    func(
        input_buffer,
        args.input_buffer_size,
        output_buffer,
        args.output_buffer_size,
        &output_bytes_written);

    /*
     * The output_buffer is expected to point to a marshaling struct.
     * The function is expected to fill the struct.
     */
    return_args_ptr = (oe_call_function_return_args_t*)output_buffer;

    result = return_args_ptr->result;
    if (result == OE_OK)
    {
        /*
         * Error out the case if the deepcopy_out_buffer is NULL but the
         * deepcopy_out_buffer_size is not zero or if the deepcopy_out_buffer is
         * not NULL but the deepcopy_out_buffer_size is zero. Note that this
         * should only occur if the oeedger8r was not used or if
         * oeedger8r-generated routine is modified.
         */
        if ((!return_args_ptr->deepcopy_out_buffer &&
             return_args_ptr->deepcopy_out_buffer_size) ||
            (return_args_ptr->deepcopy_out_buffer &&
             !return_args_ptr->deepcopy_out_buffer_size))
            OE_RAISE(OE_UNEXPECTED);

        /*
         * Nonzero deepcopy_out_buffer and deepcopy_out_buffer_size fields
         * indicate that there is deep-copied content that needs to be
         * transmitted to the host.
         */
        if (return_args_ptr->deepcopy_out_buffer &&
            return_args_ptr->deepcopy_out_buffer_size)
        {
            /*
             * Ensure that the content lies in enclave memory.
             * Note that this should only fail if oeedger8r was not used or if
             * the oeedger8r-generated routine is modified.
             */
            if (!oe_is_within_enclave(
                    return_args_ptr->deepcopy_out_buffer,
                    return_args_ptr->deepcopy_out_buffer_size))
                OE_RAISE(OE_UNEXPECTED);

            void* host_buffer =
                oe_host_malloc(return_args_ptr->deepcopy_out_buffer_size);

            /* Copy the deep-copied content to host memory. */
            OE_CHECK(oe_memcpy_s_with_barrier(
                host_buffer,
                return_args_ptr->deepcopy_out_buffer_size,
                return_args_ptr->deepcopy_out_buffer,
                return_args_ptr->deepcopy_out_buffer_size));

            /* Release the memory on the enclave heap. */
            oe_free(return_args_ptr->deepcopy_out_buffer);

            return_args_ptr->deepcopy_out_buffer = host_buffer;
        }

        // Copy outputs to host memory.
        OE_CHECK(oe_memcpy_s_with_barrier(
            args.output_buffer,
            args.output_buffer_size,
            output_buffer,
            args.output_buffer_size));

        // The ecall succeeded.
        OE_WRITE_VALUE_WITH_BARRIER(
            &args_host_ptr->output_bytes_written, output_bytes_written);
        OE_WRITE_VALUE_WITH_BARRIER(&args_host_ptr->result, OE_OK);
    }

done:
    if (result != OE_OK && return_args_ptr && args.output_buffer)
    {
        return_args_ptr->result = result;
        return_args_ptr->deepcopy_out_buffer = NULL;
        return_args_ptr->deepcopy_out_buffer_size = 0;

        oe_memcpy_s_with_barrier(
            args.output_buffer,
            args.output_buffer_size,
            return_args_ptr,
            sizeof(oe_call_function_return_args_t));
    }

    if (buffer)
        oe_free(buffer);

    return result;
}

/*
**==============================================================================
**
** _handle_exit()
**
**     Initiate call to EEXIT.
**
**==============================================================================
*/
static void _handle_exit(oe_code_t code, uint16_t func, uint64_t arg)
    OE_NO_RETURN;

static void _handle_exit(oe_code_t code, uint16_t func, uint64_t arg)
{
    oe_exit_enclave(oe_make_call_arg1(code, func, 0, OE_OK), arg);
}

void oe_virtual_exception_dispatcher(
    oe_sgx_td_t* td,
    uint64_t arg_in,
    uint64_t* arg_out);

/*
**==============================================================================
**
** _call_at_exit_functions()
**
**     Invoke atexit functions (e.g., registered by atexit() or the destructor
**     attribute)
**
**==============================================================================
*/
static void _call_at_exit_functions(void)
{
    static bool _at_exit_functions_done = false;
    static oe_spinlock_t _lock = OE_SPINLOCK_INITIALIZER;

    oe_spin_lock(&_lock);
    if (!_at_exit_functions_done)
    {
        /* Call functions installed by oe_cxa_atexit() and oe_atexit()
         */
        oe_call_atexit_functions();

        /* Call all finalization functions */
        oe_call_fini_functions();

        _at_exit_functions_done = true;
    }
    oe_spin_unlock(&_lock);
}

/*
**==============================================================================
**
** _enclave_destructor()
**
**==============================================================================
*/
static oe_result_t _enclave_destructor(void)
{
    oe_result_t result = OE_FAILURE;
    static bool _destructor_done = false;
    static oe_spinlock_t _lock = OE_SPINLOCK_INITIALIZER;

    oe_spin_lock(&_lock);
    if (!_destructor_done)
    {
        /* Cleanup attesters */
        oe_attester_shutdown();

        /* Cleanup verifiers */
        oe_verifier_shutdown();

        /* If memory still allocated, print a trace and return an error */
        OE_CHECK(oe_check_memory_leaks());

        /* Cleanup the allocator */
        oe_allocator_cleanup();

        _destructor_done = true;
    }

    result = OE_OK;

done:
    oe_spin_unlock(&_lock);
    return result;
}

/*
**==============================================================================
**
** _handle_ecall()
**
**     Handle an ECALL.
**
**==============================================================================
*/

static void _handle_ecall(
    oe_sgx_td_t* td,
    uint16_t func,
    uint64_t arg_in,
    uint64_t* output_arg1,
    uint64_t* output_arg2)
{
    /* To keep status of td consistent before and after _handle_ecall, td_init
     is moved into _handle_ecall. In this way _handle_ecall will not trigger
     stack check fail by accident. Of course not all function have the
     opportunity to keep such consistency. Such basic functions are moved to a
     separate source file and the stack protector is disabled by force
     through fno-stack-protector option. */

    /* Initialize thread data structure (if not already initialized) */
    if (!td_initialized(td))
    {
        td_init(td);
    }

    oe_result_t result = OE_OK;

    /* Insert ECALL context onto front of oe_sgx_td_t.ecalls list */
    oe_callsite_t callsite = {{0}};
    uint64_t arg_out = 0;

    td_push_callsite(td, &callsite);

    // Acquire release semantics for __oe_initialized are present in
    // _handle_init_enclave.
    if (!__oe_initialized)
    {
        // The first call to the enclave must be to initialize it.
        // Global constructors can throw exceptions/signals and result in signal
        // handlers being invoked. Eg. Using CPUID instruction within a global
        // constructor. We should also allow handling these exceptions.
        if (func != OE_ECALL_INIT_ENCLAVE &&
            func != OE_ECALL_VIRTUAL_EXCEPTION_HANDLER)
        {
            goto done;
        }
    }
    else
    {
        // Disallow re-initialization.
        if (func == OE_ECALL_INIT_ENCLAVE)
        {
            goto done;
        }
    }

    // td_push_callsite increments the depth. depth > 1 indicates a reentrant
    // call. Reentrancy is allowed to handle exceptions and to terminate the
    // enclave.
    if (td->depth > 1 && (func != OE_ECALL_VIRTUAL_EXCEPTION_HANDLER &&
                          func != OE_ECALL_DESTRUCTOR))
    {
        /* reentrancy not permitted. */
        result = OE_REENTRANT_ECALL;
        goto done;
    }

    /* Dispatch the ECALL */
    switch (func)
    {
        case OE_ECALL_CALL_ENCLAVE_FUNCTION:
        {
            arg_out = oe_handle_call_enclave_function(arg_in);
            break;
        }
        case OE_ECALL_CALL_AT_EXIT_FUNCTIONS:
        {
            _call_at_exit_functions();
            break;
        }
        case OE_ECALL_DESTRUCTOR:
        {
            /* Invoke atexit functions in case the host does not invoke
             * the CALL_AT_EXIT_FUNCTIONS ecall before the DESTRUCTOR ecall
             * (retaining the previous behavior) */
            _call_at_exit_functions();

            OE_CHECK(_enclave_destructor());

            break;
        }
        case OE_ECALL_VIRTUAL_EXCEPTION_HANDLER:
        {
            oe_virtual_exception_dispatcher(td, arg_in, &arg_out);
            break;
        }
        case OE_ECALL_INIT_ENCLAVE:
        {
            arg_out = _handle_init_enclave(arg_in);
            break;
        }
        default:
        {
            /* No function found with the number */
            result = OE_NOT_FOUND;
            goto done;
        }
    }

done:

    /* Free shared memory arena before we clear TLS */
    if (td->depth == 1)
    {
        oe_teardown_arena();
    }

    /* Remove ECALL context from front of oe_sgx_td_t.ecalls list */
    td_pop_callsite(td);

    /* Perform ERET, giving control back to host */
    *output_arg1 = oe_make_call_arg1(OE_CODE_ERET, func, 0, result);
    *output_arg2 = arg_out;
}

/*
**==============================================================================
**
** _handle_oret()
**
**     Handle an OCALL return.
**
**==============================================================================
*/

OE_INLINE void _handle_oret(
    oe_sgx_td_t* td,
    uint16_t func,
    uint16_t result,
    uint64_t arg)
{
    oe_callsite_t* callsite = td->callsites;

    if (!callsite)
        return;

    td->oret_func = func;
    td->oret_result = result;
    td->oret_arg = arg;

    /* Restore the FXSTATE and flags */
    asm volatile(
        "pushq %[rflags] \n\t" // Restore flags.
        "popfq \n\t"
        "fldcw %[fcw] \n\t"     // Restore x87 control word
        "ldmxcsr %[mxcsr] \n\t" // Restore MXCSR
        "lfence \n\t" // MXCSR Configuration Dependent Timing (MCDT) mitigation
        : [mxcsr] "=m"(callsite->mxcsr),
          [fcw] "=m"(callsite->fcw),
          [rflags] "=m"(callsite->rflags)
        :
        : "cc");

    oe_longjmp(&callsite->jmpbuf, 1);
}

/*
**==============================================================================
**
** oe_get_enclave_status()
**
**     Return the value of __oe_enclave_status to external code.
**
**==============================================================================
*/
oe_result_t oe_get_enclave_status()
{
    return __oe_enclave_status;
}

/*
**==============================================================================
**
** _exit_enclave()
**
** Exit the enclave.
** Additionally, if a debug enclave, write the exit frame information to host's
** ecall_context so that the host can stitch the ocall stack.
**
** This function is intended to be called by oe_asm_exit (see below).
** When called, the call stack would look like this:
**
**     enclave-function
**       -> oe_ocall
**         -> oe_exit_enclave (aliased as __morestack)
**           -> _exit_enclave
**
** For debug enclaves, _exit_enclave reads its caller (oe_exit_enclave/
** __morestack) information (return address, rbp) and passes it along to the
** host in the ecall_context.
**
** Then it proceeds to exit the enclave by invoking oe_asm_exit.
** oe_asm_exit invokes eexit instruction which resumes execution in host at the
** oe_enter function. The host dispatches the ocall via the following sequence:
**
**     oe_enter
**       -> __oe_host_stack_bridge   (Stitches the ocall stack)
**         -> __oe_dispatch_ocall
**           -> invoke ocall function
**
** Now that the enclave exit frame is available to the host,
** __oe_host_stack_bridge temporarily modifies its caller info with the
** enclave's exit information so that the stitched stack looks like this:
**
**     enclave-function                                    |
**       -> oe_ocall                                       |
**         -> oe_exit_enclave (aliased as __morestack)     | in enclave
**   --------------------------------------------------------------------------
**           -> __oe_host_stack_bridge                     | in host
**             -> __oe_dispatch_ocall                      |
**               -> invoke ocall function                  |
**
** This stitching of the stack is temporary, and __oe_host_stack_bridge reverts
** it prior to returning to its caller.
**
** Since the stitched (split) stack is preceded by the __morestack function, gdb
** natively walks the stack correctly.
**
**==============================================================================
*/
OE_NEVER_INLINE
OE_NO_RETURN
static void _exit_enclave(uint64_t arg1, uint64_t arg2)
{
    oe_sgx_td_t* td = oe_sgx_get_td();

    if (oe_is_enclave_debug_allowed())
    {
        oe_ecall_context_t* host_ecall_context = td->host_ecall_context;

        // Make sure the context is valid.
        if (host_ecall_context &&
            oe_is_outside_enclave(
                host_ecall_context, sizeof(*host_ecall_context)))
        {
            uint64_t* frame = (uint64_t*)__builtin_frame_address(0);

            /* NOTE: host memory writes that is only for debugging purposes,
             * no need for using write with barrier */
            host_ecall_context->debug_eexit_rbp = frame[0];
            // The caller's RSP is always given by this equation
            //   RBP + 8 (caller frame pointer) + 8 (caller return address)
            host_ecall_context->debug_eexit_rsp = frame[0] + 8;
            host_ecall_context->debug_eexit_rip = frame[1];
        }
    }
    oe_asm_exit(arg1, arg2, td, 0 /* direct_return */);
}

/*
**==============================================================================
**
** This function is wrapper of oe_asm_exit. It is needed to stitch the host
** stack and enclave stack together. It calls oe_asm_exit via an intermediary
** (_exit_enclave) that records the exit frame for ocall stack stitching.
**
** N.B: Don't change the function name, otherwise debugger can't work. GDB
** depends on this hardcoded function name when does stack walking for split
** stack. oe_exit_enclave has been #defined as __morestack.
**==============================================================================
*/

OE_NEVER_INLINE
void oe_exit_enclave(uint64_t arg1, uint64_t arg2)
{
    _exit_enclave(arg1, arg2);

    // This code is never reached. It exists to prevent tail call optimization
    // of the call to _exit_enclave. Tail-call optimization would effectively
    // inline _exit_enclave, and its caller would be come the caller of
    // oe_exit_enclave instead of oe_exit_enclave.
    oe_abort();
}

/*
**==============================================================================
**
** oe_ocall()
**
**     Initiate a call into the host (exiting the enclave).
**
** Remark: Given that the logging implementation relies on making an ocall to
** host, any failures when handling oe_ocall should not invoke any oe_log
** functions so as to avoid infinite recursion. OE_RAISE and OE_CHECK macros
** call oe_log functions, and therefore the following code locations use
** OE_RAISE_NO_TRACE and OE_CHECK_NO_TRACE macros.
**==============================================================================
*/

oe_result_t oe_ocall(uint16_t func, uint64_t arg_in, uint64_t* arg_out)
{
    oe_result_t result = OE_UNEXPECTED;
    oe_sgx_td_t* td = oe_sgx_get_td();
    oe_callsite_t* callsite = td->callsites;

    /* If the enclave is in crashing/crashed status, new OCALL should fail
    immediately. */
    if (__oe_enclave_status != OE_OK)
        OE_RAISE_NO_TRACE((oe_result_t)__oe_enclave_status);

    /* Check for unexpected failures */
    if (!callsite)
        OE_RAISE_NO_TRACE(OE_UNEXPECTED);

    /* Check for unexpected failures */
    if (!td_initialized(td))
        OE_RAISE_NO_TRACE(OE_FAILURE);

    /* Preserve the FXSTATE and flags */
    asm volatile("stmxcsr %[mxcsr] \n\t" // Save MXCSR
                 "fstcw %[fcw] \n\t"     // Save x87 control word
                 "pushfq \n\t"           // Save flags.
                 "popq %[rflags] \n\t"
                 :
                 : [mxcsr] "m"(callsite->mxcsr),
                   [fcw] "m"(callsite->fcw),
                   [rflags] "m"(callsite->rflags)
                 :);

    /* Save call site where execution will resume after OCALL */
    if (oe_setjmp(&callsite->jmpbuf) == 0)
    {
        /* Exit, giving control back to the host so it can handle OCALL */
        _handle_exit(OE_CODE_OCALL, func, arg_in);

        /* Unreachable! Host will transfer control back to oe_enter() */
        oe_abort();
    }
    else
    {
        /* ORET here */

        OE_CHECK_NO_TRACE(result = (oe_result_t)td->oret_result);

        if (arg_out)
            *arg_out = td->oret_arg;

        if (td->state != OE_TD_STATE_SECOND_LEVEL_EXCEPTION_HANDLING)
        {
            /* State machine check */
            if (td->state != OE_TD_STATE_ENTERED)
                oe_abort();

            td->state = OE_TD_STATE_RUNNING;
        }
    }

    result = OE_OK;

done:
    return result;
}

/*
**==============================================================================
**
** oe_call_host_function_by_table_id()
**
**==============================================================================
*/

oe_result_t oe_call_host_function_internal(
    uint64_t function_id,
    const void* input_buffer,
    size_t input_buffer_size,
    void* output_buffer,
    size_t output_buffer_size,
    size_t* output_bytes_written,
    bool switchless)
{
    oe_result_t result = OE_UNEXPECTED;
    oe_call_host_function_args_t args, *args_host_ptr = NULL;
    oe_call_function_return_args_t return_args, *return_args_host_ptr = NULL;
    uint64_t host_result = 0;

    /* Ensure input buffer is outside the enclave memory and its size is valid
     */
    if (!oe_is_outside_enclave(input_buffer, input_buffer_size) ||
        input_buffer_size < sizeof(oe_call_function_return_args_t))
        OE_RAISE(OE_INVALID_PARAMETER);

    /* Ensure output buffer is outside the enclave memory and its size is
     * valid. Also, check its address is 8-byte aligned (against the xAPIC
     * vulnerability) */
    if (!oe_is_outside_enclave(output_buffer, output_buffer_size) ||
        output_buffer_size < sizeof(oe_call_function_return_args_t) ||
        ((uint64_t)output_buffer % 8) != 0)
        OE_RAISE(OE_INVALID_PARAMETER);

    /*
     * oe_post_switchless_ocall (below) can make a regular ocall to wake up the
     * host worker thread, and will end up using the ecall context's args.
     * Therefore, for switchless calls, allocate args in the arena so that it is
     * is not overwritten by oe_post_switchless_ocall.
     */
    args_host_ptr =
        (oe_call_host_function_args_t*)(switchless ? oe_arena_malloc(sizeof(*args_host_ptr)) : oe_ecall_context_get_ocall_args());

    /* Ensure the args_host_ptr is valid and 8-byte aligned (for xAPIC
     * vulnerability mitigation) */
    if (!oe_is_outside_enclave(
            (const void*)args_host_ptr, sizeof(oe_call_host_function_args_t)) ||
        ((uint64_t)args_host_ptr % 8) != 0)
    {
        /* Fail if the enclave is crashing. */
        OE_CHECK(__oe_enclave_status);
        OE_RAISE(OE_UNEXPECTED);
    }

    /* Prepare a local copy of args */
    args.function_id = function_id;
    args.input_buffer = input_buffer;
    args.input_buffer_size = input_buffer_size;
    args.output_buffer = output_buffer;
    args.output_buffer_size = output_buffer_size;
    args.result = OE_UNEXPECTED;

    /* Copy the local copy of args to host memory */
    OE_CHECK(oe_memcpy_s_with_barrier(
        args_host_ptr, sizeof(*args_host_ptr), &args, sizeof(args)));

    /* Call the host function with this address */
    if (switchless && oe_is_switchless_initialized())
    {
        oe_result_t post_result = oe_post_switchless_ocall(args_host_ptr);

        // Fall back to regular OCALL if host worker threads are unavailable
        if (post_result == OE_CONTEXT_SWITCHLESS_OCALL_MISSED)
            OE_CHECK(oe_ocall(
                OE_OCALL_CALL_HOST_FUNCTION, (uint64_t)args_host_ptr, NULL));
        else
        {
            OE_CHECK(post_result);
            // Wait until args.result is set by the host worker.
            while (true)
            {
                OE_ATOMIC_MEMORY_BARRIER_ACQUIRE();

                /* The member result is alignend given that args_host_ptr is
                 * aligned and its size is 8-byte (for xAPIC vulnerability
                 * mitigation). */
                if (__atomic_load_n(&args_host_ptr->result, __ATOMIC_SEQ_CST) !=
                    OE_UINT64_MAX)
                    break;

                /* Yield to CPU */
                asm volatile("pause");
            }
        }
    }
    else
    {
        OE_CHECK(oe_ocall(
            OE_OCALL_CALL_HOST_FUNCTION, (uint64_t)args_host_ptr, NULL));
    }

    /* Copy the result from the host memory
     * The member result is aligned given that args_host_ptr is aligned
     * and its size is 8 byte (for xAPIC vulnerability mitigation). */
    host_result = args_host_ptr->result;

    /* Check the result */
    OE_CHECK((oe_result_t)host_result);

    return_args_host_ptr = (oe_call_function_return_args_t*)output_buffer;

    /* Copy the marshaling struct from the host memory to avoid TOCTOU issues.
     * To mitigate the xAPIC vulnerability, the output_buffer and the size of
     * oe_call_function_return_args_t must be aligned at this point via runtime
     * and compile-time checks, repsectively. */
    oe_memcpy_aligned(
        &return_args,
        return_args_host_ptr,
        sizeof(oe_call_function_return_args_t));

    if (return_args.result == OE_OK)
    {
        /*
         * Error out the case if the deepcopy_out_buffer is NULL but the
         * deepcopy_out_buffer_size is not zero or if the deepcopy_out_buffer is
         * not NULL but the deepcopy_out_buffer_size is zero. Note that this
         * should only occur if the oeedger8r was not used or if
         * oeedger8r-generated routine is modified.
         */
        if ((!return_args.deepcopy_out_buffer &&
             return_args.deepcopy_out_buffer_size) ||
            (return_args.deepcopy_out_buffer &&
             !return_args.deepcopy_out_buffer_size))
            OE_RAISE(OE_UNEXPECTED);

        /*
         * Nonzero deepcopy_out_buffer and deepcopy_out_buffer_size fields
         * indicate that there is deep-copied content that needs to be
         * transmitted from the host.
         */
        if (return_args.deepcopy_out_buffer &&
            return_args.deepcopy_out_buffer_size)
        {
            /*
             * Ensure that the deepcopy_out_buffer and deepcopy_out_buffer_size
             * are both 8-byte aligned against the xAPIC vulnerability.
             */
            if ((((uint64_t)return_args.deepcopy_out_buffer % 8) != 0) ||
                (return_args.deepcopy_out_buffer_size % 8) != 0)
                OE_RAISE(OE_UNEXPECTED);

            /*
             * Ensure that the content lies in host memory.
             * Note that this should only fail if oeedger8r was not used or if
             * the oeedger8r-generated routine is modified.
             */
            if (!oe_is_outside_enclave(
                    return_args.deepcopy_out_buffer,
                    return_args.deepcopy_out_buffer_size))
                OE_RAISE(OE_UNEXPECTED);

            void* enclave_buffer =
                oe_malloc(return_args.deepcopy_out_buffer_size);

            if (!enclave_buffer)
                OE_RAISE(OE_OUT_OF_MEMORY);

            /* Copy the deep-copied content to enclave memory. */
            oe_memcpy_aligned(
                enclave_buffer,
                return_args.deepcopy_out_buffer,
                return_args.deepcopy_out_buffer_size);

            /* Release the memory on host heap. */
            oe_host_free(return_args.deepcopy_out_buffer);

            /*
             * Update the deepcopy_out_buffer field.
             * Note that the field is still in host memory. Currently, the
             * oeedger8r-generated code will perform an additional check that
             * ensures the buffer stays within the enclave memory.
             */
            OE_WRITE_VALUE_WITH_BARRIER(
                &(return_args_host_ptr->deepcopy_out_buffer), enclave_buffer);
        }
    }

    /* The member output_bytes_written is aligned given that args_host_ptr is
     * aligned (for xAPIC vulnerability mitigation) */
    *output_bytes_written = args_host_ptr->output_bytes_written;
    result = OE_OK;

done:
    if (result != OE_OK && return_args_host_ptr)
    {
        /* Set up the local return_args for the failing case */
        return_args.result = result;
        return_args.deepcopy_out_buffer = NULL;
        return_args.deepcopy_out_buffer_size = 0;

        /* Copy the return_args to host memory */
        oe_memcpy_s_with_barrier(
            return_args_host_ptr,
            sizeof(*return_args_host_ptr),
            &return_args,
            sizeof(return_args));
    }

    return result;
}

/*
**==============================================================================
**
** oe_call_host_function()
** This is the preferred way to call host functions.
**
**==============================================================================
*/

oe_result_t oe_call_host_function(
    size_t function_id,
    const void* input_buffer,
    size_t input_buffer_size,
    void* output_buffer,
    size_t output_buffer_size,
    size_t* output_bytes_written)
{
    return oe_call_host_function_internal(
        function_id,
        input_buffer,
        input_buffer_size,
        output_buffer,
        output_buffer_size,
        output_bytes_written,
        false /* non-switchless */);
}

/*
**==============================================================================
**
** _stitch_ecall_stack()
**
**     This function fixes up the first enclave frame (passed in) when the
**     enclave is in debug mode and the ecall_context includes valid
**     debug_eenter_rbp and debug_eenter_rip (i.e., both of which should
**     be set and point to host memory). Otherwise, the function is a no-op.
**     Currently, the stack stitching is required when vDSO is used on Linux.
**
**     Backtrace before stitching:
**
**     oe_ecall                                    | in host
**       -> _do_eenter                             |
**         -> oe_enter (aliased as __morestack)    |
**           -> oe_vdso_enter                      |
**             -> __vdso_sgx_enter_enclave         |
**   --------------------------------------------------------------------------
**             -> oe_enter                         | in enclave
**              -> __oe_handle_main                |
**
**     Given that __vdso_sgx_enter_enclave is the vDSO function, we cannot rely
**     on the Linux kernel to preserve its stack frame. Instead, we fix up the
**     call stack that bypasses oe_vdso_enter and __vdso_sgx_enter_enclave in
**     the trace, making it align with the trace when vDSO is not used.
**
**     Backtrace after stitching:
**
**     oe_ecall                                    | in host
**       -> _do_eenter                             |
**         -> oe_enter (aliased as __morestack)    |
**   --------------------------------------------------------------------------
**         -> oe_enter                             | in enclave
**           -> __oe_handle_main                   |
**
**==============================================================================
*/

static void _stitch_ecall_stack(oe_sgx_td_t* td, uint64_t* first_enclave_frame)
{
    oe_ecall_context_t* ecall_context = td->host_ecall_context;

    if (oe_is_enclave_debug_allowed())
    {
        if (oe_is_outside_enclave(ecall_context, sizeof(*ecall_context)))
        {
            uint64_t host_rbp = ecall_context->debug_eenter_rbp;
            uint64_t host_rip = ecall_context->debug_eenter_rip;

            /* Check that the supplied host frame (hpst_rbp, host_rip) are set
             * and really lies outside before stitching the stack */
            if (oe_is_outside_enclave((void*)host_rbp, sizeof(uint64_t)) &&
                oe_is_outside_enclave((void*)host_rip, sizeof(uint64_t)))
            {
                first_enclave_frame[0] = host_rbp;
                first_enclave_frame[1] = host_rip;
            }
        }
    }
}

/*
**==============================================================================
**
** __oe_handle_main()
**
**     This function is called by oe_enter(), which is called by the EENTER
**     instruction (executed by the host). The host passes the following
**     parameters to EENTER:
**
**         RBX - TCS - address of a TCS page in the enclave
**         RCX - AEP - pointer to host's asynchronous exception procedure
**         RDI - ARGS1 (holds the CODE and FUNC parameters)
**         RSI - ARGS2 (holds the pointer to the args structure)
**
**     EENTER then calls oe_enter() with the following registers:
**
**         RAX - CSSA - index of current SSA
**         RBX - TCS - address of TCS
**         RCX - RETADDR - address to jump back to on EEXIT
**         RDI - ARGS1 (holds the code and func parameters)
**         RSI - ARGS2 (holds the pointer to the args structure)
**
**     Finally oe_enter() calls this function with the following parameters:
**
**         ARGS1 (holds the code and func parameters)
**         ARGS2 (holds the pointer to the args structure)
**         CSSA - index of current SSA
**         TCS - address of TCS (thread control structure)
**
**     Each enclave contains one or more thread sections (a collection of pages
**     used by a thread entering the enclave). Each thread section has the
**     following layout:
**
**         +----------------------------+
**         | Guard Page                 |
**         +----------------------------+
**         | Stack pages                |
**         +----------------------------+
**         | Guard Page                 |
**         +----------------------------+
**         | TCS Page                   |
**         +----------------------------+
**         | SSA (State Save Area) 0    |
**         +----------------------------+
**         | SSA (State Save Area) 1    |
**         +----------------------------+
**         | Guard Page                 |
**         +----------------------------+
**         | Thread local storage       |
**         +----------------------------+
**         | FS/GS Page (oe_sgx_td_t + tsp)    |
**         +----------------------------+
**
**     EENTER sets the FS segment register to refer to the FS page before
**     calling this function.
**
**     If the enclave should fault, SGX saves the registers in the SSA slot
**     (given by CSSA) and invokes the host's asynchronous exception handler
**     (AEP). The handler may terminate or call ERESUME which increments CSSA
**     and enters this function again. So:
**
**         CSSA == 0: indicates a normal entry
**         CSSA >= 1: indicates an exception entry
**
**     Since the enclave builder only allocates two SSA pages, the enclave can
**     nest no more than two faults. EENTER fails when the number of SSA slots
**     are exhausted (i.e., TCS.CSSA == TCS.NSSA)
**
**     This function ultimately calls EEXIT to exit the enclave. An enclave may
**     exit to the host for two reasons (aside from an asynchronous exception
**     already mentioned):
**
**         (1) To return normally from an ECALL
**         (2) To initiate an OCALL
**
**     When exiting to perform an OCALL, the host may perform another ECALL,
**     and so ECALLS and OCALLS may be nested arbitrarily until stack space is
**     exhausted (hitting a guard page). The state for performing nested calls
**     is maintained on the stack associated with the TCS (see diagram above).
**
**     The enclave's stack pointer is determined as follows:
**
**         (*) For non-nested calls, the stack pointer is calculated relative
**             to the TCS (one page before minus the STATIC stack size).
**
**         (*) For nested calls the stack pointer is obtained from the
**             oe_sgx_td_t.last_sp field (saved by the previous call).
**
**==============================================================================
*/
void __oe_handle_main(
    uint64_t arg1,
    uint64_t arg2,
    uint64_t cssa,
    void* tcs,
    uint64_t* output_arg1,
    uint64_t* output_arg2)
{
    oe_code_t code = oe_get_code_from_call_arg1(arg1);
    uint16_t func = oe_get_func_from_call_arg1(arg1);
    uint16_t arg1_result = oe_get_result_from_call_arg1(arg1);
    uint64_t arg_in = arg2;
    *output_arg1 = 0;
    *output_arg2 = 0;

    /* Get pointer to the thread data structure */
    oe_sgx_td_t* td = td_from_tcs(tcs);

    /* Initialize the enclave the first time it is ever entered. Note that
     * this function DOES NOT call global constructors. Global construction
     * is performed while handling OE_ECALL_INIT_ENCLAVE. */
    oe_initialize_enclave(td);

    /* td's host_ecall_context is set in enter.S and this is the first chance we
       get to validate it. */
    oe_ecall_context_t* ecall_context = td->host_ecall_context;
    if (!oe_is_outside_enclave(ecall_context, sizeof(*ecall_context)))
        td->host_ecall_context = NULL;

    /* Ensure that ecall_context is 8-byte aligned against the xAPIC
     * vunlerability */
    if (((uint64_t)ecall_context % 8) != 0)
        td->host_ecall_context = NULL;

    /* Stitch the stack. Pass the caller's frame for fix up.
     * Note that before stitching, the caller's frame points
     * to the host stack right before switiching to the enclave
     * stack (see .construct_stack_frame in enter.S).
     * The function is called after oe_initialize_enclave
     * (relocations have been applied) so that we can safely
     * access globals that are referenced via GOT. */
    _stitch_ecall_stack(td, __builtin_frame_address(1));

    // Block enclave enter based on current enclave status.
    switch (__oe_enclave_status)
    {
        case OE_OK:
        {
            break;
        }
        case OE_ENCLAVE_ABORTING:
        {
            // Block any ECALL except first time OE_ECALL_DESTRUCTOR call.
            // Don't block ORET here.
            if (code == OE_CODE_ECALL)
            {
                if (func == OE_ECALL_DESTRUCTOR)
                {
                    // Termination function should be only called once.
                    __oe_enclave_status = OE_ENCLAVE_ABORTED;
                }
                else
                {
                    // Return crashing status.
                    *output_arg1 =
                        oe_make_call_arg1(OE_CODE_ERET, func, 0, OE_OK);
                    *output_arg2 = __oe_enclave_status;
                    return;
                }
            }

            break;
        }
        default:
        {
            // Return crashed status.
            *output_arg1 = oe_make_call_arg1(OE_CODE_ERET, func, 0, OE_OK);
            *output_arg2 = OE_ENCLAVE_ABORTED;
            return;
        }
    }

    /* If this is a normal (non-exception) entry */
    if (cssa == 0)
    {
        switch (code)
        {
            case OE_CODE_ECALL:
            {
                /* The invocation of the virtual exception handler is not
                 * allowed when cssa=0. */
                if (func == OE_ECALL_VIRTUAL_EXCEPTION_HANDLER)
                    oe_abort_with_td(td);

                /* State machine check */
                if (td->state != OE_TD_STATE_ENTERED)
                    oe_abort_with_td(td);

                /* At this point, we are ready to execute the ecall.
                 * Update the state to RUNNING */
                td->state = OE_TD_STATE_RUNNING;

                _handle_ecall(td, func, arg_in, output_arg1, output_arg2);
                break;
            }
            case OE_CODE_ORET:
                /* Eventually calls oe_exit_enclave() and never returns here if
                 * successful */
                _handle_oret(td, func, arg1_result, arg_in);
                // fallthrough

            default:
                /* Unexpected case */
                oe_abort_with_td(td);
        }
    }
    else if (cssa == 1)
    {
        /* cssa == 1 indicates the entry after an AEX. We only allow the
         * invocation of the virtual exception handler in this case. */
        if ((code == OE_CODE_ECALL) &&
            (func == OE_ECALL_VIRTUAL_EXCEPTION_HANDLER))
        {
            _handle_ecall(td, func, arg_in, output_arg1, output_arg2);
            return;
        }

        /* Unexpected case */
        oe_abort_with_td(td);
    }
    else /* cssa > 1 */
    {
        /* Currently OE only supports an enclave with nssa = 2, which means
         * that cssa can never exceed 1 (indicating nested AEX). */
        oe_abort_with_td(td);
    }
}

/* Abort the enclave execution with valid td. This function is only directly
 * invoked by __oe_handle_main and init.c where the td may not be initialized
 * yet (i.e., before the td_init() is called in the very first
 * oe_handle_ecall()). For the other scenarios, this function is wrapped by
 * oe_abort where we can safely get td with oe_sgx_get_td_no_check(). */
void oe_abort_with_td(oe_sgx_td_t* td)
{
    uint64_t arg1 = oe_make_call_arg1(OE_CODE_ERET, 0, 0, OE_OK);

    /* Abort can be called with user-modified FS (e.g., FS check fails in
     * oe_sgx_get_td()). */
    if (oe_is_enclave_debug_allowed())
    {
        oe_ecall_context_t* host_ecall_context = td->host_ecall_context;

        // Make sure the context is valid.
        if (host_ecall_context &&
            oe_is_outside_enclave(
                host_ecall_context, sizeof(*host_ecall_context)))
        {
            uint64_t* frame = (uint64_t*)__builtin_frame_address(0);

            /* NOTE: host memory writes that is only for debugging purposes,
             * no need for using write with barrier */
            host_ecall_context->debug_eexit_rbp = frame[0];
            // The caller's RSP is always given by this equation
            //   RBP + 8 (caller frame pointer) + 8 (caller return address)
            host_ecall_context->debug_eexit_rsp = frame[0] + 8;
            host_ecall_context->debug_eexit_rip = frame[1];
        }

        // For debug enclaves, log the backtrace before marking the enclave as
        // aborted.
        {
            // Fetch current values of FS and GS. Typically, FS[0] == FS and
            // GS[0] == GS.
            uint64_t fs;
            uint64_t gs;
            asm volatile("mov %%fs:0, %0" : "=r"(fs));
            asm volatile("mov %%gs:0, %0" : "=r"(gs));

            // We can make ocalls only if td has been initialized which is true
            // only when the self-pointer has been setup.
            if (gs == (uint64_t)td)
            {
                // Restore FS if FS has been modified.
                if (fs != gs)
                {
                    // wrfsbase could trigger an exception. The enclave may not
                    // be in a state to emulate the instruction. Therefore, just
                    // restore FS[0].
                    asm volatile("mov %0, %%fs:0" : : "r"(gs) : "memory");
                }

                void* buffer[OE_BACKTRACE_MAX];
                int size;
                oe_result_t r = OE_UNEXPECTED;
                if ((size = oe_backtrace(buffer, OE_BACKTRACE_MAX)) > 0)
                {
                    oe_sgx_log_backtrace_ocall(
                        &r,
                        oe_get_enclave(),
                        OE_LOG_LEVEL_ERROR,
                        (uint64_t*)buffer,
                        (size_t)size);
                }
                else
                {
                    // It is not possible to convey much information at this
                    // point.
                }

                // Rever FS if it was restored above.
                if (fs != gs)
                    asm volatile("mov %0, %%fs:0" : : "r"(fs) : "memory");
            }
        }
    }

    td->state = OE_TD_STATE_ABORTED;

    // Once it starts to crash, the state can only transit forward, not
    // backward.
    if (__oe_enclave_status < OE_ENCLAVE_ABORTING)
    {
        __oe_enclave_status = OE_ENCLAVE_ABORTING;
    }

    // Return to the latest ECALL.
    oe_asm_exit(arg1, __oe_enclave_status, td, 1 /* direct_return */);
}

void oe_abort(void)
{
    /* Bypass the FS check given that the oe_abort can be invoked anywhere */
    oe_sgx_td_t* td = oe_sgx_get_td_no_fs_check();

    /* It is unlikely that td is invalid. If this is the case, we cannot
     * call _abort to exit the enclave. Instead, we intentionally trigger
     * the page fault by writing to the code page to exit the enclave.
     * Note that the subsequent execution may hang in case that state machine
     * check fails in oe_enter, which will block the call to the
     * __oe_handle_main(). If the execution reaches __oe_handle_main(), we can
     * safely abort with valid td via the check against __oe_enclave_status. */
    if (!td)
    {
        uint64_t oe_abort_address = (uint64_t)oe_abort;

        __oe_enclave_status = OE_ENCLAVE_ABORTING;

        asm volatile("mov $1, %0" : "=r"(*(uint64_t*)oe_abort_address));
    }

    oe_abort_with_td(td);
}
