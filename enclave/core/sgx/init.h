// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#ifndef OE_INIT_H
#define OE_INIT_H

#include <openenclave/enclave.h>
#include "../init_fini.h"
#include "td.h"

void oe_initialize_enclave(oe_sgx_td_t* td);

bool oe_apply_relocations(void);

#endif /* OE_INIT_H */
