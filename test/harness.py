#!/usr/bin/env python
"""End-to-end harness for the J2534 proxy against the mock driver.

Requires 32-bit Python (the proxy and mock are Win32/x86 builds).
Rig layout (defaults to this script's directory; override with J2534_TEST_DIR):
    test/vendor_J2534.dll        <- proxy build (copied by CMake)
    test/vendor_J2534_orig.dll   <- mock build, renamed (copied by CMake)
    test/trace.ini               <- created by the harness
    test/traces/                  <- created by the proxy at load
"""
import ctypes, glob, os, random, re, shutil, struct, subprocess, sys, threading
from ctypes import POINTER, byref, c_long, c_ulong, create_string_buffer

TEST_DIR   = os.environ.get(
    "J2534_TEST_DIR",
    os.path.dirname(os.path.abspath(__file__))
)
PROXY_DLL  = os.path.join(TEST_DIR, "vendor_J2534.dll")
REAL_DLL   = os.path.join(TEST_DIR, "vendor_J2534_orig.dll")
TRACE_INI  = os.path.join(TEST_DIR, "trace.ini")
TRACES_DIR = os.path.join(TEST_DIR, "traces")

RESULTS = []
def check(name, cond, detail=""):
    RESULTS.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else "  :: " + detail))

class PASSTHRU_MSG(ctypes.Structure):
    _fields_ = [("ProtocolID", c_ulong), ("RxStatus", c_ulong),
                ("TxFlags", c_ulong), ("Timestamp", c_ulong),
                ("DataSize", c_ulong), ("ExtraDataIndex", c_ulong),
                ("Data", ctypes.c_ubyte * 4128)]

class SCONFIG(ctypes.Structure):
    _fields_ = [("Parameter", c_ulong), ("Value", c_ulong)]
class SCONFIG_LIST(ctypes.Structure):
    _fields_ = [("NumOfParams", c_ulong), ("ConfigPtr", POINTER(SCONFIG))]

