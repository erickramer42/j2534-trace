# j2534-trace

> ⚠️ **WARNING:** This software may permanently damage your vehicle.  
> See [DISCLAIMER.md](DISCLAIMER.md) for full terms before use.

A J2534 proxy DLL that intercepts communication between a J2534 application
and its vehicle interface, logging all traffic to files.

## Requirements

- Visual Studio 2019 or newer Build Tools (Desktop C++ workload).
  Tested with VS2019, VS2022, and VS2026 toolsets. Any MSVC toolchain
  since VS2015 produces ABI-compatible binaries (vcruntime140).
- CMake 3.24 or newer (needed for `--fresh` and current VS generator
  strings; 3.20 works with older generators)
- 32-bit Python for the test harness (the DLLs are Win32/x86)

Check your Python bitness (must print 32):

```
py -3.12 -c "import struct; print(struct.calcsize('P')*8)"
```

## Build

```
cmake -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

Adjust the `-G` string to your installed Visual Studio version
(e.g. `"Visual Studio 16 2019"`, `"Visual Studio 17 2022"`).
When switching generators on an existing build directory, add `--fresh`
to avoid a cache mismatch error:

```
cmake -B build -G "<generator>" -A Win32 --fresh
```

Win32 is required. The DLL must match the bitness of the target
application. Verify with:

```
dumpbin /headers build/proxy/trace.dll | findstr machine
```

(must print `x86`)

Build outputs:

```
build/proxy/trace.dll          deployable proxy
test/vendor_J2534.dll          proxy copy placed in the test rig
test/vendor_J2534_orig.dll     mock driver for testing
test/trace.ini                 [trace] enabled=1
```

## Test

To run tests after building:

```
py -3.12 test/harness.py
```

Checks performed:

1. Banner and log file created
2. API round trips (Open, ReadVersion, Connect, StartMsgFilter)
3. UDS echo (TX/RX with intact hex)
4. Large payload (3000 bytes logged in full)
5. Timeout path (error return, no data logged)
6. Unresolved export (returns 0xE2, no crash)
7. Ioctl SET/GET_CONFIG round trip
8. Thread stress (3 workers, no torn log lines)
9. Disabled mode (enabled=0, no trace file, calls still work)
10. Missing real DLL (graceful 0xE2 failure, no crash)
11. Periodic message logged (StartPeriodicMsg traffic reaches the log)
12. Error code propagation (invalid channel returns 0xC1, logged)

Exit code 0 means all passed. Failures print with FAIL and the exit code
is 1.

Limitation: the harness validates proxy plumbing (logging, threading,
error handling, path resolution). It cannot validate the ABI against the
real driver. That can only be confirmed in a live session.

## Deployment

In the target application folder:

1. Rename the original driver:

   ```
   <vendor_name>_J2534.dll  ->  <vendor_name>_J2534_orig.dll
   ```

2. Copy the built proxy in, named as the original:

   ```
   build/proxy/trace.dll  ->  <vendor_name>_J2534.dll
   ```

3. Add a `trace.ini` next to the DLLs:

   ```ini
   [trace]
   enabled=1
   ```

Set `enabled=0` to disable logging without removing the proxy.

4. Verify the deployed copy carries version metadata (blank values
   mean the target app will reject it):

   ```
   (Get-Item <vendor_name>_J2534.dll).VersionInfo | Format-List FileVersion
   ```

Expected folder layout after deployment:

```
/
├── <vendor_name>_J2534.dll          <- our proxy
├── <vendor_name>_J2534_orig.dll     <- original driver
├── trace.ini                        <- [trace] enabled=1
└── traces/                          <- created on first launch
    └── trace_YYYYMMDD_HHMMSS_mmm_pidNNNN.log
```

## To revert

Delete the proxy copy and rename `<vendor_name>_J2534_orig.dll` back to 
`<vendor_name>_J2534.dll`. The application is then completely stock again.

## Notes

- Traces are written to `traces/` inside the app folder, one file per
  process, timestamped and PID-suffixed. The file is opened with shared
  read access so it can be tailed live (`Get-Content -Wait` in
  PowerShell).
- Log lines carry microsecond delta timestamps (`+[us]`) for
  inter-frame timing analysis.
- Messages with implausible DataSize/ExtraDataIndex values are flagged
  `SUSPECT` (possible ABI mismatch indicator).
- The full payload of every TX/RX message is logged (up to 4128 bytes).
  Expect large files during flash sessions.
- Do not commit traces. They may contain security access seed/key
  material from your ECU.
- Generated test artifacts (`*.dll`, `traces/`, `trace.ini`) are gitignored.
  The test rig is rebuilt by CMake and never committed.
- The proxy carries a version resource (`proxy/resource.rc`) mirroring the
  original driver's metadata. The target application validates this and
  rejects DLLs with blank or missing version info ("not supported").
  If the target driver version changes, update FILEVERSION/PRODUCTVERSION
  and the string values in `proxy/resource.rc` to match, or the same check
  fails again.
