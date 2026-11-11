/* usnf97fix: window-thread hang watchdog (diagnostics).
 *
 * Every 500 ms a background thread sends WM_NULL to the game window with a timeout.
 * If the window thread stops answering, it is briefly suspended, its registers and the
 * top of its stack are copied, it is resumed, and the likely call chain is logged
 * (return addresses resolved against USNF.SMS and loaded modules). The game and timer
 * threads are dumped too, plus the owner of the critical section KEYEvent uses, so a
 * deadlock shows both sides. Nothing is logged while the game behaves. */
#include "shim.h"

#define CTX_SIZE      0x2CC
#define CTX_FLAGS     0x00010003      /* CONTEXT_i386 | CONTROL | INTEGER */
#define CTX_EBP       0xB4
#define CTX_EIP       0xB8
#define CTX_ESP       0xC4
#define STACK_DWORDS  1024

static HWND  s_hwnd;
static DWORD s_exeLo, s_exeHi, s_keyCs;

static void fmt_addr(DWORD a, char *out, int cap)
{
    out[0] = 0;
    if (a >= s_exeLo && a < s_exeHi) {
        DWORD base = 0;
        const char *n = patches_symbol_at(a, &base);
        if (n) wsprintfA(out, "usnf.exe!%.60s+%lX", n, a - base);
        else   wsprintfA(out, "usnf.exe+%lX", a - s_exeLo);
        return;
    }
    {
        HMODULE m = NULL;
        char path[MAX_PATH];
        const char *b, *p;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (const char *)a, &m) || !m) return;
        if (!GetModuleFileNameA(m, path, sizeof path)) return;
        for (b = p = path; *p; p++) if (*p == '\\' || *p == '/') b = p + 1;
        (void)cap;
        wsprintfA(out, "%.40s+%lX", b, a - (DWORD)m);
    }
}

static int is_code(DWORD a)
{
    char t[128];
    if (a < 0x10000) return 0;
    fmt_addr(a, t, sizeof t);
    return t[0] != 0;
}

static void dump_thread(const char *label, HANDLE h)
{
    static BYTE ctx[CTX_SIZE + 16];
    static DWORD stack[STACK_DWORDS];
    BYTE *c = (BYTE *)(((DWORD)ctx + 15) & ~15u);
    DWORD eip, esp, got = 0, i, shown = 0;
    SIZE_T rd = 0;
    char where[128];
    int ok;

    if (!h) { logf("hang: %s: no handle", label); return; }
    for (i = 0; i < CTX_SIZE; i++) c[i] = 0;
    *(DWORD *)c = CTX_FLAGS;
    if (SuspendThread(h) == (DWORD)-1) { logf("hang: %s: SuspendThread failed (%lu)", label, GetLastError()); return; }
    ok = GetThreadContext(h, c);
    if (ok) {
        esp = *(DWORD *)(c + CTX_ESP);
        /* copy while suspended; resolve names only after resuming (loader lock) */
        for (got = STACK_DWORDS; got >= 64; got /= 2)
            if (ReadProcessMemory(GetCurrentProcess(), (void *)esp, stack, got * 4, &rd)) break;
        if (got < 64) got = 0;
    }
    ResumeThread(h);
    if (!ok) { logf("hang: %s: GetThreadContext failed (%lu)", label, GetLastError()); return; }

    eip = *(DWORD *)(c + CTX_EIP);
    fmt_addr(eip, where, sizeof where);
    logf("hang: %s (tid %lu) eip %08lX %s  esp %08lX ebp %08lX", label, GetThreadId(h), eip, where,
         *(DWORD *)(c + CTX_ESP), *(DWORD *)(c + CTX_EBP));
    for (i = 0; i < got && shown < 24; i++) {
        if (!is_code(stack[i])) continue;
        fmt_addr(stack[i], where, sizeof where);
        logf("hang:   [esp+%03lX] %08lX %s", i * 4, stack[i], where);
        shown++;
    }
}

static HANDLE game_handle(const char *sym)
{
    DWORD a = patches_symbol(sym);
    return a ? *(HANDLE *)a : NULL;
}

static void dump_all(HWND hwnd, DWORD hungMs)
{
    DWORD tid = GetWindowThreadProcessId(hwnd, NULL);
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    logf("hang: window thread %lu not answering for %lu ms; fg=%08lX", tid, hungMs, (DWORD)GetForegroundWindow());
    dump_thread("window thread", h);
    if (h) CloseHandle(h);
    dump_thread("game thread", game_handle("_hGameThread"));
    dump_thread("time thread", game_handle("_hTimeThread"));
    if (s_keyCs) {
        /* RTL_CRITICAL_SECTION: +4 LockCount, +8 RecursionCount, +C OwningThread */
        DWORD *cs = (DWORD *)s_keyCs;
        logf("hang: KEYEvent lock %08lX: LockCount %ld Recursion %ld Owner tid %lu",
             s_keyCs, (LONG)cs[1], (LONG)cs[2], cs[3]);
    }
}

static DWORD WINAPI hang_thread(void *arg)
{
    DWORD hungSince = 0, dumps = 0, lastDump = 0;
    (void)arg;
    for (;;) {
        DWORD_PTR res;
        Sleep(500);
        if (!IsWindow(s_hwnd)) return 0;
        if (SendMessageTimeoutA(s_hwnd, WM_NULL, 0, 0, SMTO_NORMAL, 400, &res)) {
            if (hungSince) logf("hang: window thread responsive again after %lu ms", GetTickCount() - hungSince);
            hungSince = 0;
            continue;
        }
        if (!hungSince) hungSince = GetTickCount() - 400;
        if (dumps < 4 && (dumps == 0 || GetTickCount() - lastDump >= 5000) && GetTickCount() - hungSince >= 1000) {
            dump_all(s_hwnd, GetTickCount() - hungSince);
            lastDump = GetTickCount();
            dumps++;
        }
    }
}

void hang_watch_start(HWND hwnd)
{
    BYTE *e = (BYTE *)GetModuleHandleA(NULL);
    DWORD ke;
    if (s_hwnd || !hwnd) return;
    s_hwnd = hwnd;
    s_exeLo = (DWORD)e;
    s_exeHi = s_exeLo + *(DWORD *)(e + *(DWORD *)(e + 0x3c) + 0x50);
    /* KEYEvent: "push offset cs; call [EnterCriticalSection]" near the top */
    ke = patches_symbol("?KEYEvent@@YAXIIJ@Z");
    if (ke && ke >= s_exeLo && ke + 0x100 < s_exeHi) {
        const BYTE *p = (const BYTE *)ke;
        int i;
        for (i = 0; i < 0x100 - 11; i++)
            if (p[i] == 0x68 && p[i + 5] == 0xFF && p[i + 6] == 0x15) {
                DWORD v = *(const DWORD *)(p + i + 1);
                if (v >= s_exeLo && v < s_exeHi) { s_keyCs = v; break; }
            }
    }
    CreateThread(NULL, 0, hang_thread, NULL, 0, NULL);
    logf("hang watch: on (KEYEvent lock %08lX)", s_keyCs);
}