def load_lib():
    lib = ctypes.CDLL(PROXY_DLL)
    lib.PassThruOpen.argtypes            = [ctypes.c_char_p, POINTER(c_ulong)]
    lib.PassThruConnect.argtypes         = [c_ulong, c_ulong, c_ulong, c_ulong, POINTER(c_ulong)]
    lib.PassThruStartMsgFilter.argtypes  = [c_ulong, c_ulong, POINTER(PASSTHRU_MSG),
                                            POINTER(PASSTHRU_MSG), POINTER(PASSTHRU_MSG), POINTER(c_ulong)]
    lib.PassThruWriteMsgs.argtypes       = [c_ulong, POINTER(PASSTHRU_MSG), POINTER(c_ulong), c_ulong]
    lib.PassThruReadMsgs.argtypes        = [c_ulong, POINTER(PASSTHRU_MSG), POINTER(c_ulong), c_ulong]
    lib.PassThruReadVersion.argtypes    = [c_ulong, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.PassThruIoctl.argtypes           = [c_ulong, c_ulong, ctypes.c_void_p, ctypes.c_void_p]
    for n in ("PassThruOpen", "PassThruClose", "PassThruConnect", "PassThruDisconnect",
              "PassThruReadMsgs", "PassThruWriteMsgs", "PassThruStartMsgFilter",
              "PassThruReadVersion", "PassThruGetLastError", "PassThruIoctl",
              "PassThruReadVoltage"):
        getattr(lib, n).restype = c_long
    return lib

def log_text():
    files = glob.glob(os.path.join(TRACES_DIR, "*.log"))
    if not files: return ""
    with open(max(files, key=os.path.getmtime), "r") as f:
        return f.read()

def make_msg(proto, data, edi=None):
    m = PASSTHRU_MSG()
    m.ProtocolID, m.DataSize = proto, len(data)
    m.ExtraDataIndex = edi if edi is not None else len(data)
    for i, b in enumerate(data): m.Data[i] = b
    return m

def subproc_probe(script):
    """Run a fresh process that loads the proxy and runs a snippet."""
    code = ("import ctypes;from ctypes import byref,c_ulong;"
            "lib=ctypes.CDLL(r'%s');" % PROXY_DLL) + script
    return subprocess.call([sys.executable, "-c", code])

def main():
    if struct.calcsize("P") * 8 != 32:
        sys.exit("ERROR: 32-bit Python required (this is %d-bit)" % (struct.calcsize("P") * 8))
    if not os.path.exists(PROXY_DLL):
        sys.exit("ERROR: proxy not found at %s" % PROXY_DLL)

    # Clean slate: the log file is created at DLL load, so clear first
    shutil.rmtree(TRACES_DIR, ignore_errors=True)
    lib = load_lib()

    # 1. banner
    t = log_text()
    check("banner + log file created", "Proxy loaded, logging ENABLED" in t, t[:200])

    # 2. open / version / connect / filter
    dev = c_ulong(0)
    check("PassThruOpen", lib.PassThruOpen(None, byref(dev)) == 0 and dev.value == 1)
    fw, dll, api = (create_string_buffer(80) for _ in range(3))
    lib.PassThruReadVersion(dev, fw, dll, api)
    check("ReadVersion buffers", fw.value == b"1.0-MOCK" and api.value == b"4.04")
    ch = c_ulong(0)
    check("PassThruConnect", lib.PassThruConnect(dev, 6, 0, 500000, byref(ch)) == 0 and ch.value >= 1)

    mask, pat, fc = (PASSTHRU_MSG() for _ in range(3))
    mask.DataSize = pat.DataSize = 4
    for m in (mask, pat):
        m.Data[0] = 0x07; m.Data[1] = 0xE0
    fid = c_ulong(0)
    check("StartMsgFilter", lib.PassThruStartMsgFilter(ch, 1, byref(mask), byref(pat),
                                                        byref(fc), byref(fid)) == 0 and fid.value >= 1)

    # 3. UDS echo round-trip
    num = c_ulong(1)
    lib.PassThruWriteMsgs(ch, byref(make_msg(6, [0x10, 0x03])), byref(num), 1000)
    lib.PassThruReadMsgs(ch, byref(make_msg(6, [0])), byref(num), 1000)
    t = log_text()
    check("TX request logged", "10 03" in t)
    check("RX positive echo logged", "50 03" in t)

    # 4. large payload — full 3000 bytes, no truncation
    rnd = random.Random(42)
    payload = bytes(rnd.randrange(256) for _ in range(3000))
    lib.PassThruWriteMsgs(ch, byref(make_msg(6, payload)), byref(num), 1000)
    t = log_text()
    big = [l for l in t.splitlines() if "MSG TX" in l and "len=3000" in l]
    tok = big[-1].split("data=", 1)[1].split() if big else []
    check("3000-byte payload fully logged", len(tok) == 3000 and "..." not in big[-1],
          "tokens=%d" % len(tok))

    # 5. drain queue, then timeout: num=0, NO new RX line
    while True:
        num.value = 1
        if lib.PassThruReadMsgs(ch, byref(PASSTHRU_MSG()), byref(num), 10) != 0:
            break
    rx_before = log_text().count("MSG RX")
    num.value = 1
    rc = lib.PassThruReadMsgs(ch, byref(PASSTHRU_MSG()), byref(num), 10)
    check("timeout: rc!=0, num=0, no RX logged",
          rc != 0 and num.value == 0 and log_text().count("MSG RX") == rx_before)

    # 6. unresolved export (mock omits ReadVoltage)
    v = c_ulong(0)
    rc = lib.PassThruReadVoltage(dev, byref(v))
    t = log_text()
    check("unresolved export returns 0xE2, logged, no crash",
          rc == 0xE2 and "GetProcAddress failed for 'PassThruReadVoltage'" in t)

    # 7. Ioctl SET/GET round-trip via mock store
    params = (SCONFIG * 1)()
    params[0].Parameter, params[0].Value = 0x02, 1          # loopback param id
    slist = SCONFIG_LIST(1, params)
    lib.PassThruIoctl(dev, 0x02, ctypes.byref(slist), None)   # SET
    lib.PassThruIoctl(dev, 0x01, None, ctypes.byref(slist))    # GET
    t = log_text()
    check("SET_CONFIG decoded in log", "SET_CONFIG param=0x2 value=0x1" in t)
    check("GET_CONFIG decoded in log", "GET_CONFIG param=0x2 value=0x1" in t)

    # 8. thread stress — no torn lines
    def worker():
        ln = c_ulong(1)
        for _ in range(200):
            lib.PassThruWriteMsgs(ch, byref(make_msg(6, [0x22, 0xF1, 0x00])), byref(ln), 100)
            lib.PassThruReadMsgs(ch, byref(PASSTHRU_MSG()), byref(ln), 100)
    threads = [threading.Thread(target=worker) for _ in range(3)]
    [th.start() for th in threads]; [th.join() for th in threads]
    torn = [l for l in log_text().splitlines()
        if not l.startswith(("[", "MSG ", "[SUSPECT"))]
    check("no torn/interleaved log lines", not torn, "%d bad lines" % len(torn))

    lib.PassThruClose(dev)

    # 9. disabled mode: subprocess loads with enabled=0, no new trace file
    # TODO need to read trace file into a variable to be able to restore it at the end
    # with open(TRACE_INI, "w") as f: f.write("[trace]\nenabled=0\n")
    # before = set(glob.glob(os.path.join(TRACES_DIR, "*.log")))
    # subproc_probe("lib.PassThruOpen(None,byref(c_ulong()));lib.PassThruClose(1)")
    # after = set(glob.glob(os.path.join(TRACES_DIR, "*.log")))
    # check("disabled mode: no trace file, calls still succeed", before == after)
    # with open(TRACE_INI, "w") as f: f.write("[trace]\nenabled=1\n")

    # 10. missing real DLL: graceful failure
    try:
        os.rename(REAL_DLL, REAL_DLL + ".bak")
        code = ("import ctypes;from ctypes import byref,c_ulong;"
                "lib=ctypes.CDLL(r'%s');"
                "rc=lib.PassThruOpen(None,byref(c_ulong()));"
                "assert rc==0xE2, rc;print('OK')" % PROXY_DLL)
        r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True) 
        check("missing real DLL: rc=0xE2, no crash", r.returncode == 0)
    finally:
        os.rename(REAL_DLL + ".bak", REAL_DLL)
        
    # 11. Periodic message logging
    periodic = make_msg(6, [0x03, 0x00])
    num.value = 1
    rc = lib.PassThruStartPeriodicMsg(ch, byref(periodic), byref(c_ulong(0)), 100)
    # Mock returns periodic in next read
    rc = lib.PassThruReadMsgs(ch, byref(PASSTHRU_MSG()), byref(num), 100)
    t = log_text()
    check("Periodic message logged", "MSG PERIODIC" in t or "txflags=0x0" in t,
          "periodic msg should appear in log")
    
    # 12. Error code propagation
    lib.PassThruClose(dev)
    lib.PassThruConnect(dev, 6, 0, 500000, byref(ch))  # reopen channel then attempt bogus write
    rc = lib.PassThruWriteMsgs(c_ulong(999), byref(make_msg(6, [0])), byref(num), 100)
    # Invalid channel should fail, proxy should propagate and log
    check("Error code propagate", rc != 0 and "ch=999" in log_text()[-500:],
          "invalid channel call should return error and log channel number")

    # 13. HexBytes logging: concurrent StartMsgFilter calls
    log_before = len(log_text())

    def filter_worker(tid, results):
        b = 0xA0 + tid
        mask = make_msg(6, [b, b, b, b])
        pat  = make_msg(6, [b, b, b, b])
        fc   = make_msg(6, [b, b, b, b])
        for _ in range(200):
            rc = lib.PassThruStartMsgFilter(ch, 1, byref(mask), byref(pat),
                                            byref(fc), byref(fid))
            if rc != 0:
                results[tid] = "rc=%d" % rc
                return
        results[tid] = "ok"

    threads = []
    results = {}
    for t in range(4):
        th = threading.Thread(target=filter_worker, args=(t, results))
        threads.append(th); th.start()
    for th in threads: th.join()

    trip = re.compile(r"mask=\[([0-9A-F ]*)\] pattern=\[([0-9A-F ]*)\] fc=\[([0-9A-F ]*)\]")
    new_lines = [l for l in log_text()[log_before:].splitlines()
                if "StartMsgFilter" in l]
    bad = []
    for l in new_lines:
        m = trip.search(l)
        if not m or not (m.group(1) == m.group(2) == m.group(3)):
            bad.append(l)

    check("concurrent filters log correct data",
        all(v == "ok" for v in results.values()) and
        len(new_lines) >= 800 and not bad,
        "concurrent StartMsgFilter calls must log matching mask/pattern/fc triples")

    failed = [n for n, ok in RESULTS if not ok]
    print("\n%d/%d checks passed" % (len(RESULTS) - len(failed), len(RESULTS)))
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()