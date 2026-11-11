/* usnf97fix: window management and presentation.
 *
 * The game's front buffer (8-bit + palette) is drawn into its window with
 * StretchDIBits from a dedicated thread, so palette changes made from the game's
 * timer thread and partial-scanline blits from the render thread both end up on
 * screen without the game ever owning the display. The real display mode is never
 * changed. Windowed and borderless-fullscreen are supported (Alt+Enter toggles). */
#include "shim.h"

#define WM_USNF_LAYOUT (WM_APP + 0x51)

static HWND      s_hwnd;
static WNDPROC   s_origProc;
static volatile int s_modeW = 640, s_modeH = 480;
static DDSurface *s_primary;           /* guarded by g_ddLock */
static HANDLE    s_event;
static volatile LONG s_dirty, s_force;
static int       s_borderless;
static RECT      s_savedWindowRect;
static int       s_haveSavedRect;
static int       s_cursorCount;        /* ShowCursor counter as the game sees it */
static BYTE     *s_stage;
static unsigned  s_stageSize;
static BITMAPINFO256 s_bmi;
static HRESULT (WINAPI *s_DwmFlush)(void);
static LONGLONG  s_vblankBase;
static LRESULT CALLBACK subclass_proc(HWND, UINT, WPARAM, LPARAM);

DDSurface *present_primary(void) { return s_primary; }
HWND present_hwnd(void) { return s_hwnd; }
int present_is_primary_mem(const DDSurface *s) { return s && s == s_primary; }

/* -------- geometry -------- */
static void logical_size(int *w, int *h)
{
    *w = s_modeW; *h = s_modeH;
    if (g_cfg.aspectCorrect && *w == 320 && *h == 200) *h = 240;   /* CRT 4:3 */
}

/* Where the game image lands inside a client area of cw x ch. */
static void calc_dest(int cw, int ch, RECT *r)
{
    int lw, lh, dw, dh;
    logical_size(&lw, &lh);
    if (g_cfg.stretch || lw <= 0 || lh <= 0) { r->left = r->top = 0; r->right = cw; r->bottom = ch; return; }
    if (cw * lh <= ch * lw) { dw = cw; dh = cw * lh / lw; }     /* 32-bit is plenty (<= 8k x 4k) */
    else { dh = ch; dw = ch * lw / lh; }
    r->left = (cw - dw) / 2; r->top = (ch - dh) / 2;
    r->right = r->left + dw; r->bottom = r->top + dh;
}

static void client_to_game(int x, int y, int *gx, int *gy)
{
    RECT c, d;
    int dw, dh;
    if (!s_hwnd || !GetClientRect(s_hwnd, &c)) { *gx = x; *gy = y; return; }
    calc_dest(c.right, c.bottom, &d);
    dw = d.right - d.left; dh = d.bottom - d.top;
    if (dw <= 0 || dh <= 0) { *gx = x; *gy = y; return; }
    *gx = (x - d.left) * s_modeW / dw;
    *gy = (y - d.top) * s_modeH / dh;
    if (*gx < 0) *gx = 0;
    if (*gy < 0) *gy = 0;
    if (*gx >= s_modeW) *gx = s_modeW - 1;
    if (*gy >= s_modeH) *gy = s_modeH - 1;
}

/* -------- window layout (UI thread only) -------- */
#define STYLE_WINDOWED (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME)

