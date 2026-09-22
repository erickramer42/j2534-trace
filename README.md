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

Checks performed (20 total, numbered in output):

| # | Check | Covers |
|---|-------|--------|
| 1 | banner + log file created | Logger init on DLL load |
| 2–5 | Open / ReadVersion / Connect / StartMsgFilter | Basic API round trips |
| 6–7 | UDS echo TX/RX logged | Write/read capture with intact hex |
| 8 | 3000-byte payload fully logged | No truncation on large messages |
| 9 | timeout path | Error return, no phantom RX logged |
| 10 | unresolved export | Missing export → 0xE2, logged, no crash |
| 11–12 | Ioctl SET/GET_CONFIG decode | SCONFIG round-trip |
| 13 | thread stress | 3 workers, no torn log lines |
| 14 | disabled mode | enabled=0 → no trace file, calls succeed |
| 15 | missing real DLL | Graceful 0xE2 failure, no crash |
| 16 | periodic message logging | StartPeriodicMsg traffic logged |
| 17 | error code propagation | Invalid channel error returned + logged |
| 18 | concurrent filters | 4 threads × 200 filters, correct data each |
| 19 | logging latency < 2ms/call | Measured overhead of enabled logging |
| 20 | low-disk guard | min_free_mb → fail open, no trace file |

Exit code 0 means all passed. Failures print as `FAIL #N: <name>` with
the failing check number.

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
   ; dll_suffix: suffix appended to the proxy's own filename to find the
   ; real driver (default _orig). Only set this if you renamed differently.
   dll_suffix=_orig
   ; flush_ms: 0 = flush after every log line (default, max fidelity).
   ; Higher values (e.g. 250) reduce disk sync overhead during
   ; latency-sensitive sessions, at the cost of losing the un-flushed
   ; tail if the host process dies.
   flush_ms=0
   ; min_free_mb: logging refuses to start below this much free disk
   ; space (default 200). Fail-open: the proxy keeps forwarding traffic,
   ; just without logging.
   min_free_mb=200
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
- The proxy's version resource is generated at build time from
  `proxy/resource.rc.in`. Vendor-mimicking values live in
  `proxy/resource_values.local.rc`, which is **gitignored** — if the
  target app validates driver version info and rejects mismatched DLLs
  ("not supported"), regenerate that file from the current vendor DLL's
  metadata and rebuild. The absence of the local file builds neutral
  version info.
- If any log write fails mid-session (disk full being the realistic
  case), logging shuts down permanently for that process and the proxy
  continues forwarding without capture — a lost log is recoverable, a
  stalled call path during a flash session is not.
- The proxy build enables MSVC /W4; fix or consciously accept any new
  warning rather than suppressing it.