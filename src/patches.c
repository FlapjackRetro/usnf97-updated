/* usnf97fix: in-memory fixes to USNF.EXE, applied from DllMain before the game runs.
 *
 * Addresses come from USNF.SMS, the symbol map EA shipped next to the exe, so the
 * same DLL works with any build whose SMS matches (1.0 retail, 1.4 patch). Every
 * patch checks the exact instruction bytes it expects first and is skipped (and
 * logged) on any mismatch: an unknown build degrades to "no fix", never to a crash. */
#include "shim.h"
#include "patch_core.h"

static BYTE  *s_base;
static SmsMap s_sms;

/* ------------------------------------------------------------------ */
/* PE helpers                                                          */
/* ------------------------------------------------------------------ */
static DWORD rd32(const BYTE *p) { return *(const DWORD *)p; }

static int write_code(void *dst, const void *src, unsigned n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return 1;
}

int iat_hook(HMODULE mod, const char *dll, const char *func, void *repl, void **orig)
{
    BYTE *b = (BYTE *)mod;
    BYTE *nt = b + rd32(b + 0x3c);
    DWORD impRva = rd32(nt + 0x80);            /* OptionalHeader.DataDirectory[1] (PE32) */
    BYTE *desc;
    void *real = NULL;
    HMODULE dm = GetModuleHandleA(dll);
    if (dm) real = (void *)GetProcAddress(dm, func);
    if (!impRva) return 0;
    for (desc = b + impRva; rd32(desc + 12); desc += 20) {
        const char *name = (const char *)(b + rd32(desc + 12));
        DWORD oft = rd32(desc), ft = rd32(desc + 16), i;
        if (!str_ieq(name, dll)) continue;
        for (i = 0;; i++) {
            DWORD *slot = (DWORD *)(b + ft) + i;
            int match = 0;
            if (!*slot) break;
            if (oft) {
                DWORD t = ((DWORD *)(b + oft))[i];
                if (!(t & 0x80000000u) && str_eq((const char *)(b + t + 2), func)) match = 1;
            } else if (real && (void *)*slot == real) match = 1;
            if (match) {
                DWORD old, v = (DWORD)repl;
                if (orig) *orig = (void *)*slot;
                if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return 0;
                *slot = v;
                VirtualProtect(slot, 4, old, &old);
                return 1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* USNF.SMS                                                            */
/* ------------------------------------------------------------------ */
static int sms_load(void)
{
    char path[280];
    HANDLE f;
    DWORD size, got;
    BYTE *buf;
    str_cpy(path, g_gameDir, sizeof path);
    str_cat(path, "USNF.SMS", sizeof path);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { logf("USNF.SMS not found: exe fixes disabled"); return 0; }
    size = GetFileSize(f, NULL);
    buf = mem_alloc(size + 1);
    if (!buf || !ReadFile(f, buf, size, &got, NULL) || got != size) { CloseHandle(f); logf("USNF.SMS unreadable"); return 0; }
    CloseHandle(f);
    if (!sms_parse(&s_sms, buf, size)) { logf("USNF.SMS: bad format"); return 0; }
    logf("USNF.SMS: %lu symbols", s_sms.count);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Fix 1: CD path. _cdpath() only scans CD drives when CDPATH is empty. */
/* ------------------------------------------------------------------ */
static void patch_cdpath(void)
{
    char want[260], probe[300];
    DWORD addr = sms_find(&s_sms, "?CDPATH@@3PADA");
    DWORD cap = sms_size(&s_sms, addr);
    char *buf;
    unsigned n;

    if (!addr) { logf("CD path: symbol not found, leaving CD detection alone"); return; }
    buf = (char *)addr;
    if (cap > 260) cap = 260;
    if (cap < 16) cap = 16;
    if (buf[0]) { logf("CD path: already set to '%s'", buf); return; }

    if (g_cfg.cdPath[0]) {
        str_cpy(want, g_cfg.cdPath, sizeof want);
    } else {
        str_cpy(probe, g_gameDir, sizeof probe);
        str_cat(probe, "USNF_10.LIB", sizeof probe);
        if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES) {
            logf("CD path: USNF_10.LIB not in game folder, the game will look for the CD");
            return;
        }
        str_cpy(want, g_gameDir, sizeof want);
    }
    n = str_len(want);
    if (n && want[n - 1] != '\\' && want[n - 1] != '/') str_cat(want, "\\", sizeof want);

    /* The game strcat()s file names onto this in 80-byte buffers: keep it short. */
    if (str_len(want) > 60) {
        char shortp[260];
        DWORD m = GetShortPathNameA(want, shortp, sizeof shortp);
        if (m && m < sizeof shortp && str_len(shortp) <= 60) str_cpy(want, shortp, sizeof want);
    }
    if (str_len(want) > 60 || str_len(want) + 1 > cap) {
        logf("CD path: '%s' is too long for the game (max 60 chars); move the game to a shorter folder", want);
        return;
    }
    str_cpy(buf, want, cap);   /* .data is writable */
    logf("CD path: set to '%s' (no disc needed)", buf);
}

/* ------------------------------------------------------------------ */
/* Fix 2: game speed. TIMEUpdate() waits for the 1/256 s clock to move */
/* and then clamps the step to >= 5 ticks, so any machine faster than  */
/* ~51 fps runs the simulation faster than real time. We make the wait */
/* last until the minimum step has really elapsed (and sleep, not spin).*/
/* ------------------------------------------------------------------ */
static TimePatch s_tp;
static void (__cdecl *s_readClock)(void);

static void __cdecl tick_wait(void)
{
    volatile DWORD *now = (volatile DWORD *)s_tp.nowAddr, *last = (volatile DWORD *)s_tp.lastAddr;
    int minTicks = g_cfg.minFrameTicks;
    if (g_cfg.frameCap > 0) {
        int capTicks = (256 + g_cfg.frameCap - 1) / g_cfg.frameCap;
        if (capTicks > minTicks) minTicks = capTicks;
    }
    if (s_tp.compressAddr) {
        short c = *(volatile short *)s_tp.compressAddr;   /* time compression: <0 = slow motion */
        if (c < 0 && c > -6) minTicks <<= -c;
    }
    for (;;) {
        LONG diff;
        s_readClock();
        diff = (LONG)(*now - *last);
        if (diff < 0 || diff >= minTicks) return;
        if ((minTicks - diff) * 1000 / 256 >= 2) Sleep(1);
        else Sleep(0);
    }
}

static void patch_timeupdate(void)
{
    DWORD fn = sms_find(&s_sms, "_TIMEUpdate@0");
    BYTE call[5];
    DWORD rel;
    if (!fn) { logf("speed fix: _TIMEUpdate@0 not in symbol map"); return; }
    if (!timeupdate_match((const BYTE *)fn, fn, &s_tp)) {
        logf("speed fix: TIMEUpdate at %08lX does not look like the known code; not patched", fn);
        return;
    }
    s_readClock = (void (__cdecl *)(void))s_tp.readClockFn;
    rel = (DWORD)tick_wait - (s_tp.callSite + 5);
    call[0] = 0xE8;
    memcpy(call + 1, &rel, 4);
    if (!write_code((void *)s_tp.callSite, call, 5)) { logf("speed fix: VirtualProtect failed"); return; }
    logf("speed fix: TIMEUpdate %08lX patched (clock %08lX, now %08lX, last %08lX, compression %08lX)",
         fn, s_tp.readClockFn, s_tp.nowAddr, s_tp.lastAddr, s_tp.compressAddr);
    logf("speed fix: game clamps steps to >= %d ticks; frames now wait for >= %d ticks of real time",
         s_tp.hardMinStep, g_cfg.minFrameTicks);
    if (s_tp.hardMinStep > 0 && s_tp.hardMinStep != g_cfg.minFrameTicks)
        logf("speed fix: note MinFrameTicks (%d) differs from the game's clamp (%d)", g_cfg.minFrameTicks, s_tp.hardMinStep);
}

/* ------------------------------------------------------------------ */
/* Fix 3: privileged instructions. The video player loop (MainLoop,    */
/* run by PlayCobra for the intro and reference movies) still brackets */
/* clock reads with CLI/STI from the DOS version. Windows 9x tolerated */
/* that; NT-based Windows raises STATUS_PRIVILEGED_INSTRUCTION. Skip   */
/* the one-byte CLI (FA) / STI (FB) when it faults inside USNF.EXE.    */
/* ------------------------------------------------------------------ */
static DWORD s_imageLo, s_imageHi;
static volatile LONG s_cliSkipped;

static LONG WINAPI priv_handler(EXCEPTION_POINTERS *ep)
{
    DWORD *eip;
    BYTE op;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    eip = (DWORD *)(ep->ContextRecord + CONTEXT_EIP_OFFSET);
    if (*eip < s_imageLo || *eip >= s_imageHi) return EXCEPTION_CONTINUE_SEARCH;
    op = *(const BYTE *)*eip;
    if (op != 0xFA && op != 0xFB) return EXCEPTION_CONTINUE_SEARCH;
    if (s_cliSkipped++ == 0) logf("privileged %s at %08lX skipped (further ones are skipped silently)", op == 0xFA ? "CLI" : "STI", *eip);
    *eip += 1;
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* ------------------------------------------------------------------ */
/* Option: joystick read interval. ReadDevice() re-reads a joystick at  */
/* most every 50 ms (gameport reads were expensive in 1996).            */
/* ------------------------------------------------------------------ */
static void patch_joy_interval(void)
{
    DWORD fn = sms_find(&s_sms, "?ReadDevice@@YA?AW4JOYRESULT@@K@Z");
    int ms = (int)GetPrivateProfileIntA("input", "JoystickPollMs", 50, g_iniPath);
    BYTE *p, v;
    int i;
    if (ms == 50) return;
    if (ms < 1) ms = 1;
    if (ms > 127) ms = 127;
    if (!fn) { logf("joystick poll: ReadDevice not in symbol map"); return; }
    p = (BYTE *)fn;
    for (i = 0; i < 0x60; i++)                 /* sub eax,ecx / cmp eax,32h / jae */
        if (p[i] == 0x2B && p[i + 1] == 0xC1 && p[i + 2] == 0x83 && p[i + 3] == 0xF8 && p[i + 4] == 0x32 && p[i + 5] == 0x73) {
            v = (BYTE)ms;
            if (write_code(p + i + 4, &v, 1)) logf("joystick poll: every %d ms (game default 50)", ms);
            return;
        }
    logf("joystick poll: interval check not found in ReadDevice; not patched");
}

/* ------------------------------------------------------------------ */
void patches_apply(void)
{
    BYTE *nt;
    DWORD entry, stamp;
    HMODULE exe = GetModuleHandleA(NULL);

    s_base = (BYTE *)exe;
    nt = s_base + rd32(s_base + 0x3c);
    stamp = rd32(nt + 8);
    entry = (DWORD)s_base + rd32(nt + 0x28);
    logf("exe: base %08lX, entry %08lX, timestamp %08lX", (DWORD)s_base, entry, stamp);

    s_imageLo = (DWORD)s_base;
    s_imageHi = (DWORD)s_base + rd32(nt + 0x50);          /* SizeOfImage */
    logf("CLI/STI handler: %s", AddVectoredExceptionHandler(1, priv_handler) ? "installed" : "FAILED");

    /* window / cursor hooks: independent of the game build */
    logf("hook CreateWindowExA: %s", iat_hook(exe, "USER32.dll", "CreateWindowExA", (void *)hook_CreateWindowExA, NULL) ? "ok" : "FAILED");
    logf("hook GetCursorPos:   %s", iat_hook(exe, "USER32.dll", "GetCursorPos", (void *)hook_GetCursorPos, NULL) ? "ok" : "not imported");
    logf("hook ShowCursor:     %s", iat_hook(exe, "USER32.dll", "ShowCursor", (void *)hook_ShowCursor, NULL) ? "ok" : "not imported");
    logf("hook ClipCursor:     %s", iat_hook(exe, "USER32.dll", "ClipCursor", (void *)hook_ClipCursor, NULL) ? "ok" : "not imported (1.0)");

    if (input_enabled()) {
        int ok = iat_hook(exe, "WINMM.dll", "joyGetNumDevs", (void *)hook_joyGetNumDevs, NULL);
        ok &= iat_hook(exe, "WINMM.dll", "joyGetDevCapsA", (void *)hook_joyGetDevCapsA, NULL);
        ok &= iat_hook(exe, "WINMM.dll", "joyGetPos", (void *)hook_joyGetPos, NULL);
        ok &= iat_hook(exe, "WINMM.dll", "joyGetPosEx", (void *)hook_joyGetPosEx, NULL);
        logf("hook joystick (DirectInput/XInput): %s", ok ? "ok" : "FAILED (game uses Windows joysticks directly)");
    } else logf("joystick: [input] Enable=0, the game reads Windows joysticks directly");

    if (!sms_load()) return;
    if (sms_find(&s_sms, "_WinMainCRTStartup") != entry) {
        logf("USNF.SMS does not match USNF.EXE (entry point differs): exe fixes disabled");
        return;
    }
    patch_cdpath();
    patch_joy_interval();
    if (g_cfg.fixGameSpeed) patch_timeupdate();
    else logf("speed fix: disabled in ini");
}