static void apply_layout(HWND hwnd)
{
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        mi.rcMonitor.left = mi.rcMonitor.top = 0;
        mi.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN); mi.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
        mi.rcWork = mi.rcMonitor;
    }
    if (s_borderless) {
        SetWindowLongA(hwnd, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
        SetWindowPos(hwnd, NULL, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        logf("layout: borderless %ldx%ld", mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
    } else {
        RECT r;
        SetWindowLongA(hwnd, GWL_STYLE, (LONG)(STYLE_WINDOWED | WS_VISIBLE));
        if (s_haveSavedRect) {
            r = s_savedWindowRect;
        } else {
            int workW = mi.rcWork.right - mi.rcWork.left, workH = mi.rcWork.bottom - mi.rcWork.top;
            int scale = g_cfg.windowScale, cw, ch;
            if (!scale) {   /* largest whole multiple of 640x480 that fits in 90% of the work area */
                int sx = workW * 9 / 10 / 640, sy = workH * 9 / 10 / 480;
                scale = sx < sy ? sx : sy;
                if (scale < 1) scale = 1;
            }
            cw = 640 * scale; ch = 480 * scale;
            r.left = 0; r.top = 0; r.right = cw; r.bottom = ch;
            AdjustWindowRectEx(&r, STYLE_WINDOWED, FALSE, WS_EX_APPWINDOW);
            {
                int ww = r.right - r.left, wh = r.bottom - r.top;
                r.left = mi.rcWork.left + (workW - ww) / 2;
                r.top  = mi.rcWork.top + (workH - wh) / 2;
                if (r.top < mi.rcWork.top) r.top = mi.rcWork.top;
                r.right = r.left + ww; r.bottom = r.top + wh;
            }
            logf("layout: windowed, client %dx%d (scale %d)", cw, ch, scale);
        }
        SetWindowPos(hwnd, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    }
    s_force = 1;
    if (s_event) SetEvent(s_event);
}

static void toggle_borderless(HWND hwnd)
{
    if (!s_borderless) {
        RECT r;
        /* remember the windowed placement to come back to it */
        if (GetWindowRect(hwnd, &r)) { s_savedWindowRect = r; s_haveSavedRect = 1; }
    }
    s_borderless = !s_borderless;
    apply_layout(hwnd);
}

static void subclass(HWND hwnd)
{
    if (s_origProc || !hwnd) return;
    s_hwnd = hwnd;
    s_origProc = (WNDPROC)GetWindowLongA(hwnd, GWL_WNDPROC);
    SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)subclass_proc);
}

/* -------- IAT hooks (exe only) -------- */
HWND WINAPI hook_CreateWindowExA(DWORD ex, const char *cls, const char *title, DWORD style,
                                 int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, void *param)
{
    HWND hwnd;
    (void)style; (void)x; (void)y; (void)w; (void)h;
    if (parent) return CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    SetProcessDPIAware();   /* real pixels; no DWM bitmap stretching on high-DPI desktops */
    s_borderless = g_cfg.windowMode == 1;
    hwnd = CreateWindowExA(ex & ~WS_EX_TOPMOST, cls, title, STYLE_WINDOWED, 0, 0, 640, 480, parent, menu, inst, param);
    logf("CreateWindowExA: game window %08lX (requested style %08lX %dx%d)", (DWORD)hwnd, style, w, h);
    if (hwnd) {
        subclass(hwnd);
        apply_layout(hwnd);
    }
    return hwnd;
}

BOOL WINAPI hook_GetCursorPos(POINT *p)
{
    POINT q;
    int gx, gy;
    if (!p) return FALSE;
    if (!GetCursorPos(&q)) return FALSE;
    if (!s_hwnd) { *p = q; return TRUE; }
    ScreenToClient(s_hwnd, &q);
    client_to_game(q.x, q.y, &gx, &gy);
    p->x = gx; p->y = gy;
    return TRUE;
}

int WINAPI hook_ShowCursor(BOOL show)
{
    /* Track the game's wish without hiding the cursor over the title bar / other apps;
     * inside the client area WM_SETCURSOR handles it (the game draws its own pointer). */
    s_cursorCount += show ? 1 : -1;
    return s_cursorCount;
}

/* -------- window procedure -------- */
static LRESULT CALLBACK subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_USNF_LAYOUT:
        apply_layout(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        s_force = 1;
        if (s_event) SetEvent(s_event);
        return 0;
    }
    case WM_SIZE:
    case WM_EXITSIZEMOVE:
        s_force = 1;
        if (s_event) SetEvent(s_event);
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(s_cursorCount < 0 ? NULL : LoadCursorA(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcA(hwnd, msg, wp, lp);
    case WM_ACTIVATEAPP:
        if (!wp && !g_cfg.pauseInBackground) return 0;   /* keep running when unfocused */
        break;
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN && (lp & (1 << 29))) { toggle_borderless(hwnd); return 0; }
        break;
    case WM_SYSCHAR:
        return 0;                                          /* no "ding" on Alt+key */
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;         /* Alt/F10 must not open the system menu */
        return DefWindowProcA(hwnd, msg, wp, lp);
    default:
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL) {
            int gx, gy;
            client_to_game(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &gx, &gy);
            lp = MAKELPARAM(gx, gy);
        }
        break;
    }
    return CallWindowProcA(s_origProc, hwnd, msg, wp, lp);
}

/* -------- presenter thread -------- */
static void fill_bars(HDC dc, int cw, int ch, const RECT *d)
{
    HBRUSH b = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT r;
    if (d->top > 0)   { r.left = 0; r.top = 0; r.right = cw; r.bottom = d->top; FillRect(dc, &r, b); }
    if (d->bottom < ch) { r.left = 0; r.top = d->bottom; r.right = cw; r.bottom = ch; FillRect(dc, &r, b); }
    if (d->left > 0)  { r.left = 0; r.top = d->top; r.right = d->left; r.bottom = d->bottom; FillRect(dc, &r, b); }
    if (d->right < cw) { r.left = d->right; r.top = d->top; r.right = cw; r.bottom = d->bottom; FillRect(dc, &r, b); }
}

