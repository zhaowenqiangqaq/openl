Enforcing full serialization in EDL
===================================

Motivation
----------

Currently EDL only provides partial support for parameter serialization. For
example, the following EDL defines ECALLs whose parameters can only be
partially serialized.

```c++
enclave
{
    /* Include struct iovec (foreign structure) */
    #include <sys/uio.h>

    trusted
    {
        public int send(
            [in, count=iovcnt] const struct iovec* iov,
            size_t iovcnt);

        public int recv(
            [out, count=iovcnt] struct iovec* iov,
            size_t iovcnt);
    };
};
```

The generator produces code that serializes the array of **iovec**
structures, but it cannot serialize the buffer referenced by the structure
itself. Recall the definition of the **iovec** structure.

```c++
struct iovec
{
    void* iov_base;
    size_t iov_len;
};
```

Consequently, these ECALLs pass pointers to untrusted memory (**iov_base**) to
the enclave developer's **send** and **recv** implementations.

The exposure of untrusted pointers to the enclave implementation poses two main
problems.

- Implementations are vulnerable to coding errors that may introduce security
  vulnerabilities.
- The enclave application is not portable to trusted hardware environments
  that do not support or have not enabled shared memory (e.g., TrustZone).

Some potential security vulnerabilities include:

- Time-of-check, Time-of-use errors (TOC-TOU).
- Unwittingly writing secrets onto host memory.
- Overwrite attacks where **iov_base** points to enclave memory.

EDL provides a *deep-copy* feature for fully serializing parameters as shown
below.

```c++
enclave
{
    struct local_iovec
    {
        [size=iov_len]
        void* iov_base;
        size_t iov_len;
    };

    trusted
    {
        public int send(
            [in, count=iovcnt] const struct local_iovec* iov,
            size_t iovcnt);

        public int recv(
            [out, count=iovcnt] struct local_iovec* iov,
            size_t iovcnt);
    };
};
```

This example introduces a local structure definition (**local_iovec**) which
provides an annotation allowing **iov_base** to be serialized.

See [Support Deep Copy of Variable-Length Out Parameters](/docs/DesignDocs/DeepCopyOutParameters.md)
for more detail.

The following EDL considers a second scenario that implicitly passes untrusted
memory into the trusted implementation.

```c++
enclave
{
    untrusted
    {
        struct widget* get_widget();
    };
};
```

The **get_widget** OCALL returns a pointer to untrusted memory. This is overcome
by redefining the OCALL as follows.

```c++
enclave
{
    untrusted
    {
        int get_widget([out] struct widget* widget);
    };
};
```

Although EDL provides sufficient mechanisms for performing full serialization,
the generator does not currently enforce these mechanisms. As shown above,
without using proper syntax or annotations, the generator may still produce edge
routines that implicitly copy untrusted memory references into the enclave.
This results in potential security flaws and non-portable code.

To overcome these problems, the generator can be modified to warn when an EDL
specification cannot be fully serialized.

User Experience
---------------

When using the generator, the user will see warnings when the following are
encountered.

- Foreign structures
- Non-serializable local structures (lacking the appropriate annotations)
- Pointer return values

The user may wish to treat these warnings as errors using an option similar to
GCC's **-Werror** option.

Specification
-------------

The generator should be modified to produce the warnings mentioned above, via
these flags:

- `-Wforeign-struct`
- `-Wptr-in-local-struct`
- `-Wptr-return-value`

While adding these warnings, the existing warnings that are able to be ignored
(not that they ought to be) should be made togglable with the following flags:

- `-Wnon-portable-types`
- `-Wsigned-size-or-count-types`
- `-Wcalling-convention`
- `-Wallow-list`

Along with `-Wno-` versions of all of the above to disable them, `-Wall` to turn
them all on and `-Werror` to treat them as errors (causing `oeedger8r` to exit
with a non-zero exit code).

The warnings should not be on by default. However, the Open Enclave build should
be modified to use `-Wall -Werror` in all places.

The existing "warnings" which are actually errors that cannot be ignored should
be renamed as "errors" in the implementation as well as in their messages. This
includes:

- Simultaneous size and count parameters
- The `--use_prefix` flag
- `private` function specifier
- Switchless ecalls
- "dllimport"

Alternatives
------------

We considered introducing complimentary interface definition languages such
as Google protobufs and others. We are open to having such alternatives in
the future.

Authors
-------

- Mike Brasher (mikbras)
- Andrew Schwartzmeyer (andschwa)
