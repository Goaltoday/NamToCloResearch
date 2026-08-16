# Portable HTUSBTools runtime (v2.6.7-portable)

This branch is based directly on v2.6.7. The NAM/CLO conversion and refinement algorithms are unchanged.

## Why this exists
On one validated Windows PC, HTUSBTools.dll crashed asynchronously with access violation 0xC0000005 when NamToClo ran as a normal user, but succeeded when the application was elevated. The minidump located the crash inside HTUSBTools.dll after `namConvertCloData()` had returned.

## Change
NamToClo no longer asks the proprietary runtime to execute from the application directory. For each conversion it:

1. Creates the existing per-job directory under the user's temporary directory.
2. Recursively copies `runtime\ampero` into `<job>\htusb_runtime`.
3. Loads `HTUSBTools.dll` from that private writable copy.
4. Starts the worker with the job directory as its current working directory.
5. Sets the worker's `TEMP` and `TMP` variables to the same job directory before loading HTUSBTools.dll.
6. Keeps NAM, WAV, CLO and any implicit auxiliary files inside a location writable by the current user.
7. Deletes the job directory at the end as before.

No UAC elevation or administrator manifest is used.

## Expected behavior
The existing runtime folder remains read-only from the worker's point of view; it is only used as the source for the private copy. This should make behavior independent of ACL differences on folders such as `C:\NAMtoCLO`, `C:\Program Files`, Downloads, etc.
