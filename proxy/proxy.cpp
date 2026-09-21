#include <windows.h>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <mutex>

#include "logger.h"

#define STATUS_NOERROR 0x0000

static HMODULE g_real = NULL;
static HMODULE g_selfModule = NULL;
static std::wstring g_selfDir; // directory of this proxy DLL
static bool g_enabled = true;
static bool g_loadFailed = false;
static std::wstring g_realDllSuffix; // suffix for the real J2534 DLL to load, from trace.ini

// returns the directory containing this DLL, no trailing slash
static const std::wstring& GetSelfDir()
{
    if (g_selfDir.empty()) {
        wchar_t selfPath[MAX_PATH];
        if (GetModuleFileNameW(g_selfModule, selfPath, MAX_PATH)) {
            std::wstring tmp(selfPath);
            size_t slash = tmp.find_last_of(L'\\');
            if (slash != std::wstring::npos)
                g_selfDir = tmp.substr(0, slash);
        }
    }
    return g_selfDir;
}

static HMODULE LoadRealDriver()
{   
    // real driver name is derived from this proxy's name, with the suffix from trace.ini
    wchar_t selfName[MAX_PATH];
    GetModuleFileNameW(g_selfModule, selfName, MAX_PATH);
    size_t dot = std::wstring(selfName).find_last_of(L'.');
    // remove .dll extension and append suffix
    std::wstring real = std::wstring(selfName).substr(0, dot) + g_realDllSuffix + L".dll";
    return LoadLibraryW(real.c_str());
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = (HMODULE)hinst;

        // [trace] enabled=0 disables frame logging
        std::wstring ini = GetSelfDir() + L"\\trace.ini";
        g_enabled = GetPrivateProfileIntW(L"trace", L"enabled", 1, ini.c_str()) != 0;

        // [trace] dll_suffix=orig specifies the suffix for the real DLL to load
        wchar_t suffix[MAX_PATH];
        GetPrivateProfileStringW(L"trace", L"dll_suffix", L"_orig", suffix, MAX_PATH, ini.c_str());
        g_realDllSuffix = std::wstring(suffix);

        if(g_enabled) {
            LoggerInit(ini);
            LogCall("Proxy loaded, logging enabled, dll=%ls", GetSelfDir().c_str());
        }
    }
    return TRUE;
}

static FARPROC Resolve(const char* name)
{
    static std::mutex resolveLock;
    static std::unordered_map<std::string, FARPROC> cache;

    std::lock_guard<std::mutex> lock(resolveLock);

    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    if (!g_real) {
        g_real = LoadRealDriver();
        if (!g_real) {
            if (!g_loadFailed) {
                LogCall("LoadLibrary failed for original J2534 driver, GLE=%lu", GetLastError());
                g_loadFailed = true; // log once, not per-call
            }
            return NULL;
        }
    }

    FARPROC fn = GetProcAddress(g_real, name);
    cache[name] = fn;
    if (!fn) LogCall("GetProcAddress failed for '%s'", name);
    return fn;
}

