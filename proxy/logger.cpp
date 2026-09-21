#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <share.h>
#include <stdint.h>

#include "logger.h"

#define DATA_CAP    4128   // full buffer: never truncate flash payloads

static std::wstring g_traceDir; // directory of this proxy DLL
static HANDLE g_mutex = NULL;
static FILE*  g_file = NULL;
static LARGE_INTEGER g_freq, g_last; // log delta time between frames 
static bool g_perf_init = false;

// flush policy: 0 => flush after every log write (default, max capture fidelity)
static long g_flushMs = 0;
// refuse to start logging below this much free space (MB), fail open
static unsigned long long g_minFreeMB = 200;
static bool g_loggerDead = false; // set after fatal IO error or low disk
static LARGE_INTEGER g_lastFlush;
static bool g_lastFlushInit = false;

void LoggerInit(const std::wstring& iniPath)
{
    if (!g_mutex) g_mutex = CreateMutexA(NULL, FALSE, NULL);

    if (!g_perf_init) {
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_last);
        g_perf_init = true;
    }

    g_flushMs = (long)GetPrivateProfileIntW(L"trace", L"flush_ms", 0, iniPath.c_str());
    g_minFreeMB = GetPrivateProfileIntW(L"trace", L"min_free_mb", 200, iniPath.c_str());

    std::wstring selfDir(iniPath);
    size_t slash = selfDir.find_last_of(L'\\');
    if (slash != std::wstring::npos) selfDir.resize(slash);

    g_traceDir = selfDir + L"\\traces";
    CreateDirectoryW(g_traceDir.c_str(), NULL);   // ok if it already exists
    g_traceDir += L"\\";
}

// kill logger if disk full or other IO issue
static void KillLoggerLocked(const char* why)
{
    if (g_file) { fclose(g_file); g_file = NULL; }
    g_loggerDead = true; 
    OutputDebugStringA(why); // no file - use debugger channel
}

static void OpenLogFileLocked()
{
    if (g_file || g_loggerDead) return;

    // free-space check, avoids a potential mid-flash failure. Fail open = keep forwarding, no log.
    ULARGE_INTEGER freeBytes = {};
    if (GetDiskFreeSpaceExW(g_traceDir.c_str(), &freeBytes, NULL, NULL)) {
        unsigned long long freeMB = freeBytes.QuadPart / (1024ull * 1024ull);
        if (freeMB < g_minFreeMB) {
            KillLoggerLocked("j2534-trace: low disk space, logging disabled (proxy continues)");
            return;
        }
    }

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t wpath[MAX_PATH];
    swprintf(wpath, MAX_PATH,
        L"%strace_%04d%02d%02d_%02d%02d%02d_%03d_pid%lu.log",
        g_traceDir.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour,
        st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId());
    g_file = _wfsopen(wpath, L"w", _SH_DENYNO);
}

static void MaybeFlushLocked()
{
    if (!g_file) return;
    if (g_flushMs <= 0) {                     // default: flush every write
        if (fflush(g_file) != 0)
            KillLoggerLocked("j2534-trace: fflush failed, logging disabled");
        return;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ms = g_lastFlushInit
        ? (now.QuadPart - g_lastFlush.QuadPart) * 1000.0 / g_freq.QuadPart
        : 1e9;                                // first write after init always flushes
    if (ms >= (double)g_flushMs) {
        if (fflush(g_file) != 0) {
            KillLoggerLocked("j2534-trace: fflush failed, logging disabled");
            return;
        }
        g_lastFlush = now;
        g_lastFlushInit = true;
    }
}

static void LogTimestamp(FILE* file)
{
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(file, "[%02d:%02d:%02d.%03d",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (g_perf_init) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double us = (now.QuadPart - g_last.QuadPart) * 1e6 / g_freq.QuadPart;
        fprintf(file, " +%06.0fus", us);
        g_last = now;
    }

    fprintf(file, "] ");
}

void LogCall(const char* fmt, ...)
{
    if (!g_mutex || g_loggerDead) return;
    WaitForSingleObject(g_mutex, INFINITE);
    OpenLogFileLocked();
    if (g_file) {
        LogTimestamp(g_file);
        va_list ap; va_start(ap, fmt);
        vfprintf(g_file, fmt, ap);
        va_end(ap);
        fprintf(g_file, "\n");
        MaybeFlushLocked();
    }
    ReleaseMutex(g_mutex);
}

void LogMsgs(const char* dir, unsigned long channelId,
             const PASSTHRU_MSG* msgs, unsigned long count, bool suspect)
{
    if (!g_mutex || g_loggerDead) return;
    WaitForSingleObject(g_mutex, INFINITE);
    OpenLogFileLocked();
    if (g_file) {
        for (unsigned long i = 0; i < count; ++i) {
            const PASSTHRU_MSG* m = &msgs[i];

            bool is_bad_size = (m->DataSize > 4128) || (m->ExtraDataIndex > m->DataSize);
            if (suspect || is_bad_size) {
                fprintf(g_file, "  [SUSPECT] ");
            }

            char hex[DATA_CAP * 3 + 1]; // one big line, built then written once
            size_t pos = 0;
            unsigned long n = m->DataSize < DATA_CAP ? m->DataSize : DATA_CAP;
            for (unsigned long b = 0; b < n; ++b) {
                int w = snprintf(hex + pos, sizeof(hex) - pos, "%02X ", m->Data[b]);
                if (w < 0 || (size_t)w >= sizeof(hex) - pos) break; // prevent overflow
                pos += w;
            }
            fprintf(g_file, "MSG %s ch=%lu proto=0x%lX txflags=0x%lX rxstatus=0x%lX "
                            "ts=%lu len=%lu edi=%lu data=%.*s\n",
                    dir, channelId, m->ProtocolID, m->TxFlags, m->RxStatus,
                    m->Timestamp, m->DataSize, m->ExtraDataIndex, (int)pos, hex);
        }
        MaybeFlushLocked();
    }
    ReleaseMutex(g_mutex);
}

const char* HexBytes(const PASSTHRU_MSG& msg)
{
    static thread_local char bufs[4][256];
    static thread_local int slot = 0;
    char* buf = bufs[slot];
    slot = (slot + 1) % 4;

    unsigned long n = msg.DataSize < 64 ? msg.DataSize : 64;
    size_t pos = 0;
    for (unsigned long i = 0; i < n; ++i) {
        int w = snprintf(buf + pos, sizeof(buf) - pos, "%02X ", msg.Data[i]);
        if (w < 0 || (size_t)w >= sizeof(buf) - pos)
            break;
        pos += w;
    }
    return buf;
}
