// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/corelibc/stdlib.h>

#if !defined(OE_NEED_STDC_NAMES)
#define OE_NEED_STDC_NAMES
#define __UNDEF_OE_NEED_STDC_NAMES
#endif
#if defined(OE_INLINE)
#undef OE_INLINE
#define OE_INLINE
#endif
#include <openenclave/corelibc/bits/malloc.h>
#if defined(__UNDEF_OE_NEED_STDC_NAMES)
#undef OE_NEED_STDC_NAMES
#undef __UNDEF_OE_NEED_STDC_NAMES
#endif

/*
 * __libc_malloc is used in in ldso, aio, thread, time, ldso, locale,
 * exit, and malloc. __libc_malloc() is defined in
 * musl/src/malloc/lite_malloc.c
 */
OE_WEAK_ALIAS(malloc, __libc_malloc);

/*
 * __libc_free is used by MUSL locale functions.
 */
OE_WEAK_ALIAS(free, __libc_free);
