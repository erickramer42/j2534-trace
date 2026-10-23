# Testing

## Running

After [building](BUILDING.md):

```
py -3.12 test/harness.py
```

The harness requires 32-bit Python and exits immediately with an error
otherwise. Exit code 0 means all checks passed. Failures print as
`FAIL #N: <name>  :: <detail>` with the failing check number.

## How the harness works

- **Test rig.** CMake copies the built proxy to `test/vendor_J2534.dll`
  and builds the mock driver as `test/vendor_J2534_orig.dll`, mirroring a
  real deployment. Set `J2534_TEST_DIR` to run against a rig elsewhere.
  The rig files (`*.dll`, `traces/`, `trace.ini`) are gitignored and are
  never committed.
- **Mock vendor driver** ([test/mock_driver/](../test/mock_driver/)). This
  is a C++ DLL with the real J2534 calling convention. It echoes writes back
  as UDS-style positive responses, stores SET_CONFIG values so GET_CONFIG
  can round-trip them, queues periodic messages, rejects unknown channels,
  and deliberately omits `PassThruReadVoltage` to exercise the
  missing-export path.
- **Assertions on the log, not just return codes.** Most checks call the
  proxy through `ctypes` and then parse the trace file, confirming that
  the forwarded result *and* the logged record are both correct.
- **Fresh processes for load-time behavior.** `trace.ini` is read in
  `DllMain`, so the disabled-mode, missing-driver, latency, and low-disk
  checks each load the proxy in a new Python subprocess with a modified
  config or rig. The original `trace.ini` and driver are restored
  afterward.

## Checks

20 total, numbered in output:

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

## Limitations

The harness validates proxy plumbing: logging, threading, error
handling, and path resolution. It cannot validate the ABI against a real
vendor driver. That can only be confirmed in a live session.
