/* usnf97fix: tiny CRT, config, logging, timing. */
#include "shim.h"

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset, memcpy, memcmp)
#endif

int _fltused = 1;

void *memset(void *d, int c, unsigned int n) { BYTE *p = d; while (n--) *p++ = (BYTE)c; return d; }
void *memcpy(void *d, const void *s, unsigned int n)
{
    BYTE *dp = d; const BYTE *sp = s;
    while (n >= 4) { *(DWORD *)dp = *(const DWORD *)sp; dp += 4; sp += 4; n -= 4; }
    while (n--) *dp++ = *sp++;
    return d;
}
void *memmove(void *d, const void *s, unsigned int n)
{
    BYTE *dp = d; const BYTE *sp = s;
    if (dp == sp || !n) return d;
    if (dp < sp || dp >= sp + n) return memcpy(d, s, n);
    dp += n; sp += n;
    while (n--) *--dp = *--sp;
    return d;
}
int memcmp(const void *a, const void *b, unsigned int n)
{
    const BYTE *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
unsigned int str_len(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
int str_eq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
int str_ieq(const char *a, const char *b) { while (*a && lc(*a) == lc(*b)) a++, b++; return lc(*a) == lc(*b); }
void str_cpy(char *d, const char *s, unsigned int cap)
{
    unsigned int i = 0;
    if (!cap) return;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
void str_cat(char *d, const char *s, unsigned int cap) { unsigned int n = str_len(d); if (n < cap) str_cpy(d + n, s, cap - n); }

void *mem_alloc(unsigned int n) { return VirtualAlloc(NULL, n ? n : 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
void  mem_free(void *p) { if (p) VirtualFree(p, 0, MEM_RELEASE); }

/* ---------------- config ---------------- */
Config g_cfg;
char   g_gameDir[260];
char g_iniPath[280];
#define s_iniPath g_iniPath

void config_load(void)
{
    static const char S[] = "usnf97fix";
    char path[260];
    unsigned int i, slash = 0;

    GetModuleFileNameA(NULL, path, sizeof path);
    for (i = 0; path[i]; i++) if (path[i] == '\\' || path[i] == '/') slash = i + 1;
    path[slash] = 0;
    str_cpy(g_gameDir, path, sizeof g_gameDir);
    str_cpy(s_iniPath, g_gameDir, sizeof s_iniPath);
    str_cat(s_iniPath, "usnf97fix.ini", sizeof s_iniPath);

    g_cfg.windowMode        = GetPrivateProfileIntA(S, "WindowMode", 0, s_iniPath);
    g_cfg.windowScale       = GetPrivateProfileIntA(S, "WindowScale", 0, s_iniPath);
    g_cfg.filter            = GetPrivateProfileIntA(S, "Filter", 0, s_iniPath);
    g_cfg.aspectCorrect     = GetPrivateProfileIntA(S, "AspectCorrect", 1, s_iniPath);
    g_cfg.stretch           = GetPrivateProfileIntA(S, "Stretch", 0, s_iniPath);
    {
        char v[16];
        GetPrivateProfileStringA(S, "SideFill", "ambient", v, sizeof v, s_iniPath);
        g_cfg.sideFill = (v[0] == 'b' || v[0] == 'B' || v[0] == '0') ? 0 : 1;
    }
    g_cfg.vsync             = GetPrivateProfileIntA(S, "VSync", 1, s_iniPath);
    g_cfg.frameCap          = GetPrivateProfileIntA(S, "FrameCap", 0, s_iniPath);
    g_cfg.fixGameSpeed      = GetPrivateProfileIntA(S, "FixGameSpeed", 1, s_iniPath);
    g_cfg.minFrameTicks     = GetPrivateProfileIntA(S, "MinFrameTicks", 5, s_iniPath);
    g_cfg.pauseInBackground = GetPrivateProfileIntA(S, "PauseInBackground", 1, s_iniPath);
    g_cfg.confineMouse      = GetPrivateProfileIntA(S, "ConfineMouse", 1, s_iniPath);
    g_cfg.traceMessages     = GetPrivateProfileIntA(S, "TraceMessages", 0, s_iniPath);
    g_cfg.log               = GetPrivateProfileIntA(S, "Log", 1, s_iniPath);
    GetPrivateProfileStringA(S, "CDPath", "", g_cfg.cdPath, sizeof g_cfg.cdPath, s_iniPath);

    if (g_cfg.windowScale < 0) g_cfg.windowScale = 0;
    if (g_cfg.minFrameTicks < 1) g_cfg.minFrameTicks = 1;
    if (g_cfg.minFrameTicks > 64) g_cfg.minFrameTicks = 64;
    if (g_cfg.frameCap < 0) g_cfg.frameCap = 0;
}

/* ---------------- logging ---------------- */
static HANDLE s_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION s_logLock;

void log_open(void)
{
    char path[280];
    InitializeCriticalSection(&s_logLock);
    if (!g_cfg.log) return;
    str_cpy(path, g_gameDir, sizeof path);
    str_cat(path, "usnf97fix.log", sizeof path);
    s_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void logf(const char *fmt, ...)
{
    char buf[1100];
    va_list ap;
    DWORD n, w;
    if (s_log == INVALID_HANDLE_VALUE && !g_cfg.log) return;
    va_start(ap, fmt);
    n = (DWORD)wvsprintfA(buf, fmt, ap);      /* user32: at most 1024 chars of output */
    va_end(ap);
    if (n > sizeof buf - 3) n = sizeof buf - 3;
    buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    OutputDebugStringA(buf);
    if (s_log == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&s_logLock);
    WriteFile(s_log, buf, n, &w, NULL);
    LeaveCriticalSection(&s_logLock);
}

/* ---------------- crash diagnostics ----------------
 * Raw writer (no wvsprintfA) + a vectored handler that records the first error-class
 * exceptions with their address, relative to this DLL or USNF.EXE. */
static DWORD s_dllLo, s_dllHi, s_exeLo, s_exeHi;
static volatile LONG s_crashLogged;

static void hex8(char *o, DWORD v) { int i; for (i = 7; i >= 0; i--) { o[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; } }

void log_raw(const char *s)
{
    DWORD w;
    if (s_log == INVALID_HANDLE_VALUE) return;
    WriteFile(s_log, s, str_len(s), &w, NULL);
    WriteFile(s_log, "\r\n", 2, &w, NULL);
}

static LONG WINAPI crash_logger(EXCEPTION_POINTERS *ep)
{
    char line[96];
    DWORD code = ep->ExceptionRecord->ExceptionCode, at = (DWORD)ep->ExceptionRecord->ExceptionAddress;
    if (code < 0xC0000000u || s_crashLogged >= 16) return EXCEPTION_CONTINUE_SEARCH;
    s_crashLogged++;
    str_cpy(line, "exception XXXXXXXX at XXXXXXXX", sizeof line);
    hex8(line + 10, code); hex8(line + 22, at);
    {
        DWORD base = 0;
        if (at >= s_dllLo && at < s_dllHi) { str_cat(line, " = ddraw.dll+", sizeof line); base = s_dllLo; }
        else if (at >= s_exeLo && at < s_exeHi) { str_cat(line, " = usnf.exe+", sizeof line); base = s_exeLo; }
        if (base) { unsigned int n = str_len(line); hex8(line + n, at - base); line[n + 8] = 0; }
    }
    log_raw(line);
    return EXCEPTION_CONTINUE_SEARCH;
}

void crash_logger_install(HINSTANCE dll)
{
    BYTE *b = (BYTE *)dll, *e = (BYTE *)GetModuleHandleA(NULL);
    s_dllLo = (DWORD)b; s_dllHi = s_dllLo + *(DWORD *)(b + *(DWORD *)(b + 0x3c) + 0x50);
    s_exeLo = (DWORD)e; s_exeHi = s_exeLo + *(DWORD *)(e + *(DWORD *)(e + 0x3c) + 0x50);
    AddVectoredExceptionHandler(0, crash_logger);   /* last in the chain: only observes */
}

/* ---------------- timing ---------------- */
static LONGLONG s_freq = 1;

void time_init(void)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    s_freq = f.QuadPart ? f.QuadPart : 1;
}
LONGLONG qpc_now(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
LONGLONG qpc_freq(void) { return s_freq; }

void wait_until(LONGLONG target)
{
    LONGLONG oneAndHalfMs = (LONGLONG)((DWORD)s_freq / 2000u * 3u);   /* QPC freq fits in 32 bits */
    for (;;) {
        LONGLONG left = target - qpc_now();
        if (left <= 0) return;
        if (left > oneAndHalfMs) Sleep(1);
        else Sleep(0);
    }
}
