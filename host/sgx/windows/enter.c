// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/bits/sgx/sgxtypes.h>
#include <openenclave/internal/calls.h>
#include <openenclave/internal/constants_x64.h>
#include <openenclave/internal/registers.h>
#include <openenclave/internal/sgx/ecall_context.h>
#include "../asmdefs.h"
#include "../enclave.h"
#include "../xstate.h"

/* Note: The code was originally made to work on both Linux and Windows.
 * Given that the diversity increases with the support of vDSO, we make
 * the copy of the code to each OS, sgx/linux/enter.c and sgx/windows/enter.c,
 * and apply vDSO-related changes to the former while leave the latter
 * mostly untouched. Doing so also avoids breaking the debugging contract
 * on Windows, which requires careful review before we want to merge
 * the two implementations again. */

// Define a variable with given name and bind it to the register with the
// corresponding name. This allows manipulating the register as a normal
// C variable. The variable and hence the register is also assigned the
// specified value.
#define OE_DEFINE_REGISTER(regname, value) \
    register uint64_t regname __asm__(#regname) = (uint64_t)(value)

// In x64 Windows ABI, the frame pointer can be any register and the frame
// pointer points to a constant location *within* the frame. In x64, the
// frame pointer points to the top of the frame. Windows debugger extensions
// for Open Enclave SDK require a linux-style frame pointer for the oe_enter
// function on the host-side.
#define OE_DEFINE_FRAME_POINTER(r, v) OE_DEFINE_REGISTER(r, v)

// As per Windows x64 ABI, the linux style frame pointer is -0x40 bytes
// from the address of the enclave parameter which is passed via the stack.
// Enclave parameter is the 7th parameter. Including the return-address, the
// Linux style frame-pointer is -(7+1)*8 = -64 = -0x40 bytes from the enclave
// parameter in the stack.
#define OE_FRAME_POINTER_VALUE ((uint64_t)&enclave - 0x40)
#define OE_FRAME_POINTER , "r"(rbp)

// The following registers are inputs to ENCLU instruction. They are also
// clobbered and hence are marked as +r.
#define OE_ENCLU_REGISTERS \
    "+r"(rax), "+r"(rbx), "+r"(rcx), "+r"(rdi), "+r"(rsi), "+r"(rdx)

// The following registers are clobbered by ENCLU.
// Only rbp and rsp are preserved on return from ENCLU.
// The XMM registers are listed as clobbered to signal to the compiler that
// they need to be preserved when --target=x86_64-msvc-windows and are
// ignored on Linux builds.
#define OE_ENCLU_CLOBBERED_REGISTERS                                      \
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "xmm6", "xmm7", \
        "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"

// Release two registers for the simulation mode, which is required by the
// inline assembly to save rsp and rbp in memory (used as scratch registers)
#define OE_SIMULATE_ENCLU_CLOBBERED_REGISTERS                                 \
    "r10", "r11", "r12", "r13", "r14", "r15", "xmm6", "xmm7", "xmm8", "xmm9", \
        "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"

#define OE_VZEROUPPER              \
    asm volatile("vzeroupper \n\t" \
                 :                 \
                 :                 \
                 : "ymm0",         \
                   "ymm1",         \
                   "ymm2",         \
                   "ymm3",         \
                   "ymm4",         \
                   "ymm5",         \
                   "ymm6",         \
                   "ymm7",         \
                   "ymm8",         \
                   "ymm9",         \
                   "ymm10",        \
                   "ymm11",        \
                   "ymm12",        \
                   "ymm13",        \
                   "ymm14",        \
                   "ymm15")

// The following function must not be inlined and must have a frame-pointer
// Windows can use any register as the frame pointer or omit the frame-pointer
// altogether. We just a constant offset from the seventh parameter
// (ecall_context) to fetch the linux style frame-pointer.
// See OE_FRAME_POINTER_VALUE above.
// ATTENTION: ENSURE THAT ECALL_CONTEXT IS THE SEVENTH PARAMETER.
OE_NEVER_INLINE
int __oe_host_stack_bridge(
    uint64_t arg1,
    uint64_t arg2,
    uint64_t* arg1_out,
    uint64_t* arg2_out,
    void* tcs,
    oe_enclave_t* enclave,
    oe_ecall_context_t* ecall_context)
{
    // Use volatile attribute so that the compiler does not optimize away the
    // restoration of the stack frame.
    volatile oe_host_ocall_frame_t *current = NULL, backup;
    bool debug = enclave->debug;
    if (debug)
    {
        // Fetch pointer to current frame.
        current = (oe_host_ocall_frame_t*)((uint64_t)&ecall_context - 0x40);

        // Back up current frame.
        backup = *current;

        // Stitch the ocall stack
        current->return_address = ecall_context->debug_eexit_rip;
        current->previous_rbp = ecall_context->debug_eexit_rbp;
    }

    int ret = __oe_dispatch_ocall(arg1, arg2, arg1_out, arg2_out, tcs, enclave);

    if (debug)
    {
        // Restore the frame so that this function can return to the caller
        // correctly. Without the volatile qualifier, the compiler could
        // optimize this away.
        *current = backup;
    }

    return ret;
}

/**
 * Size of ocall buffers passed in ecall_contexts. Large enough for most ocalls.
 * If an ocall requires more than this size, then the enclave will make an
 * ocall to allocate the buffer instead of using the ecall_context's buffer.
 * Note: Currently, quotes are about 10KB.
 */
#define OE_DEFAULT_OCALL_BUFFER_SIZE (16 * 1024)

/**
 * Setup the ecall_context.
 */
OE_INLINE void _setup_ecall_context(oe_ecall_context_t* ecall_context)
{
    oe_thread_binding_t* binding = oe_get_thread_binding();
    if (binding->ocall_buffer == NULL)
    {
        // Lazily allocate buffer for making ocalls. Bound to the tcs.
        // Will be cleaned up by enclave during termination.
        binding->ocall_buffer = malloc(OE_DEFAULT_OCALL_BUFFER_SIZE);
        binding->ocall_buffer_size = OE_DEFAULT_OCALL_BUFFER_SIZE;
    }
    ecall_context->ocall_buffer = binding->ocall_buffer;
    ecall_context->ocall_buffer_size = binding->ocall_buffer_size;
}

/**
 * oe_enter Executes the ENCLU instruction and transfers control to the enclave.
 *
 * The ENCLU instruction has the following contract:
 * EENTER(RBX=TCS, RCX=AEP, RDX=ECALL_CONTEXT, RDI=ARG1, RSI=ARG2) contract
 * Input:
 *       RBX=TCS, RCX=AEP, RDX=ECALL_CONTEXT, RDI=ARG1, RSI=ARG2
 *       RBP=Current host stack rbp,
 *       RSP=Current host stack sp.
 *       All other registers are NOT used/ignored.
 * Output:
 *       RDI=ARG1OUT, RSI=ARG2OUT,
 *       RBP, RBP are preserved.
 *       All other Registers are clobbered.
 *
 * Callee-saved (non-volatile) registers:
 * As per System V x64 ABI, the registers RBX, RBP, RSP, R12, R13, R14, and R15
 * are preserved across function calls.
 * As per x64 Windows ABI, the registers RBX, RBP, RDI, RSI, RSP, R12, R13, R14,
 * R15, and XMM6-15 are preserved across function calls.
 * The general purpose callee-saved registers and XMM registers are listed in
 * OE_ENCLU_CLOBBERED_REGISTERS.
 */
OE_NEVER_INLINE
oe_result_t oe_enter(
    void* tcs,
    uint64_t aep,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t* arg3,
    uint64_t* arg4,
    oe_enclave_t* enclave)
{
    // Additional control registers that need to be preserved as part of the
    // Windows and Linux x64 ABIs
    uint32_t mxcsr = 0;
    uint16_t fcw = 0;

    oe_ecall_context_t ecall_context = {0};
    _setup_ecall_context(&ecall_context);

    while (1)
    {
        // Compiler will usually handle this on exiting a function that uses
        // AVX, but we need to avoid the AVX-SSE transition penalty here
        // manually as part of the transition to enclave. See
        // https://software.intel.com/content/www/us/en/develop/articles
        // /avoiding-avx-sse-transition-penalties.html
        if (oe_is_avx_enabled)
            OE_VZEROUPPER;

        // Define register bindings and initialize the registers.
        // On Windows, explicitly setup rbp as a Linux ABI style frame-pointer.
        // On Linux, the frame-pointer is set up by compiling the file with the
        // -fno-omit-frame-pointer flag.
        OE_DEFINE_REGISTER(rax, ENCLU_EENTER);
        OE_DEFINE_REGISTER(rbx, tcs);
        OE_DEFINE_REGISTER(rcx, aep);
        OE_DEFINE_REGISTER(rdx, &ecall_context);
        OE_DEFINE_REGISTER(rdi, arg1);
        OE_DEFINE_REGISTER(rsi, arg2);
        OE_DEFINE_FRAME_POINTER(rbp, OE_FRAME_POINTER_VALUE);

        asm volatile("stmxcsr %[mxcsr] \n\t" // Save MXCSR
                     "fstcw %[fcw] \n\t"     // Save x87 control word
                     "pushfq \n\t"           // Save RFLAGS
                     "enclu \n\t"            // EENTER
                     "popfq \n\t"            // Restore RFLAGS
                     "fldcw %[fcw] \n\t"     // Restore x87 control word
                     "ldmxcsr %[mxcsr] \n\t" // Restore MXCSR
                     : OE_ENCLU_REGISTERS
                     : [fcw] "m"(fcw), [mxcsr] "m"(mxcsr)OE_FRAME_POINTER
                     : OE_ENCLU_CLOBBERED_REGISTERS);

        // Update arg1 and arg2 with outputs returned by the enclave.
        arg1 = rdi;
        arg2 = rsi;

        // Make an OCALL if needed.
        oe_code_t code = oe_get_code_from_call_arg1(arg1);
        if (code == OE_CODE_OCALL)
        {
            __oe_host_stack_bridge(
                arg1, arg2, &arg1, &arg2, tcs, enclave, &ecall_context);
        }
        else
            break;
    }

    *arg3 = arg1;
    *arg4 = arg2;

    return OE_OK;
}

/**
 * oe_enter_sim Simulates the ENCLU instruction.
 *
 * See oe_enter above for ENCLU instruction's contract.
 * For simulation, the contract is modified as below:
 *  - rax is the CSSA which is always 0
 *  - rcx contains the return address instead of the AEP
 *  - The address of the enclave entry point is fetched from the tcs
 *    (offset 72) and the control is transferred to it via a jmp
 */
OE_NEVER_INLINE
void oe_enter_sim(
    void* tcs,
    uint64_t aep,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t* arg3,
    uint64_t* arg4,
    oe_enclave_t* enclave)
{
    OE_UNUSED(aep);
    OE_ALIGNED(16)
    uint64_t fx_state[64];
    uint16_t func;
    uint64_t cssa;
    sgx_ssa_gpr_t* ssa_gpr;
    const uint64_t ssa = (uint64_t)tcs + OE_SSA_FROM_TCS_BYTE_OFFSET;

    // Backup host FS and GS registers
    void* host_fs = oe_get_fs_register_base();
    void* host_gs = oe_get_gs_register_base();
    sgx_tcs_t* sgx_tcs = (sgx_tcs_t*)tcs;
    oe_ecall_context_t ecall_context = {0};
    _setup_ecall_context(&ecall_context);

    while (1)
    {
        // Set FS/GS registers to values set by the ENCLU instruction upon
        // entry to the enclave.
        // In Linux, the new value of FS persists until it is explicitly
        // restored below. Windows however restores FS to the original value
        // unexpectedly (say when the thread is suspended/resumed).
        // This leads to access violations since features like stack-protector
        // and thread-local storage use the FS register; but its value has been
        // restored by Windows. To let the enclave chug along in simulation
        // mode, we prepend a vectored exception handler that resets the FS
        // register to the desired value. See host/sgx/create.c.
        oe_set_fs_register_base(
            (void*)(enclave->start_address + sgx_tcs->fsbase));
        oe_set_gs_register_base(
            (void*)(enclave->start_address + sgx_tcs->gsbase));

        // For parity with oe_enter, see comments there.
        if (oe_is_avx_enabled)
            OE_VZEROUPPER;

        // Simulate the cssa set by EENTER
        func = oe_get_func_from_call_arg1(arg1);
        if (func == OE_ECALL_VIRTUAL_EXCEPTION_HANDLER)
            cssa = 1;
        else
            cssa = 0;

        // Obtain ssa_gpr based on cssa
        ssa_gpr =
            (sgx_ssa_gpr_t*)(ssa + OE_PAGE_SIZE * cssa + OE_SGX_GPR_OFFSET_FROM_SSA);

        // Define register bindings and initialize the registers.
        // See oe_enter for ENCLU contract.
        OE_DEFINE_REGISTER(rax, cssa);
        OE_DEFINE_REGISTER(rbx, tcs);
        OE_DEFINE_REGISTER(rcx, 0 /* filled in asm snippet */);
        OE_DEFINE_REGISTER(rdx, &ecall_context);
        OE_DEFINE_REGISTER(rdi, arg1);
        OE_DEFINE_REGISTER(rsi, arg2);
        OE_DEFINE_FRAME_POINTER(rbp, OE_FRAME_POINTER_VALUE);

        asm volatile("fxsave %[fx_state] \n\t"   // Save floating point state
                     "pushfq \n\t"               // Save flags
                     "mov %%rsp, %[ursp] \n\t"   // Save rsp to the SSA.URSP
                     "mov %%rbp, %[urbp] \n\t"   // Save rbp to the SSA.URBP
                     "lea 1f(%%rip), %%rcx \n\t" // Load return address in rcx
                     "mov 72(%%rbx), %%r8 \n\t"  // Load enclave entry point
                     "jmp *%%r8  \n\t"           // Jump to enclave entry point
                     "1: \n\t"
                     "popfq \n\t"               // Restore flags
                     "fxrstor %[fx_state] \n\t" // Restore floating point state
                     : OE_ENCLU_REGISTERS,
                       [ursp] "=m"(ssa_gpr->ursp),
                       [urbp] "=m"(ssa_gpr->urbp)
                     : [fx_state] "m"(fx_state)OE_FRAME_POINTER
                     : OE_SIMULATE_ENCLU_CLOBBERED_REGISTERS);

        // Update arg1 and arg2 with outputs returned by the enclave.
        arg1 = rdi;
        arg2 = rsi;

        // Restore FS/GS registers upon returning from the enclave.
        oe_set_fs_register_base(host_fs);
        oe_set_gs_register_base(host_gs);

        // Make an OCALL if needed.
        oe_code_t code = oe_get_code_from_call_arg1(arg1);
        if (code == OE_CODE_OCALL)
        {
            __oe_host_stack_bridge(
                arg1, arg2, &arg1, &arg2, tcs, enclave, &ecall_context);
        }
        else
            break;
    }

    *arg3 = arg1;
    *arg4 = arg2;
}
