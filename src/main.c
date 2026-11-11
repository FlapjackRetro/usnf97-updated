/* usnf97fix: DLL entry and the ddraw.dll exports. */
#include "shim.h"

static int s_started;

HRESULT WINAPI DirectDrawCreate(GUID *guid, void **out, void *outer)
{
    HRESULT hr;
    (void)outer;
    if (!out) return E_INVALIDARG;
    if (!s_started) {
        s_started = 1;
        timeBeginPeriod(1);       /* Sleep(1) ~= 1 ms for frame pacing; the game never asks for it */
        present_init();
    }
    hr = dd_create(out);
    logf("DirectDrawCreate(guid=%08lX) -> %08lX", (DWORD)guid, (DWORD)hr);
    return hr;
}

typedef BOOL (WINAPI *LPDDENUMCALLBACKA)(GUID *, char *, char *, void *);
HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA cb, void *ctx)
{
    if (!cb) return E_INVALIDARG;
    cb(NULL, "usnf97fix software DirectDraw", "display", ctx);
    return DD_OK;
}

/* Windows application-compatibility shims (AppCompat database or a compatibility mode
 * chosen in the exe's Properties) load these DLLs; some filter Alt+Tab / Windows key. */
static void log_compat(void)
{
    static const char *const k_shimDlls[] = {
        "apphelp.dll", "AcLayers.dll", "AcGenral.dll", "AcXtrnal.dll", "AcSpecfc.dll",
        "AcWinRT.dll", "acppage.dll", "AcRes.dll", "AcDxCompat.dll", 0 };
    char env[512];
    int i;
    DWORD n = GetEnvironmentVariableA("__COMPAT_LAYER", env, sizeof env);
    if (n && n < sizeof env) logf("compat: __COMPAT_LAYER=%s", env);
    else logf("compat: __COMPAT_LAYER not set");
    for (i = 0; k_shimDlls[i]; i++)
        if (GetModuleHandleA(k_shimDlls[i])) logf("compat: %s loaded", k_shimDlls[i]);
}

BOOL WINAPI DllMainCRTStartup(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char exe[260];
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_ddLock);
        config_load();
        log_open();
        crash_logger_install(inst);
        log_raw("[start] log open");
        time_init();
        log_raw("[start] timer ok");
        input_configure();
        log_raw("[start] input config ok");
        GetModuleFileNameA(NULL, exe, sizeof exe);
        logf("usnf97fix %s loaded into %s", SHIM_VERSION, exe);
        logf("config: WindowMode=%d WindowScale=%d Filter=%d AspectCorrect=%d Stretch=%d VSync=%d FrameCap=%d",
             g_cfg.windowMode, g_cfg.windowScale, g_cfg.filter, g_cfg.aspectCorrect, g_cfg.stretch, g_cfg.vsync, g_cfg.frameCap);
        logf("config: FixGameSpeed=%d MinFrameTicks=%d PauseInBackground=%d CDPath='%s'",
             g_cfg.fixGameSpeed, g_cfg.minFrameTicks, g_cfg.pauseInBackground, g_cfg.cdPath);
        patches_apply();
        log_compat();
        log_raw("[start] DllMain done");
    }
    return TRUE;
}