extern "C" {

__declspec(dllexport) long __stdcall
PassThruOpen(const char* pName, unsigned long* pDeviceID)
{
    typedef long(__stdcall *Fn)(const char*, unsigned long*);
    Fn f = (Fn)Resolve("PassThruOpen");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(pName, pDeviceID);
    if (r == STATUS_NOERROR)
        LogCall("PassThruOpen(name='%s') -> %ld deviceId=%lu",
                pName ? pName : "", r, pDeviceID ? *pDeviceID : 0);
    else
        LogCall("PassThruOpen(name='%s') -> %ld", pName ? pName : "", r);

    // warm cache eagerly, to prevent two threads from racing and corrupting the cache map
    Resolve("PassThruClose");
    Resolve("PassThruConnect");
    Resolve("PassThruDisconnect");
    Resolve("PassThruReadMsgs");
    Resolve("PassThruWriteMsgs");
    Resolve("PassThruStartPeriodicMsg");
    Resolve("PassThruStopPeriodicMsg");
    Resolve("PassThruStartMsgFilter");
    Resolve("PassThruStopMsgFilter");
    Resolve("PassThruSetProgrammingVoltage");
    Resolve("PassThruReadVoltage");
    Resolve("PassThruReadVersion");
    Resolve("PassThruGetLastError");
    Resolve("PassThruIoctl");
    
    return r;
}

__declspec(dllexport) long __stdcall
PassThruClose(unsigned long DeviceID)
{
    typedef long(__stdcall *Fn)(unsigned long);
    Fn f = (Fn)Resolve("PassThruClose");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(DeviceID);
    LogCall("PassThruClose(device=%lu) -> %ld", DeviceID, r);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID, unsigned long Flags,
                unsigned long BaudRate, unsigned long* pChannelID)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long*);
    Fn f = (Fn)Resolve("PassThruConnect");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(DeviceID, ProtocolID, Flags, BaudRate, pChannelID);
    LogCall("PassThruConnect(device=%lu proto=0x%lX flags=0x%lX baud=%lu) -> %ld channel=%lu",
            DeviceID, ProtocolID, Flags, BaudRate, r, pChannelID ? *pChannelID : 0);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruDisconnect(unsigned long ChannelID)
{
    typedef long(__stdcall *Fn)(unsigned long);
    Fn f = (Fn)Resolve("PassThruDisconnect");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(ChannelID);
    LogCall("PassThruDisconnect(channel=%lu) -> %ld", ChannelID, r);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG* pMsg, unsigned long* pNumMsgs,
                 unsigned long Timeout)
{
    typedef long(__stdcall *Fn)(unsigned long, PASSTHRU_MSG*, unsigned long*, unsigned long);
    Fn f = (Fn)Resolve("PassThruReadMsgs");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(ChannelID, pMsg, pNumMsgs, Timeout);
    if (g_enabled) LogCall("PassThruReadMsgs(ch=%lu timeout=%lu) -> %ld num=%lu",
                           ChannelID, Timeout, r, pNumMsgs ? *pNumMsgs : 0);
    if (g_enabled && pNumMsgs && *pNumMsgs > 0 && r == STATUS_NOERROR)
        LogMsgs("RX", ChannelID, pMsg, *pNumMsgs, pMsg->DataSize > 4128);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG* pMsg, unsigned long* pNumMsgs,
                 unsigned long Timeout)
{
    typedef long(__stdcall *Fn)(unsigned long, PASSTHRU_MSG*, unsigned long*, unsigned long);
    Fn f = (Fn)Resolve("PassThruWriteMsgs");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    if (g_enabled && pNumMsgs && *pNumMsgs > 0)
        LogMsgs("TX", ChannelID, pMsg, *pNumMsgs, pMsg->DataSize > 4128);
    long r = f(ChannelID, pMsg, pNumMsgs, Timeout);
    if (g_enabled)
        LogCall("PassThruWriteMsgs(ch=%lu timeout=%lu) -> %ld", ChannelID, Timeout, r);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG* pMsg,
                         unsigned long* pMsgID, unsigned long TimeInterval)
{
    typedef long(__stdcall *Fn)(unsigned long, PASSTHRU_MSG*, unsigned long*, unsigned long);
    Fn f = (Fn)Resolve("PassThruStartPeriodicMsg");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(ChannelID, pMsg, pMsgID, TimeInterval);
    LogCall("PassThruStartPeriodicMsg(ch=%lu interval=%lu) -> %ld msgID=%lu",
            ChannelID, TimeInterval, r, pMsgID ? *pMsgID : 0);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long);
    Fn f = (Fn)Resolve("PassThruStopPeriodicMsg");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(ChannelID, MsgID);
    LogCall("PassThruStopPeriodicMsg(ch=%lu msgID=%lu) -> %ld", ChannelID, MsgID, r);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruStartMsgFilter(unsigned long ChannelID, unsigned long FilterType,
                       PASSTHRU_MSG* pMaskMsg, PASSTHRU_MSG* pPatternMsg,
                       PASSTHRU_MSG* pFlowControlMsg, unsigned long* pFilterID)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long, PASSTHRU_MSG*, PASSTHRU_MSG*, PASSTHRU_MSG*, unsigned long*);
    Fn f = (Fn)Resolve("PassThruStartMsgFilter");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(ChannelID, FilterType, pMaskMsg, pPatternMsg, pFlowControlMsg, pFilterID);
    LogCall("PassThruStartMsgFilter(ch=%lu type=0x%lX mask=[%s] pattern=[%s] fc=[%s]) -> %ld filterID=%lu",
            ChannelID, FilterType,
            pMaskMsg ? HexBytes(*pMaskMsg) : "",
            pPatternMsg ? HexBytes(*pPatternMsg) : "",
            pFlowControlMsg ? HexBytes(*pFlowControlMsg) : "",
            r, pFilterID ? *pFilterID : 0);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruStopMsgFilter(unsigned long ChannelID, unsigned long FilterID)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long);
    Fn f = (Fn)Resolve("PassThruStopMsgFilter");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(ChannelID, FilterID);
    LogCall("PassThruStopMsgFilter(ch=%lu filterID=%lu) -> %ld", ChannelID, FilterID, r);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruSetProgrammingVoltage(unsigned long DeviceID, unsigned long Pin, unsigned long Voltage)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long, unsigned long);
    Fn f = (Fn)Resolve("PassThruSetProgrammingVoltage");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(DeviceID, Pin, Voltage);
    LogCall("SetProgrammingVoltage(device=%lu pin=%lu voltage=%lu) -> %ld", DeviceID, Pin, Voltage, r);
    return r;
}

