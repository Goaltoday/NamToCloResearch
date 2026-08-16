# HTUSBTools crash diagnostics — v2.6.7-DIAG2

This build is based on the working v2.6.7 conversion/refinement path. It does not intentionally change the conversion algorithm.

Additional diagnostics:

- Keeps the existing `*_HTUSBTools_DIAGNOSTIC.log`.
- Installs a vectored exception handler and an unhandled-exception filter in the isolated worker process.
- For serious worker exceptions such as `0xC0000005` (access violation), writes `*_HTUSBTools_CRASH.dmp` beside the normal conversion outputs.
- Logs the faulting instruction address, module containing that address, thread ID, and for access violations whether the fault was a read/write/execute and the target address.
- Logs basic Windows/CPU environment information.

## Test

Run the same conversion that fails. After the error, send both:

1. `*_HTUSBTools_DIAGNOSTIC.log`
2. `*_HTUSBTools_CRASH.dmp`

If the dump file is generated, do not rename it before sending it together with its matching log.
