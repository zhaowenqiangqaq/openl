// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <Windows.h>
#include <openenclave/internal/debugrt/host.h>
#include <openenclave/internal/trace.h>
#include <stdlib.h>
#include "../../hostthread.h"

static struct
{
    oe_once_type once;
    HMODULE hmodule;
    oe_result_t (*notify_enclave_created)(oe_debug_enclave_t* enclave);
    oe_result_t (*notify_enclave_terminated)(oe_debug_enclave_t* enclave);
    oe_result_t (*push_thread_binding)(
        oe_debug_enclave_t* enclave,
        struct _sgx_tcs* tcs);
    oe_result_t (*pop_thread_binding)(void);
    oe_result_t (*notify_module_loaded)(oe_debug_module_t* module);
    oe_result_t (*notify_module_unloaded)(oe_debug_module_t* module);
} _oedebugrt;

static void get_debugrt_function(const char* name, FARPROC* out)
{
    *out = GetProcAddress(_oedebugrt.hmodule, name);
    if (*out == NULL)
    {
        OE_TRACE_FATAL("Could not find function %s in oedebugrt.dll", name);
    }
}

static void load_oedebugrt(void)
{
    if (_oedebugrt.hmodule != NULL)
    {
        OE_TRACE_WARNING("oedebugrt.dll has already been loaded.");
        return;
    }

    /* Search for oedebugrt.dll first in the application folder and then the
     * system32 folder.*/
    if (_oedebugrt.hmodule == NULL)
    {
        _oedebugrt.hmodule = LoadLibraryExA(
            "oedebugrt.dll",
            NULL, /* reserved */
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }

    if (_oedebugrt.hmodule != NULL)
    {
        get_debugrt_function(
            "oe_debug_notify_enclave_created",
            (FARPROC*)&_oedebugrt.notify_enclave_created);
        get_debugrt_function(
            "oe_debug_notify_enclave_terminated",
            (FARPROC*)&_oedebugrt.notify_enclave_terminated);
        get_debugrt_function(
            "oe_debug_push_thread_binding",
            (FARPROC*)&_oedebugrt.push_thread_binding);
        get_debugrt_function(
            "oe_debug_pop_thread_binding",
            (FARPROC*)&_oedebugrt.pop_thread_binding);
        get_debugrt_function(
            "oe_debug_notify_module_loaded",
            (FARPROC*)&_oedebugrt.notify_module_loaded);
        get_debugrt_function(
            "oe_debug_notify_module_unloaded",
            (FARPROC*)&_oedebugrt.notify_module_unloaded);

        OE_TRACE_INFO(
            "oedebugrtbridge: Loaded oedebugrt.dll. Debugging is available.\n");
    }
    else
    {
        DWORD error = GetLastError();
        OE_TRACE_INFO(
            "oedebugrtbridge: LoadLibraryEx on oedebugrt.dll error"
            "= %#x. Debugging is unavailable.\n",
            error);
    }
}

static void cleanup(void)
{
    if (_oedebugrt.hmodule != NULL)
    {
        FreeLibrary(_oedebugrt.hmodule);
    }
}

static void initialize()
{
    oe_once(&_oedebugrt.once, &load_oedebugrt);
    atexit(&cleanup);
}

oe_result_t oe_debug_notify_enclave_created(oe_debug_enclave_t* enclave)
{
    initialize();
    if (_oedebugrt.notify_enclave_created)
        return _oedebugrt.notify_enclave_created(enclave);

    return OE_OK;
}

oe_result_t oe_debug_notify_enclave_terminated(oe_debug_enclave_t* enclave)
{
    if (_oedebugrt.notify_enclave_terminated)
        return _oedebugrt.notify_enclave_terminated(enclave);

    return OE_OK;
}

oe_result_t oe_debug_push_thread_binding(
    oe_debug_enclave_t* enclave,
    struct _sgx_tcs* tcs)
{
    if (_oedebugrt.push_thread_binding)
        return _oedebugrt.push_thread_binding(enclave, tcs);

    return OE_OK;
}

oe_result_t oe_debug_pop_thread_binding()
{
    if (_oedebugrt.pop_thread_binding)
        return _oedebugrt.pop_thread_binding();

    return OE_OK;
}

oe_result_t oe_debug_notify_module_loaded(oe_debug_module_t* module)
{
    if (_oedebugrt.notify_module_loaded)
        return _oedebugrt.notify_module_loaded(module);

    return OE_OK;
}

oe_result_t oe_debug_notify_module_unloaded(oe_debug_module_t* module)
{
    if (_oedebugrt.notify_module_unloaded)
        return _oedebugrt.notify_module_unloaded(module);

    return OE_OK;
}