static void present_once(int force)
{
    int w, h, pitch, i;
    RECT c, d;
    HDC dc;
    HWND hwnd = s_hwnd;
    static RECT lastDest;

    if (!hwnd || IsIconic(hwnd)) return;

    LOCK_DD();
    if (!s_primary) { UNLOCK_DD(); return; }
    w = s_primary->w; h = s_primary->h; pitch = s_primary->pitch;
    if ((unsigned)(pitch * h) > s_stageSize) {
        mem_free(s_stage);
        s_stageSize = (unsigned)(pitch * h);
        s_stage = mem_alloc(s_stageSize);
        if (!s_stage) { s_stageSize = 0; UNLOCK_DD(); return; }
    }
    memcpy(s_stage, s_primary->mem, (unsigned)(pitch * h));
    if (s_primary->pal) {
        for (i = 0; i < 256; i++) {
            s_bmi.bmiColors[i].rgbRed   = s_primary->pal->entries[i].peRed;
            s_bmi.bmiColors[i].rgbGreen = s_primary->pal->entries[i].peGreen;
            s_bmi.bmiColors[i].rgbBlue  = s_primary->pal->entries[i].peBlue;
            s_bmi.bmiColors[i].rgbReserved = 0;
        }
    }
    UNLOCK_DD();

    s_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_bmi.bmiHeader.biWidth = pitch;          /* rows are pitch bytes; we crop to w below */
    s_bmi.bmiHeader.biHeight = -h;            /* top-down */
    s_bmi.bmiHeader.biPlanes = 1;
    s_bmi.bmiHeader.biBitCount = 8;
    s_bmi.bmiHeader.biCompression = BI_RGB;
    s_bmi.bmiHeader.biClrUsed = 256;

    if (!GetClientRect(hwnd, &c) || c.right <= 0 || c.bottom <= 0) return;
    calc_dest(c.right, c.bottom, &d);
    dc = GetDC(hwnd);
    if (!dc) return;
    if (force || memcmp(&d, &lastDest, sizeof d)) { fill_bars(dc, c.right, c.bottom, &d); lastDest = d; }
    SetStretchBltMode(dc, g_cfg.filter ? HALFTONE : COLORONCOLOR);
    if (g_cfg.filter) SetBrushOrgEx(dc, 0, 0, NULL);
    StretchDIBits(dc, d.left, d.top, d.right - d.left, d.bottom - d.top, 0, 0, w, h,
                  s_stage, &s_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, dc);
}

static DWORD WINAPI present_thread(void *arg)
{
    (void)arg;
    for (;;) {
        DWORD r = WaitForSingleObject(s_event, s_dirty ? 12 : 250);
        int force = s_force;
        if (r == WAIT_TIMEOUT && !s_dirty && !force) continue;
        s_dirty = 0;
        s_force = 0;
        present_once(force);
        if (g_cfg.vsync && s_DwmFlush) s_DwmFlush();
    }
}

void present_init(void)
{
    HMODULE dwm;
    if (s_event) return;
    s_borderless = g_cfg.windowMode == 1;
    s_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    dwm = LoadLibraryA("dwmapi.dll");
    if (dwm) s_DwmFlush = (HRESULT (WINAPI *)(void))GetProcAddress(dwm, "DwmFlush");
    s_vblankBase = qpc_now();
    CreateThread(NULL, 0, present_thread, NULL, 0, NULL);
    logf("presenter started (vsync=%d, filter=%d, DwmFlush=%s)", g_cfg.vsync, g_cfg.filter, s_DwmFlush ? "yes" : "no");
}

void present_set_window(HWND hwnd)
{
    if (!s_hwnd) { subclass(hwnd); PostMessageA(hwnd, WM_USNF_LAYOUT, 0, 0); }
    else if (hwnd != s_hwnd) logf("SetCooperativeLevel on a different window (%08lX), ignoring", (DWORD)hwnd);
}

void present_set_mode(int w, int h)
{
    s_modeW = w; s_modeH = h;
    s_force = 1;
    if (s_event) SetEvent(s_event);
}

void present_set_primary(DDSurface *p)
{
    LOCK_DD();
    s_primary = p;
    UNLOCK_DD();
    s_force = 1;
    if (s_event) SetEvent(s_event);
}

void present_release_primary(DDSurface *p)
{
    int was;
    LOCK_DD();
    was = s_primary == p;
    if (was) s_primary = NULL;
    UNLOCK_DD();
    if (was) { s_force = 1; if (s_event) SetEvent(s_event); }
}

void present_mark_dirty(int mode)
{
    if (mode == 2) { if (s_dirty && s_event) SetEvent(s_event); return; }
    s_dirty = 1;
    if (mode == 1 && s_event) SetEvent(s_event);
}

void present_wait_vblank(void)
{
    DWORD period = (DWORD)qpc_freq() / 60u;
    LONGLONG now = qpc_now();
    DWORD phase;
    if (!period) return;
    phase = (DWORD)(now - s_vblankBase) % period;
    wait_until(now + (period - phase));
}
