# Usage

> ⚠️ Read [DISCLAIMER.md](../DISCLAIMER.md) before deploying against a
> real vehicle.

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

3. Add a `trace.ini` next to the DLLs (see [Configuration](#configuration)):

   ```ini
   [trace]
   enabled=1
   ```

4. Verify the deployed copy carries version metadata (blank values
   mean the target app will reject it; see
   [BUILDING.md](BUILDING.md#version-resource)):

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

## Configuration

`trace.ini` is read once, when the proxy DLL loads. All keys live in the
`[trace]` section.

| Key | Default | Meaning |
|-----|---------|---------|
| `enabled` | `1` | `0` disables logging without removing the proxy; calls are still forwarded. |
| `dll_suffix` | `_orig` | Suffix appended to the proxy's own filename to find the real driver. Only set this if you renamed the original differently. |
| `flush_ms` | `0` | `0` flushes after every log line (maximum fidelity). Higher values (e.g. `250`) reduce disk sync overhead during latency-sensitive sessions, at the cost of losing the unflushed tail if the host process dies. |
| `min_free_mb` | `200` | Logging refuses to start below this much free disk space. Fail-open: the proxy keeps forwarding traffic, just without logging. |

Full example:

```ini
[trace]
enabled=1
dll_suffix=_orig
flush_ms=0
min_free_mb=200
```

## Reading a trace

Traces are written to `traces/` inside the app folder, one file per
process, timestamped and PID-suffixed. The file is opened with shared
read access so it can be tailed live:

```
Get-Content -Wait traces\trace_<...>.log
```

There are two kinds of line:

```
[HH:MM:SS.mmm +DDDDDDus] <call>(<args>) -> <return code> <out params>
MSG <TX|RX> ch=<channel> proto=<ProtocolID> txflags=<TxFlags> rxstatus=<RxStatus> ts=<device timestamp> len=<DataSize> edi=<ExtraDataIndex> data=<hex bytes>
```

- **Call lines** carry local wall-clock time and the microsecond delta
  since the previous call line. Failed calls are logged with their error
  code.
- **`MSG` lines** carry the full payload of every TX/RX message (up to
  4128 bytes) along with the raw `PASSTHRU_MSG` fields. TX messages are
  written before the forwarded `PassThruWriteMsgs` call line; RX messages
  follow the `PassThruReadMsgs` call line that returned them.
- **`[SUSPECT]`** prefixes a `MSG` line whose `DataSize` exceeds 4128 or
  whose `ExtraDataIndex` exceeds `DataSize`. This usually means the
  application and driver disagree on the struct layout.
- **`Ioctl` SET_CONFIG / GET_CONFIG** calls are followed by one indented
  line per parameter.

Expect large files during flash sessions.

## Handling traces

Do not commit or share traces. They may contain security access
seed/key material from your ECU.

## Reverting

Delete the proxy copy and rename `<vendor_name>_J2534_orig.dll` back to
`<vendor_name>_J2534.dll`. The application is then completely stock again.