// j2534-2 technically so not required, but included for completeness; some devices implement it
__declspec(dllexport) long __stdcall
PassThruReadVoltage(unsigned long DeviceID, unsigned long* pVoltage)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long*);
    Fn f = (Fn)Resolve("PassThruReadVoltage");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(DeviceID, pVoltage);
    LogCall("ReadVoltage(device=%lu) -> %ld voltage=%lu mV", DeviceID, r, pVoltage ? *pVoltage : 0);
    return r;
}

__declspec(dllexport) long __stdcall
PassThruReadVersion(unsigned long DeviceID, char* pFirmwareVersion,
                    char* pDllVersion, char* pApiVersion)
{
    typedef long(__stdcall *Fn)(unsigned long, char*, char*, char*);
    Fn f = (Fn)Resolve("PassThruReadVersion");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(DeviceID, pFirmwareVersion, pDllVersion, pApiVersion);
    LogCall("ReadVersion(device=%lu) -> %ld fw='%s' dll='%s' api='%s'",
            DeviceID, r,
            pFirmwareVersion ? pFirmwareVersion : "",
            pDllVersion ? pDllVersion : "",
            pApiVersion ? pApiVersion : "");
    return r;
}

__declspec(dllexport) long __stdcall
PassThruGetLastError(char* pErrorDescription)
{
    typedef long(__stdcall *Fn)(char*);
    Fn f = (Fn)Resolve("PassThruGetLastError");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(pErrorDescription);
    LogCall("GetLastError() -> %ld desc='%s'",
            r, pErrorDescription ? pErrorDescription : "");
    return r;
}

static void LogSconfigList(const char* which, void* p)
{
    if (!p) return;
    SCONFIG_LIST* list = (SCONFIG_LIST*)p;
    if (!list->ConfigPtr || list->NumOfParams == 0) return;
    unsigned long n = list->NumOfParams < 64 ? list->NumOfParams : 64;  // cap to spec-plausible range
    for (unsigned long i = 0; i < n; ++i)
        LogCall("  %s param=0x%lX value=0x%lX", which,
                list->ConfigPtr[i].Parameter, list->ConfigPtr[i].Value);
}

__declspec(dllexport) long __stdcall
PassThruIoctl(unsigned long HandleID, unsigned long IoctlID,
              void* pInput, void* pOutput)
{
    typedef long(__stdcall *Fn)(unsigned long, unsigned long, void*, void*);
    Fn f = (Fn)Resolve("PassThruIoctl");
    if (!f) return 0xE2; // ERR_NOT_SUPPORTED
    long r = f(HandleID, IoctlID, pInput, pOutput);

    LogCall("Ioctl(handle=%lu id=0x%lX) -> %ld", HandleID, IoctlID, r);
    if (IoctlID == 0x02 && pInput) {            // SET_CONFIG: params inbound
        LogSconfigList("SET_CONFIG", pInput);
    }
    if (IoctlID == 0x01 && pOutput) {            // GET_CONFIG: results land in pOutput
        LogSconfigList("GET_CONFIG", pOutput);
    }
    return r;
}

} // extern "C"