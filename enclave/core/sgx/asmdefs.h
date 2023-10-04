// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifndef _ASMDEFS_H
#define _ASMDEFS_H

#ifndef __ASSEMBLER__
#include <openenclave/enclave.h>
#include <openenclave/internal/sgx/td.h>
#endif

#define ENCLU_EGETKEY 1
#define ENCLU_EENTER 2
#define ENCLU_EEXIT 4

#define PAGE_SIZE 4096
#define STATIC_STACK_SIZE 8 * 100
#define OE_WORD_SIZE 8

/* Defined in oe_result_t (result.h) */
#define CODE_ENCLAVE_ABORTING 0x13

/* Defined in exception.h */
#define CODE_EXCEPTION_CONTINUE_EXECUTION 0xFFFFFFFF

/* Assembly code cannot use enum values directly,
 * define them here to match oe_td_state_t in
 * internal/sgx/td.h */
#define TD_STATE_NULL 0
#define TD_STATE_ENTERED 1
#define TD_STATE_RUNNING 2
#define TD_STATE_FIRST_LEVEL_EXCEPTION_HANDLING 3
#define TD_STATE_SECOND_LEVEL_EXCEPTION_HANDLING 4
#define TD_STATE_EXITED 5
#define TD_STATE_ABORTED 6

/* Set the max signal number based on Linux (i.e., SIGRTMAX) */
#define MAX_SIGNAL_NUMBER 64

/* Use GS register if this flag is set */
#ifdef __ASSEMBLER__
#define OE_ARG_FLAG_GS 0x0001
#endif

/* Padding needed to ensure that `callsite` fields's offset matches what Windows
   debuggers expect */
#define td_callsites_padding 24

/* Offsets into oe_sgx_td_t structure */
#define td_self_addr 0
#define td_last_sp 8
#define td_magic 168
#define td_depth (td_magic + 8)
#define td_eenter_rax (td_depth + 8)
#define td_host_rcx (td_eenter_rax + 8)
#define td_oret_func (td_host_rcx + 8)
#define td_oret_arg (td_oret_func + 8)
#define td_callsites (td_oret_arg + 8 + td_callsites_padding)
#define td_simulate (td_callsites + 8)
#define td_host_ecall_context (td_simulate + 8)
#define td_host_previous_ecall_context (td_host_ecall_context + 8)
#define td_exception_handler_stack (td_host_previous_ecall_context + 8)
#define td_exception_handler_stack_size (td_exception_handler_stack + 8)
#define td_exception_handler_stack_bitmap (td_exception_handler_stack_size + 8)
#define td_state (td_exception_handler_stack_bitmap + 8)
#define td_previous_state (td_state + 8)
#define td_exception_nesting_level (td_previous_state + 8)
#define td_host_signal_unmasked (td_exception_nesting_level + 8)
#define td_is_handling_host_signal (td_host_signal_unmasked + 8)
#define td_host_signal (td_is_handling_host_signal + 8)
#define td_host_signal_bitmask (td_host_signal + 8)

#define oe_exit_enclave __morestack
#ifndef __ASSEMBLER__
/* This function exits the enclave by initiating the ENCLU-EEXIT instruction.
 * It should not be confused with oe_exit(), which maps to the standard-C
 * exit() function defined in <openenclave/corelibc/stdlib.h>.
 */
void oe_exit_enclave(uint64_t arg1, uint64_t arg2) OE_NO_RETURN;

/* This is the actual implementation of eexit */
void oe_asm_exit(
    uint64_t arg1,
    uint64_t arg2,
    oe_sgx_td_t* td,
    uint64_t aborting) OE_NO_RETURN;
#endif

#ifndef __ASSEMBLER__
void __oe_handle_main(
    uint64_t arg1,
    uint64_t arg2,
    uint64_t cssa,
    void* tcs,
    uint64_t* output_arg1,
    uint64_t* output_arg2);

void oe_exception_dispatcher(void* context);
#endif

#endif /* _ASMDEFS_H */
