/* usnf97cfg.exe: controls configurator for usnf97fix.
 *
 * Pick a controller and watch every axis, button and hat move live, then assign
 * them to USNF '97's flight controls and commands. Assignments are kept as
 * profiles in usnf97fix.ini ([input] Profiles=, [profile:NAME] sections). At
 * launch ddraw.dll uses the first profile, in the order set here, whose
 * controllers are all connected; if none is, it falls back to an Xbox pad (auto)
 * or the keyboard. usnf97fix also sets the game's own Flight > Control choices
 * from the profile at every flight start, so nothing has to be changed in-game.
 *
 * Native Win32, no SDK headers and no C runtime; shares input_core.c (source
 * syntax, resolving and evaluation) with ddraw.dll. */
#include "cfg_win.h"
#include "../input_core.h"

/* ------------------------------------------------------------------ */
/* tiny runtime                                                       */
/* ------------------------------------------------------------------ */
void *memset(void *d, int c, unsigned int n) { BYTE *p = d; while (n--) *p++ = (BYTE)c; return d; }
void *memcpy(void *d, const void *s, unsigned int n) { BYTE *a = d; const BYTE *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, unsigned int n)
{
    BYTE *a = d; const BYTE *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
int memcmp(const void *x, const void *y, unsigned int n)
{
    const BYTE *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, int cap) { int i = 0; if (cap <= 0) return; for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static void scat(char *d, const char *s, int cap) { int n = slen(d); if (n < cap) scpy(d + n, s, cap - n); }
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int ieq(const char *a, const char *b) { while (*a && lc(*a) == lc(*b)) a++, b++; return lc(*a) == lc(*b); }
static void trim(char *s)
{
    int n = slen(s), i = 0;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, (unsigned)(n - i + 1));
}
static int to_int(const char *s, int def)
{
    int v = 0, any = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; if (v > 100000) break; }
    return any ? (neg ? -v : v) : def;
}

/* ------------------------------------------------------------------ */
/* DirectInput 8 / XInput (same declarations as src/input.c)          */
/* ------------------------------------------------------------------ */
#define DI_OK                 0
#define DI8DEVCLASS_GAMECTRL  4
#define DIEDFL_ATTACHEDONLY   1
#define DIENUM_CONTINUE       1
#define DISCL_NONEXCLUSIVE    2
#define DISCL_BACKGROUND      8
#define DIDF_ABSAXIS          1
#define DIDFT_AXIS            0x00000003
#define DIDFT_BUTTON          0x0000000C
#define DIDFT_POV             0x00000010
#define DIDFT_ANYINSTANCE     0x00FFFF00
#define DIDFT_OPTIONAL        0x80000000
#define DIDOI_ASPECTPOSITION  0x00000100
#define DIPH_DEVICE           0
#define DIPROP_RANGE          ((const GUID *)4)
#define DIPROP_GUIDANDPATH    ((const GUID *)12)

typedef struct { const GUID *pguid; DWORD dwOfs, dwType, dwFlags; } DIOBJECTDATAFORMAT;
typedef struct { DWORD dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs; DIOBJECTDATAFORMAT *rgodf; } DIDATAFORMAT;
typedef struct { DWORD dwSize, dwHeaderSize, dwObj, dwHow; } DIPROPHEADER;
typedef struct { DIPROPHEADER h; LONG lMin, lMax; } DIPROPRANGE;
typedef struct { DIPROPHEADER h; GUID guidClass; WORD wszPath[260]; } DIPROPGUIDANDPATH;
typedef struct {
    DWORD dwSize; GUID guidInstance; GUID guidProduct; DWORD dwDevType;
    char tszInstanceName[260]; char tszProductName[260]; GUID guidFFDriver; WORD wUsagePage, wUsage;
} DIDEVICEINSTANCEA;
typedef struct {
    DWORD dwSize; GUID guidType; DWORD dwOfs, dwType, dwFlags; char tszName[260];
    DWORD dwFFMaxForce, dwFFForceResolution; WORD wCollectionNumber, wDesignatorIndex, wUsagePage, wUsage;
    DWORD dwDimension; WORD wExponent, wReportId;
} DIDEVICEOBJECTINSTANCEA;
typedef struct { DWORD dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs, r[5]; } DIDEVCAPS;
typedef struct { LONG ax[8]; DWORD pov[4]; BYTE btn[128]; } DIRAW;
typedef char check_didi[sizeof(DIDEVICEINSTANCEA) == 580 ? 1 : -1];
typedef char check_didoi[sizeof(DIDEVICEOBJECTINSTANCEA) == 316 ? 1 : -1];
typedef char check_raw[sizeof(DIRAW) == 176 ? 1 : -1];

typedef BOOL (WINAPI *DIENUMDEVCB)(const DIDEVICEINSTANCEA *, void *);
typedef BOOL (WINAPI *DIENUMOBJCB)(const DIDEVICEOBJECTINSTANCEA *, void *);
#define VT(obj) (*(void ***)(obj))
#define DI_Release(o)               ((ULONG (WINAPI *)(void *))VT(o)[2])(o)
#define DI_CreateDevice(o, g, d)    ((HRESULT (WINAPI *)(void *, const GUID *, void **, void *))VT(o)[3])(o, g, d, NULL)
#define DI_EnumDevices(o, t, cb, c) ((HRESULT (WINAPI *)(void *, DWORD, DIENUMDEVCB, void *, DWORD))VT(o)[4])(o, t, cb, c, DIEDFL_ATTACHEDONLY)
#define DEV_GetCaps(o, c)           ((HRESULT (WINAPI *)(void *, DIDEVCAPS *))VT(o)[3])(o, c)
#define DEV_EnumObjects(o, cb, c)   ((HRESULT (WINAPI *)(void *, DIENUMOBJCB, void *, DWORD))VT(o)[4])(o, cb, c, 0)
#define DEV_GetProperty(o, g, h)    ((HRESULT (WINAPI *)(void *, const GUID *, DIPROPHEADER *))VT(o)[5])(o, g, h)
#define DEV_SetProperty(o, g, h)    ((HRESULT (WINAPI *)(void *, const GUID *, const DIPROPHEADER *))VT(o)[6])(o, g, h)
#define DEV_Acquire(o)              ((HRESULT (WINAPI *)(void *))VT(o)[7])(o)
#define DEV_Unacquire(o)            ((HRESULT (WINAPI *)(void *))VT(o)[8])(o)
#define DEV_GetState(o, n, p)       ((HRESULT (WINAPI *)(void *, DWORD, void *))VT(o)[9])(o, n, p)
#define DEV_SetDataFormat(o, f)     ((HRESULT (WINAPI *)(void *, const DIDATAFORMAT *))VT(o)[11])(o, f)
#define DEV_SetCoop(o, h, f)        ((HRESULT (WINAPI *)(void *, HWND, DWORD))VT(o)[13])(o, h, f)
#define DEV_Poll(o)                 ((HRESULT (WINAPI *)(void *))VT(o)[25])(o)

static const GUID IID_IDirectInput8A_ = {0xBF798030,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
#define DIGUID(n) {0xA36D02##n,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}}
static const GUID GUID_XAxis_ = DIGUID(E0), GUID_YAxis_ = DIGUID(E1), GUID_ZAxis_ = DIGUID(E2),
                  GUID_RxAxis_ = DIGUID(F4), GUID_RyAxis_ = DIGUID(F5), GUID_RzAxis_ = DIGUID(E3),
                  GUID_Slider_ = DIGUID(E4), GUID_POV_ = DIGUID(F2);

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; short sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;

/* ------------------------------------------------------------------ */
/* devices                                                            */
/* ------------------------------------------------------------------ */
#define MAXDEV 16
typedef struct {
    InDevInfo info;
    InDevState st;
    char pat[64];             /* device pattern written to the ini */
    char axName[8][40];       /* DirectInput object names ("Z Axis", "Dial") */
    void *di;
    GUID guid;
} CDev;

static void *s_di;
static DWORD (WINAPI *s_XInputGetState)(DWORD, XINPUT_STATE *);
static CDev s_dev[MAXDEV];               /* DirectInput devices first, then the 4 XInput slots */
static int  s_ndev, s_nDi;
static DIOBJECTDATAFORMAT s_objs[8 + 4 + 128];
static DIDATAFORMAT s_fmt;
static DWORD s_padRetry[4];

/* background scan (EnumDevices can take over a second) */
static CDev s_scan[MAXDEV];
static int  s_nscan;
static volatile int s_scanning;
static HWND s_hwnd;
#define WM_SCANDONE (WM_APP + 1)

static void build_format(void)
{
    static const GUID *const g[8] = { &GUID_XAxis_, &GUID_YAxis_, &GUID_ZAxis_, &GUID_RxAxis_, &GUID_RyAxis_, &GUID_RzAxis_, &GUID_Slider_, &GUID_Slider_ };
    int i, n = 0;
    for (i = 0; i < 8; i++, n++) { s_objs[n].pguid = g[i]; s_objs[n].dwOfs = (DWORD)(i * 4); s_objs[n].dwType = DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; s_objs[n].dwFlags = DIDOI_ASPECTPOSITION; }
    for (i = 0; i < 4; i++, n++) { s_objs[n].pguid = &GUID_POV_; s_objs[n].dwOfs = (DWORD)(32 + i * 4); s_objs[n].dwType = DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; s_objs[n].dwFlags = 0; }
    for (i = 0; i < 128; i++, n++) { s_objs[n].pguid = NULL; s_objs[n].dwOfs = (DWORD)(48 + i); s_objs[n].dwType = DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; s_objs[n].dwFlags = 0; }
    s_fmt.dwSize = sizeof s_fmt; s_fmt.dwObjSize = sizeof(DIOBJECTDATAFORMAT); s_fmt.dwFlags = DIDF_ABSAXIS;
    s_fmt.dwDataSize = sizeof(DIRAW); s_fmt.dwNumObjs = (DWORD)n; s_fmt.rgodf = s_objs;
}

static BOOL WINAPI obj_cb(const DIDEVICEOBJECTINSTANCEA *o, void *ctx)
{
    CDev *d = ctx;
    if (o->dwOfs < 32 && (o->dwType & DIDFT_AXIS)) {
        int a = (int)(o->dwOfs / 4);
        d->info.axisMask |= 1 << a;
        scpy(d->axName[a], o->tszName, sizeof d->axName[a]);
    }
    return DIENUM_CONTINUE;
}

static int path_is_xinput(void *dev)
{
    static DIPROPGUIDANDPATH p;
    int i;
    memset(&p, 0, sizeof p);
    p.h.dwSize = sizeof p; p.h.dwHeaderSize = sizeof p.h; p.h.dwHow = DIPH_DEVICE;
    if (DEV_GetProperty(dev, DIPROP_GUIDANDPATH, &p.h) != DI_OK) return 0;
    for (i = 0; p.wszPath[i] && p.wszPath[i + 1] && p.wszPath[i + 2]; i++)
        if ((p.wszPath[i] == 'I' || p.wszPath[i] == 'i') && (p.wszPath[i + 1] == 'G' || p.wszPath[i + 1] == 'g') && p.wszPath[i + 2] == '_')
            return 1;
    return 0;
}

static BOOL WINAPI enum_cb(const DIDEVICEINSTANCEA *inst, void *ctx)
{
    void *dev = NULL;
    CDev *d;
    DIPROPRANGE r;
    DIDEVCAPS caps;
    (void)ctx;
    if (s_nscan >= MAXDEV - 4) return 0;
    if (DI_CreateDevice(s_di, &inst->guidInstance, &dev) != DI_OK || !dev) return DIENUM_CONTINUE;
    if ((s_XInputGetState && path_is_xinput(dev)) || DEV_SetDataFormat(dev, &s_fmt) != DI_OK) { DI_Release(dev); return DIENUM_CONTINUE; }
    d = &s_scan[s_nscan++];
    memset(d, 0, sizeof *d);
    d->di = dev; d->guid = inst->guidInstance;
    scpy(d->info.name, inst->tszProductName[0] ? inst->tszProductName : inst->tszInstanceName, sizeof d->info.name);
    scpy(d->pat, d->info.name, sizeof d->pat);
    d->info.connected = 1;
    memset(&r, 0, sizeof r);
    r.h.dwSize = sizeof r; r.h.dwHeaderSize = sizeof r.h; r.h.dwHow = DIPH_DEVICE; r.lMin = 0; r.lMax = 65535;
    DEV_SetProperty(dev, DIPROP_RANGE, &r.h);
    memset(&caps, 0, sizeof caps); caps.dwSize = sizeof caps;
    DEV_GetCaps(dev, &caps);
    d->info.nButtons = (int)(caps.dwButtons > 128 ? 128 : caps.dwButtons);
    d->info.nPovs = (int)(caps.dwPOVs > 4 ? 4 : caps.dwPOVs);
    DEV_EnumObjects(dev, obj_cb, d);
    return DIENUM_CONTINUE;
}

static DWORD WINAPI scan_thread(void *arg)
{
    (void)arg;
    s_nscan = 0;
    if (s_di) DI_EnumDevices(s_di, DI8DEVCLASS_GAMECTRL, enum_cb, NULL);
    PostMessageA(s_hwnd, WM_SCANDONE, 0, 0);
    return 0;
}

static void start_scan(void)
{
    HANDLE h;
    if (s_scanning) return;
    s_scanning = 1;
    h = CreateThread(NULL, 0, scan_thread, NULL, 0, NULL);
    if (h) CloseHandle(h); else s_scanning = 0;
}

static void add_pad_slots(void)
{
    int i;
    for (i = 0; i < 4 && s_ndev < MAXDEV; i++) {
        CDev *d = &s_dev[s_ndev++];
        memset(d, 0, sizeof *d);
        wsprintfA(d->info.name, "Xbox controller %d (XInput)", i + 1);
        scpy(d->pat, "pad", sizeof d->pat);
        d->info.isPad = 1; d->info.padIndex = i + 1;
        d->info.axisMask = 0x3f; d->info.nButtons = 10; d->info.nPovs = 1;
        d->st.ax[0] = d->st.ax[1] = d->st.ax[3] = d->st.ax[4] = IN_AXIS_MID;
        d->st.pov[0] = IN_POV_CENTER;
    }
}

/* scan finished: swap in the new DirectInput list (keeps pad slots) */
static void apply_scan(void)
{
    int i;
    for (i = 0; i < s_nDi; i++) if (s_dev[i].di) { DEV_Unacquire(s_dev[i].di); DI_Release(s_dev[i].di); }
    s_ndev = 0;
    for (i = 0; i < s_nscan; i++) {
        s_dev[s_ndev] = s_scan[i];
        DEV_SetCoop(s_dev[s_ndev].di, s_hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        DEV_Acquire(s_dev[s_ndev].di);
        s_ndev++;
    }
    s_nDi = s_ndev;
    add_pad_slots();
    s_scanning = 0;
}

static void poll_dev(CDev *d, DWORD now)
{
    int i;
    if (d->di) {
        static DIRAW raw;
        HRESULT hr;
        DEV_Poll(d->di);
        hr = DEV_GetState(d->di, sizeof raw, &raw);
        if (hr != DI_OK) { DEV_Acquire(d->di); hr = DEV_GetState(d->di, sizeof raw, &raw); }
        if (hr != DI_OK) return;
        for (i = 0; i < 8; i++) d->st.ax[i] = raw.ax[i] < 0 ? 0 : raw.ax[i] > 65535 ? 65535 : (int)raw.ax[i];
        for (i = 0; i < 4; i++) d->st.pov[i] = (LOWORD(raw.pov[i]) == 0xFFFF) ? IN_POV_CENTER : raw.pov[i];
        for (i = 0; i < 128; i++) d->st.btn[i] = raw.btn[i] & 0x80;
        return;
    }
    if (d->info.isPad && s_XInputGetState) {
        static const WORD bits[10] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080 };
        XINPUT_STATE xs;
        int k = d->info.padIndex - 1, u, r, dn, l;
        if (!d->info.connected && now < s_padRetry[k]) return;
        if (s_XInputGetState((DWORD)k, &xs) != 0) { d->info.connected = 0; s_padRetry[k] = now + 1000; return; }
        d->info.connected = 1;
        d->st.ax[0] = xs.Gamepad.sThumbLX + 32768;
        d->st.ax[1] = 65535 - (xs.Gamepad.sThumbLY + 32768);
        d->st.ax[2] = xs.Gamepad.bLeftTrigger * 65535 / 255;
        d->st.ax[3] = xs.Gamepad.sThumbRX + 32768;
        d->st.ax[4] = 65535 - (xs.Gamepad.sThumbRY + 32768);
        d->st.ax[5] = xs.Gamepad.bRightTrigger * 65535 / 255;
        for (i = 0; i < 10; i++) d->st.btn[i] = (xs.Gamepad.wButtons & bits[i]) ? 0x80 : 0;
        u = (xs.Gamepad.wButtons & 1) != 0; dn = (xs.Gamepad.wButtons & 2) != 0;
        l = (xs.Gamepad.wButtons & 4) != 0; r = (xs.Gamepad.wButtons & 8) != 0;
        if (u && r) d->st.pov[0] = 4500; else if (r && dn) d->st.pov[0] = 13500;
        else if (dn && l) d->st.pov[0] = 22500; else if (l && u) d->st.pov[0] = 31500;
        else if (u) d->st.pov[0] = 0; else if (r) d->st.pov[0] = 9000;
        else if (dn) d->st.pov[0] = 18000; else if (l) d->st.pov[0] = 27000;
        else d->st.pov[0] = IN_POV_CENTER;
    }
}

static void dev_infos(InDevInfo *out)
{
    int i;
    for (i = 0; i < s_ndev; i++) out[i] = s_dev[i].info;
}

static const char *axis_label(const CDev *d, int a)
{
    static const char *const padLong[6] = { "left stick X", "left stick Y", "left trigger", "right stick X", "right stick Y", "right trigger" };
    if (d->info.isPad) return a < 6 ? padLong[a] : "";
    return d->axName[a];
}

/* ------------------------------------------------------------------ */
/* functions and profiles                                             */
/* ------------------------------------------------------------------ */
#define NROLE  9
#define MAXF   80
#define MAXS   4
#define MAXPROF 12
enum { F_ROLL, F_PITCH, F_THROTTLE, F_RUDDER, F_HAT, F_B1, F_B2, F_B3, F_B4 };
static const char *const k_role[NROLE] = { "Roll", "Pitch", "Throttle", "Rudder", "Hat", "Button1", "Button2", "Button3", "Button4" };
static const char *const k_roleLabel[NROLE] = { "Roll (stick left/right)", "Pitch (stick fwd/back)", "Throttle", "Rudder",
    "View hat", "Button 1: fire weapon", "Button 2: cycle weapon", "Button 3: gun", "Button 4: next missile" };
static int s_nfunc;

static const char *func_key(int f) { return f < NROLE ? k_role[f] : g_inAliases[f - NROLE].name; }
static const char *func_label(int f) { return f < NROLE ? k_roleLabel[f] : g_inAliases[f - NROLE].what; }
static int func_is_axis(int f) { return f <= F_RUDDER; }

typedef struct {
    char  name[48];
    InSrc src[MAXF][MAXS];
    int   n[MAXF];
    char  extra[1024];     /* lines we don't edit (raw key names), "\n"-separated */
} Profile;

static Profile s_prof[MAXPROF];
static int  s_nprof, s_cur = -1;
static char s_deleted[MAXPROF * 2][48];
static int  s_ndeleted;
static int  s_dirty;
static char s_ini[280];
static char s_status[256];

static void src_text(const InSrc *s, char *out, int cap) { insrc_format(s, out, cap); }

static void func_text(const Profile *p, int f, char *out, int cap)
{
    int j;
    char t[128];
    out[0] = 0;
    for (j = 0; j < p->n[f]; j++) {
        src_text(&p->src[f][j], t, sizeof t);
        if (j) scat(out, ", ", cap);
        scat(out, t, cap);
    }
}

static int find_func(const char *key)
{
    int f;
    for (f = 0; f < s_nfunc; f++) if (ieq(key, func_key(f))) return f;
    return -1;
}

/* parse "a, b" into a function's sources */
static void set_sources(Profile *p, int f, const char *val)
{
    const char *v = val;
    p->n[f] = 0;
    if (ieq(val, "none") || ieq(val, "keyboard") || ieq(val, "auto")) return;
    while (*v && p->n[f] < MAXS) {
        char part[96];
        int k = 0;
        while (*v == ' ' || *v == ',') v++;
        while (*v && *v != ',' && k < (int)sizeof part - 1) part[k++] = *v++;
        part[k] = 0;
        trim(part);
        if (part[0] && insrc_parse(part, &p->src[f][p->n[f]])) p->n[f]++;
    }
}

static void add_line(Profile *p, const char *key, const char *val)
{
    int f = find_func(key);
    if (f >= 0) { set_sources(p, f, val); return; }
    if (slen(p->extra) + slen(key) + slen(val) + 3 < (int)sizeof p->extra) {
        scat(p->extra, key, sizeof p->extra); scat(p->extra, "=", sizeof p->extra);
        scat(p->extra, val, sizeof p->extra); scat(p->extra, "\n", sizeof p->extra);
    }
}

static void load_section(Profile *p, const char *sect, int rolesOnlyIfExplicit)
{
    static char body[16384];
    char *q;
    if (!GetPrivateProfileSectionA(sect, body, sizeof body, s_ini)) return;
    for (q = body; *q; q += slen(q) + 1) {
        char key[64], *eq = q, *val;
        int k;
        if (*q == ';' || *q == '#') continue;
        while (*eq && *eq != '=') eq++;
        if (!*eq) continue;
        k = (int)(eq - q);
        if (k >= (int)sizeof key) continue;
        memcpy(key, q, (unsigned)k); key[k] = 0; trim(key);
        val = eq + 1;
        while (*val == ' ') val++;
        if (rolesOnlyIfExplicit) {            /* legacy [input]: only the role keys */
            int f = find_func(key);
            if (f < 0 || f >= NROLE) continue;
        }
        add_line(p, key, val);
    }
}

static int profile_index(const char *name)
{
    int i;
    for (i = 0; i < s_nprof; i++) if (ieq(s_prof[i].name, name)) return i;
    return -1;
}

static int profile_assigned(const Profile *p)
{
    int f, n = 0;
    for (f = 0; f < s_nfunc; f++) n += p->n[f];
    return n;
}

/* "connected" / "missing: X" for a profile */
static int profile_ready(const Profile *p, char *missing, int cap)
{
    InDevInfo info[MAXDEV];
    int f, j;
    if (missing) missing[0] = 0;
    dev_infos(info);
    for (f = 0; f < s_nfunc; f++)
        for (j = 0; j < p->n[f]; j++) {
            InSrc s = p->src[f][j];
            if (!insrc_resolve(&s, info, s_ndev) || !info[s.dev].connected) {
                if (missing) scpy(missing, p->src[f][j].devPat, cap);
                return 0;
            }
        }
    return profile_assigned(p) > 0;
}

static void load_all(void)
{
    char list[512], *p;
    int i;
    s_nprof = 0;
    GetPrivateProfileStringA("input", "Profiles", "", list, sizeof list, s_ini);
    for (p = list; *p && s_nprof < MAXPROF; ) {
        char name[48], sect[80];
        int k = 0;
        while (*p == ' ' || *p == ',') p++;
        while (*p && *p != ',' && k < (int)sizeof name - 1) name[k++] = *p++;
        name[k] = 0; trim(name);
        if (!name[0] || profile_index(name) >= 0) continue;
        memset(&s_prof[s_nprof], 0, sizeof s_prof[0]);
        scpy(s_prof[s_nprof].name, name, sizeof s_prof[0].name);
        scpy(sect, "profile:", sizeof sect); scat(sect, name, sizeof sect);
        load_section(&s_prof[s_nprof], sect, 0);
        s_nprof++;
    }
    if (s_nprof) { s_cur = 0; return; }

    /* first run: import the hand-written [input] / [keys] mapping, if any */
    memset(&s_prof[0], 0, sizeof s_prof[0]);
    load_section(&s_prof[0], "input", 1);
    load_section(&s_prof[0], "keys", 0);
    if (profile_assigned(&s_prof[0])) {
        const char *dev = "My controls";
        for (i = 0; i < s_nfunc; i++) if (s_prof[0].n[i]) { dev = s_prof[0].src[i][0].devPat; break; }
        scpy(s_prof[0].name, dev, sizeof s_prof[0].name);
        for (p = s_prof[0].name; *p; p++) if (*p == ',' || *p == '[' || *p == ']' || *p == '=' || *p == ';') *p = ' ';
        s_nprof = 1; s_cur = 0; s_dirty = 1;
        wsprintfA(s_status, "Imported the existing [input]/[keys] mapping as profile \"%s\". Save to start using profiles.", s_prof[0].name);
    }
}

static int save_all(void)
{
    static char body[16384];
    char list[512], sect[80], bak[290], line[400], t[300];
    int i, f, len;
    scpy(bak, s_ini, sizeof bak); scat(bak, ".bak", sizeof bak);
    if (GetFileAttributesA(bak) == INVALID_FILE_ATTRIBUTES) CopyFileA(s_ini, bak, TRUE);
    list[0] = 0;
    for (i = 0; i < s_nprof; i++) {
        Profile *p = &s_prof[i];
        char *e;
        len = 0;
        #define PUT(str) do { int L_ = slen(str); if (len + L_ + 2 < (int)sizeof body) { memcpy(body + len, str, (unsigned)L_); len += L_; body[len++] = 0; } } while (0)
        PUT("; written by usnf97cfg.exe");
        for (f = 0; f < s_nfunc; f++) {
            if (!p->n[f]) continue;
            func_text(p, f, t, sizeof t);
            scpy(line, func_key(f), sizeof line); scat(line, "=", sizeof line); scat(line, t, sizeof line);
            PUT(line);
        }
        for (e = p->extra; *e; ) {
            int k = 0;
            while (e[k] && e[k] != '\n' && k < (int)sizeof line - 1) { line[k] = e[k]; k++; }
            line[k] = 0;
            e += k; if (*e == '\n') e++;
            if (line[0]) PUT(line);
        }
        body[len++] = 0;
        #undef PUT
        scpy(sect, "profile:", sizeof sect); scat(sect, p->name, sizeof sect);
        if (!WritePrivateProfileSectionA(sect, body, s_ini)) {
            wsprintfA(s_status, "Could not write %s (error %lu). Is the game folder writable?", s_ini, GetLastError());
            return 0;
        }
        if (i) scat(list, ", ", sizeof list);
        scat(list, p->name, sizeof list);
    }
    for (i = 0; i < s_ndeleted; i++) {
        if (profile_index(s_deleted[i]) >= 0) continue;
        scpy(sect, "profile:", sizeof sect); scat(sect, s_deleted[i], sizeof sect);
        WritePrivateProfileStringA(sect, NULL, NULL, s_ini);
    }
    s_ndeleted = 0;
    WritePrivateProfileStringA("input", "Profiles", list, s_ini);
    s_dirty = 0;
    wsprintfA(s_status, "Saved %d profile(s) to usnf97fix.ini. Changes apply the next time the game starts.", s_nprof);
    return 1;
}

/* ------------------------------------------------------------------ */
/* UI state                                                           */
/* ------------------------------------------------------------------ */
enum {
    ID_PROF = 100, ID_NAME, ID_NEW, ID_RENAME, ID_DELETE, ID_UP, ID_DOWN,
    ID_DEV = 110, ID_RESCAN,
    ID_LISTEN = 120, ID_ADD, ID_CLEAR, ID_INVERT, ID_TAP, ID_THRESH, ID_BELOW,
    ID_DEADZONE = 130, ID_RESPONSE, ID_FALLBACK,
    ID_SAVE = 140, ID_CLOSE
};
static HWND s_cProf, s_cName, s_cDev, s_cInvert, s_cTap, s_cThresh, s_cBelow, s_cDead, s_cResp, s_cFall, s_cListen, s_cAdd;
static HFONT s_font, s_fontB, s_fontS;
static int s_selDev = -1, s_selFunc = F_ROLL, s_selSrc = 0;
static int s_listen;                         /* 0 off, 1 replace, 2 add */
static DWORD s_listenStart;
static InDevState s_base[MAXDEV];
static int s_updatingUi;

#define WIN_W 1220
#define WIN_H 910
#define LIVE_TOP 96
#define LIVE_BOT 352
#define FUNC_TOP 364
#define FUNC_BOT 818

/* hit areas filled while painting */
typedef struct { RECT r; int kind, idx, dir; } Hit;     /* kind: 1 axis, 2 button, 3 pov, 4 function */
static Hit s_hit[400];
static int s_nhit;
static void add_hit(int l, int t, int r, int b, int kind, int idx, int dir)
{
    if (s_nhit < (int)(sizeof s_hit / sizeof s_hit[0])) {
        Hit *h = &s_hit[s_nhit++];
        h->r.left = l; h->r.top = t; h->r.right = r; h->r.bottom = b; h->kind = kind; h->idx = idx; h->dir = dir;
    }
}

static Profile *cur(void) { return (s_cur >= 0 && s_cur < s_nprof) ? &s_prof[s_cur] : NULL; }

static void set_status(const char *s) { scpy(s_status, s, sizeof s_status); InvalidateRect(s_hwnd, NULL, FALSE); }

static void fill_profiles(void)
{
    int i;
    char t[160], miss[64];
    s_updatingUi = 1;
    SendMessageA(s_cProf, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < s_nprof; i++) {
        int ok = profile_ready(&s_prof[i], miss, sizeof miss);
        wsprintfA(t, "%d. %s   %s", i + 1, s_prof[i].name,
                  !profile_assigned(&s_prof[i]) ? "(empty)" : ok ? "(controllers connected)" : "(controller missing)");
        SendMessageA(s_cProf, CB_ADDSTRING, 0, (LPARAM)t);
    }
    if (s_cur >= s_nprof) s_cur = s_nprof - 1;
    SendMessageA(s_cProf, CB_SETCURSEL, (WPARAM)s_cur, 0);
    SetWindowTextA(s_cName, cur() ? cur()->name : "");
    s_updatingUi = 0;
}

static void fill_devices(void)
{
    int i, sel = -1, k = 0;
    char t[200];
    s_updatingUi = 1;
    SendMessageA(s_cDev, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < s_ndev; i++) {
        if (s_dev[i].info.isPad && !s_dev[i].info.connected) continue;
        if (s_dev[i].info.isPad) wsprintfA(t, "%s", s_dev[i].info.name);
        else wsprintfA(t, "%s   (%d buttons, %d hat%s)", s_dev[i].info.name, s_dev[i].info.nButtons, s_dev[i].info.nPovs, s_dev[i].info.nPovs == 1 ? "" : "s");
        SendMessageA(s_cDev, CB_ADDSTRING, 0, (LPARAM)t);
        if (i == s_selDev) sel = k;
        k++;
    }
    if (sel < 0 && k) {                         /* pick the first one */
        for (i = 0; i < s_ndev; i++) if (!s_dev[i].info.isPad || s_dev[i].info.connected) { s_selDev = i; break; }
        sel = 0;
    }
    if (!k) s_selDev = -1;
    SendMessageA(s_cDev, CB_SETCURSEL, (WPARAM)sel, 0);
    s_updatingUi = 0;
}

static int dev_from_combo(int sel)
{
    int i, k = 0;
    for (i = 0; i < s_ndev; i++) {
        if (s_dev[i].info.isPad && !s_dev[i].info.connected) continue;
        if (k++ == sel) return i;
    }
    return -1;
}

/* show the options of the selected source */
static void sync_options(void)
{
    Profile *p = cur();
    InSrc *s = (p && s_selSrc < p->n[s_selFunc]) ? &p->src[s_selFunc][s_selSrc] : NULL;
    char t[16];
    s_updatingUi = 1;
    SendMessageA(s_cInvert, BM_SETCHECK, s && s->invert, 0);
    SendMessageA(s_cTap, BM_SETCHECK, s && s->tap, 0);
    SendMessageA(s_cBelow, BM_SETCHECK, s && s->hasThresh && !s->wantGT, 0);
    wsprintfA(t, "%d", s && s->hasThresh ? s->threshPct : 50);
    SetWindowTextA(s_cThresh, t);
    EnableWindow(s_cInvert, s != NULL);
    EnableWindow(s_cTap, s != NULL && !func_is_axis(s_selFunc) && s_selFunc != F_HAT);
    EnableWindow(s_cThresh, s != NULL && s->hasThresh);
    EnableWindow(s_cBelow, s != NULL && s->hasThresh);
    EnableWindow(s_cAdd, p && p->n[s_selFunc] > 0 && p->n[s_selFunc] < MAXS);
    s_updatingUi = 0;
}

static void select_func(int f)
{
    Profile *p = cur();
    s_selFunc = f;
    s_selSrc = p && p->n[f] ? p->n[f] - 1 : 0;
    s_listen = 0;
    SetWindowTextA(s_cListen, "Listen");
    sync_options();
    if (p) {
        char t[200];
        wsprintfA(t, "%s: %s.  Press Listen and use the control, or click it in the live view above.", func_key(f), func_label(f));
        scpy(s_status, t, sizeof s_status);
    }
    InvalidateRect(s_hwnd, NULL, FALSE);
}

/* put a source on the selected function */
static void assign(int dev, const char *ctl, int threshPct, int gt)
{
    Profile *p = cur();
    InSrc s;
    char text[128], t[16];
    int f = s_selFunc, slot;
    if (!p || dev < 0) return;
    scpy(text, s_dev[dev].pat, sizeof text);
    scat(text, ":", sizeof text);
    scat(text, ctl, sizeof text);
    if (threshPct >= 0) { wsprintfA(t, "%s%d", gt ? ">" : "<", threshPct); scat(text, t, sizeof text); }
    if (!insrc_parse(text, &s)) return;
    /* the same control on the same function only once */
    for (slot = 0; slot < p->n[f]; slot++)
        if (ieq(p->src[f][slot].devPat, s.devPat) && ieq(p->src[f][slot].ctl, s.ctl) && !p->src[f][slot].ctl2[0] &&
            p->src[f][slot].hasThresh == s.hasThresh && (!s.hasThresh || p->src[f][slot].wantGT == s.wantGT)) break;
    if (slot == p->n[f]) {
        int inheritTap = p->n[f] > 0 && p->src[f][p->n[f] - 1].tap;
        if (s_listen == 2 && p->n[f] < MAXS) slot = p->n[f]++;
        else { slot = 0; p->n[f] = 1; inheritTap = 0; }
        s.tap = inheritTap;                      /* a second lever position keeps Tap */
    } else {
        s.invert = p->src[f][slot].invert;     /* keep the options of an existing entry */
        s.tap = p->src[f][slot].tap;
    }
    p->src[f][slot] = s;
    s_selSrc = slot;
    s_dirty = 1;
    s_listen = 0;
    SetWindowTextA(s_cListen, "Listen");
    func_text(p, f, text, sizeof text);
    wsprintfA(s_status, "%s = %s", func_key(f), text);
    sync_options();
    fill_profiles();
    InvalidateRect(s_hwnd, NULL, FALSE);
}

static void pov_ctl(int dev, int idx, int dir, char *out)
{
    static const char dl[4] = { 'U', 'R', 'D', 'L' };
    if (s_dev[dev].info.isPad) { if (dir < 0) scpy(out, "DPAD", 8); else wsprintfA(out, "DPAD%c", dl[dir]); }
    else if (dir < 0) wsprintfA(out, "POV%d", idx + 1);
    else wsprintfA(out, "POV%d%c", idx + 1, dl[dir]);
}

static int pov_dir(unsigned int v)
{
    if (v == IN_POV_CENTER || v >= 36000) return -1;
    return (int)(((v + 4500) % 36000) / 9000);
}

/* assign a control picked by clicking or listening */
static void assign_axis(int dev, int a, int movedUp)
{
    if (func_is_axis(s_selFunc)) assign(dev, in_axis_name(a, s_dev[dev].info.isPad), -1, 0);
    else if (s_selFunc == F_HAT) set_status("The view hat needs a hat switch (POV).");
    else assign(dev, in_axis_name(a, s_dev[dev].info.isPad), 50, movedUp);
}
static void assign_button(int dev, int b)
{
    char t[16];
    if (func_is_axis(s_selFunc) || s_selFunc == F_HAT) { set_status("That function needs an axis (or a hat for the view hat); pick a button function instead."); return; }
    if (s_dev[dev].info.isPad) {
        static const char *const pb[10] = { "A", "B", "X", "Y", "LB", "RB", "BACK", "START", "LS", "RS" };
        if (b >= 10) return;
        assign(dev, pb[b], -1, 0);
    } else { wsprintfA(t, "B%d", b + 1); assign(dev, t, -1, 0); }
}
static void assign_pov(int dev, int idx, int dir)
{
    char t[16];
    if (func_is_axis(s_selFunc)) { set_status("That function needs an axis."); return; }
    pov_ctl(dev, idx, s_selFunc == F_HAT ? -1 : dir, t);
    if (s_selFunc != F_HAT && dir < 0) return;
    assign(dev, t, -1, 0);
}

static void listen_tick(DWORD now)
{
    int d, i;
    if (!s_listen) return;
    if (now - s_listenStart > 15000) { s_listen = 0; SetWindowTextA(s_cListen, "Listen"); set_status("Stopped listening."); return; }
    for (d = 0; d < s_ndev; d++) {
        CDev *v = &s_dev[d];
        InDevState *b = &s_base[d];
        if (v->info.isPad && !v->info.connected) continue;
        for (i = 0; i < 128; i++) if (v->st.btn[i] && !b->btn[i]) { s_selDev = d; fill_devices(); assign_button(d, i); return; }
        for (i = 0; i < IN_POVS; i++) {
            int dir = pov_dir(v->st.pov[i]);
            if (dir >= 0 && pov_dir(b->pov[i]) < 0) { s_selDev = d; fill_devices(); assign_pov(d, i, dir); return; }
        }
        for (i = 0; i < IN_AXES; i++) {
            int delta;
            if (!(v->info.axisMask & (1 << i))) continue;
            delta = v->st.ax[i] - b->ax[i];
            if (delta > 22000 || delta < -22000) { s_selDev = d; fill_devices(); assign_axis(d, i, delta > 0); return; }
        }
    }
}

static void start_listen(int mode)
{
    int d;
    for (d = 0; d < s_ndev; d++) s_base[d] = s_dev[d].st;
    s_listen = mode;
    s_listenStart = GetTickCount();
    SetWindowTextA(s_cListen, "Listening...");
    set_status(func_is_axis(s_selFunc) ? "Move the axis you want all the way, then back." :
               s_selFunc == F_HAT ? "Press the hat in any direction." :
               "Press the button, hat direction or lever position you want (or move an axis past a point).");
}

/* ------------------------------------------------------------------ */
/* painting                                                           */
/* ------------------------------------------------------------------ */
#define C_BG     RGB(30, 33, 38)
#define C_PANEL  RGB(40, 44, 51)
#define C_LINE   RGB(70, 76, 86)
#define C_TEXT   RGB(222, 226, 232)
#define C_DIM    RGB(140, 148, 160)
#define C_ACC    RGB(86, 170, 255)
#define C_ON     RGB(255, 196, 64)
#define C_SEL    RGB(52, 74, 104)
#define C_GOOD   RGB(110, 210, 130)
#define C_BAD    RGB(240, 120, 110)

static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc; HBRUSH br = CreateSolidBrush(c);
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    FillRect(dc, &rc, br);
    DeleteObject(br);
}
static void frame(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc; HBRUSH br = CreateSolidBrush(c);
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    FrameRect(dc, &rc, br);
    DeleteObject(br);
}
static void text(HDC dc, int l, int t, int r, int b, const char *s, COLORREF c, HFONT f, UINT fmt)
{
    RECT rc;
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextA(dc, s, -1, &rc, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

/* which functions in the current profile use this control (for the readout) */
static void users_of(int dev, int kind, int idx, char *out, int cap)
{
    Profile *p = cur();
    InDevInfo info[MAXDEV];
    int f, j;
    out[0] = 0;
    if (!p) return;
    dev_infos(info);
    for (f = 0; f < s_nfunc; f++)
        for (j = 0; j < p->n[f]; j++) {
            InSrc s = p->src[f][j];
            int hit = 0;
            if (!insrc_resolve(&s, info, s_ndev) || s.dev != dev) continue;
            if (kind == 1 && (s.kind == SRC_AXIS || s.kind == SRC_AXIS_GT || s.kind == SRC_AXIS_LT) && s.idx == idx) hit = 1;
            if (kind == 1 && s.kind == SRC_AXISPAIR && (s.idx == idx || s.idx2 == idx)) hit = 1;
            if (kind == 2 && s.kind == SRC_BUTTON && s.idx == idx) hit = 1;
            if (kind == 3 && (s.kind == SRC_POV || s.kind == SRC_POVDIR) && s.idx == idx) hit = 1;
            if (hit) {
                if (out[0]) scat(out, ", ", cap);
                scat(out, func_key(f), cap);
                break;
            }
        }
}

static void paint_live(HDC dc)
{
    CDev *d = (s_selDev >= 0 && s_selDev < s_ndev) ? &s_dev[s_selDev] : NULL;
    char t[200], u[160];
    int i, y, x0, nb, cols, rows, cw, ch;
    fill(dc, 10, LIVE_TOP, WIN_W - 10, LIVE_BOT, C_PANEL);
    if (!d) {
        text(dc, 24, LIVE_TOP, WIN_W - 24, LIVE_BOT, s_scanning ? "Looking for controllers..." : "No controller found. Plug one in and press Rescan.", C_DIM, s_font, DT_CENTER);
        return;
    }
    text(dc, 24, LIVE_TOP + 4, 600, LIVE_TOP + 24, "Axes  (click one to assign it to the selected function)", C_DIM, s_fontS, DT_LEFT);
    y = LIVE_TOP + 28;
    for (i = 0; i < IN_AXES; i++) {
        int v, w;
        if (!(d->info.axisMask & (1 << i))) continue;
        v = d->st.ax[i];
        wsprintfA(t, "%s", in_axis_name(i, d->info.isPad));
        text(dc, 24, y, 64, y + 22, t, C_TEXT, s_fontB, DT_LEFT);
        text(dc, 64, y, 190, y + 22, axis_label(d, i), C_DIM, s_fontS, DT_LEFT);
        fill(dc, 194, y + 4, 494, y + 18, C_BG);
        w = v * 300 / 65535;
        fill(dc, 194, y + 4, 194 + w, y + 18, C_ACC);
        fill(dc, 343, y + 2, 345, y + 20, C_LINE);
        wsprintfA(t, "%d%%", v * 100 / 65535);
        text(dc, 500, y, 548, y + 22, t, C_TEXT, s_fontS, DT_RIGHT);
        users_of(s_selDev, 1, i, u, sizeof u);
        text(dc, 556, y, 700, y + 22, u, C_ON, s_fontS, DT_LEFT);
        add_hit(24, y, 548, y + 22, 1, i, 0);
        y += 26;
    }
    /* hats */
    x0 = 720;
    text(dc, x0, LIVE_TOP + 4, WIN_W - 24, LIVE_TOP + 24, "Hats and buttons  (lit = pressed; click to assign)", C_DIM, s_fontS, DT_LEFT);
    for (i = 0; i < d->info.nPovs && i < 4; i++) {
        int cx = x0 + 34 + i * 86, cy = LIVE_TOP + 70, dir = pov_dir(d->st.pov[i]);
        HPEN pen = CreatePen(PS_SOLID, 2, C_LINE), old = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - 30, cy - 30, cx + 30, cy + 30);
        SelectObject(dc, ob); SelectObject(dc, old); DeleteObject(pen);
        if (d->st.pov[i] != IN_POV_CENTER && d->st.pov[i] < 36000) {
            /* 8-way dot position */
            static const int dx[8] = { 0, 15, 21, 15, 0, -15, -21, -15 }, dy[8] = { -21, -15, 0, 15, 21, 15, 0, -15 };
            int k = (int)(((d->st.pov[i] + 2250) % 36000) / 4500);
            fill(dc, cx + dx[k] - 6, cy + dy[k] - 6, cx + dx[k] + 6, cy + dy[k] + 6, C_ON);
        } else fill(dc, cx - 3, cy - 3, cx + 3, cy + 3, C_DIM);
        (void)dir;
        wsprintfA(t, d->info.isPad ? "DPAD" : "POV%d", i + 1);
        text(dc, cx - 40, cy + 30, cx + 40, cy + 46, t, C_TEXT, s_fontS, DT_CENTER);
        users_of(s_selDev, 3, i, u, sizeof u);
        text(dc, cx - 42, cy - 48, cx + 42, cy - 32, u, C_ON, s_fontS, DT_CENTER);
        add_hit(cx - 30, cy - 30, cx + 30, cy + 30, 3, i, 0);
    }
    /* buttons */
    nb = d->info.nButtons;
    cols = 12; rows = (nb + cols - 1) / cols; if (rows < 1) rows = 1;
    cw = (WIN_W - 24 - x0) / cols;
    ch = (LIVE_BOT - 8 - (LIVE_TOP + 134)) / rows; if (ch > 34) ch = 34;
    for (i = 0; i < nb; i++) {
        int bx = x0 + (i % cols) * cw, by = LIVE_TOP + 134 + (i / cols) * ch, on = d->st.btn[i] != 0;
        static const char *const pb[10] = { "A", "B", "X", "Y", "LB", "RB", "BACK", "START", "LS", "RS" };
        fill(dc, bx + 2, by + 2, bx + cw - 2, by + ch - 2, on ? C_ON : C_BG);
        frame(dc, bx + 2, by + 2, bx + cw - 2, by + ch - 2, C_LINE);
        if (d->info.isPad && i < 10) scpy(t, pb[i], sizeof t); else wsprintfA(t, "B%d", i + 1);
        users_of(s_selDev, 2, i, u, sizeof u);
        if (u[0] && ch >= 30) {
            text(dc, bx + 2, by + 2, bx + cw - 2, by + ch / 2 + 1, t, on ? C_BG : C_TEXT, s_fontS, DT_CENTER);
            text(dc, bx + 3, by + ch / 2 - 1, bx + cw - 3, by + ch - 2, u, on ? C_BG : C_ON, s_fontS, DT_CENTER);
        } else text(dc, bx + 2, by + 2, bx + cw - 2, by + ch - 2, t, on ? C_BG : (u[0] ? C_ON : C_TEXT), s_fontS, DT_CENTER);
        add_hit(bx + 2, by + 2, bx + cw - 2, by + ch - 2, 2, i, 0);
    }
}

/* live value of a function under the current profile */
static int func_value(const Profile *p, int f, int *pressed)
{
    InDevInfo info[MAXDEV];
    int j, v = -1;
    *pressed = 0;
    dev_infos(info);
    for (j = 0; j < p->n[f]; j++) {
        InSrc s = p->src[f][j];
        const InDevState *st;
        if (!insrc_resolve(&s, info, s_ndev)) continue;
        st = &s_dev[s.dev].st;
        if (func_is_axis(f)) { if (v < 0) v = insrc_axis(&s, st); }
        else if (f == F_HAT) { if (s.kind == SRC_POV && st->pov[s.idx] != IN_POV_CENTER) *pressed = 1; }
        else if (insrc_pressed(&s, st)) *pressed = 1;
    }
    return v;
}

static void paint_func_row(HDC dc, int f, int l, int t, int r, int nameW)
{
    Profile *p = cur();
    char v[300];
    int pressed = 0, val = -1, sel = f == s_selFunc, h = 20;
    if (sel) fill(dc, l, t, r, t + h, C_SEL);
    if (p) {
        func_text(p, f, v, sizeof v);
        if (sel && p->n[f] > 1) {                /* mark the control the options edit */
            int j;
            char t[128];
            v[0] = 0;
            for (j = 0; j < p->n[f]; j++) {
                src_text(&p->src[f][j], t, sizeof t);
                if (j) scat(v, ", ", sizeof v);
                if (j == s_selSrc) scat(v, "[", sizeof v);
                scat(v, t, sizeof v);
                if (j == s_selSrc) scat(v, "]", sizeof v);
            }
        }
        val = func_value(p, f, &pressed);
    }
    else v[0] = 0;
    if (pressed) fill(dc, l, t + 3, l + 4, t + h - 3, C_ON);
    text(dc, l + 8, t, l + nameW, t + h, func_key(f), sel ? RGB(255, 255, 255) : C_TEXT, s_fontB, DT_LEFT);
    if (sel && s_listen) scpy(v, "listening... move or press a control", sizeof v);
    if (func_is_axis(f) && val >= 0 && !(sel && s_listen)) {
        /* live output bar; throttle shows power (the game wants low Z = full power) */
        int w = 70, pct = val * 100 / 65535, bx = r - w - 44;
        char t2[16];
        if (f == F_THROTTLE) pct = 100 - pct;
        fill(dc, bx, t + 6, bx + w, t + h - 6, C_BG);
        fill(dc, bx, t + 6, bx + w * pct / 100, t + h - 6, C_GOOD);
        wsprintfA(t2, f == F_THROTTLE ? "%d%% pwr" : "%d%%", pct);
        text(dc, bx + w + 2, t, r - 2, t + h, t2, C_DIM, s_fontS, DT_RIGHT);
        text(dc, l + nameW, t, bx - 6, t + h, v[0] ? v : "-", v[0] ? C_ACC : C_DIM, s_fontS, DT_LEFT);
    } else text(dc, l + nameW, t, r - 4, t + h, v[0] ? v : "-", (sel && s_listen) ? C_ON : v[0] ? C_ACC : C_DIM, s_fontS, DT_LEFT);
    add_hit(l, t, r, t + h, 4, f, 0);
}

static void paint_funcs(HDC dc)
{
    int f, y, col, colW, rowsPer, i;
    fill(dc, 10, FUNC_TOP, WIN_W - 10, FUNC_BOT, C_PANEL);
    text(dc, 20, FUNC_TOP + 2, 440, FUNC_TOP + 22, "Flight controls", C_DIM, s_fontS, DT_LEFT);
    y = FUNC_TOP + 24;
    for (f = 0; f < NROLE; f++, y += 22) paint_func_row(dc, f, 16, y, 470, 90);
    y += 6;
    text(dc, 20, y, 470, y + 20, "Button 1-4 are the game's joystick buttons", C_DIM, s_fontS, DT_LEFT);
    text(dc, 20, y + 18, 470, y + 38, "(order as with Flight > Control > Stick = Joystick).", C_DIM, s_fontS, DT_LEFT);
    text(dc, 20, y + 46, 470, y + 66, "Throttle bar: 100% pwr = full power. Invert if backwards.", C_DIM, s_fontS, DT_LEFT);
    text(dc, 20, y + 64, 470, y + 84, "Lever positions: tick Tap so each position sends one key press.", C_DIM, s_fontS, DT_LEFT);

    text(dc, 486, FUNC_TOP + 2, WIN_W - 20, FUNC_TOP + 22, "Game commands (keys sent to the game)", C_DIM, s_fontS, DT_LEFT);
    rowsPer = (s_nfunc - NROLE + 2) / 3;
    colW = (WIN_W - 20 - 482) / 3;
    for (i = 0; i < s_nfunc - NROLE; i++) {
        col = i / rowsPer;
        paint_func_row(dc, NROLE + i, 482 + col * colW, FUNC_TOP + 24 + (i % rowsPer) * 20, 482 + (col + 1) * colW - 4, 104);
    }
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd, &ps), dc;
    HBITMAP bmp;
    HGDIOBJ oldBmp;
    RECT rc;
    char t[300];
    Profile *p = cur();
    GetClientRect(hwnd, &rc);
    dc = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    s_nhit = 0;
    fill(dc, 0, 0, rc.right, rc.bottom, C_BG);
    text(dc, 12, 10, 70, 34, "Profile", C_TEXT, s_font, DT_LEFT);
    text(dc, 12, 42, 80, 66, "Controller", C_TEXT, s_font, DT_LEFT);
    if (p) {
        char miss[64];
        int ok = profile_ready(p, miss, sizeof miss);
        if (!profile_assigned(p)) scpy(t, "This profile is empty: select a function below, then Listen or click a control.", sizeof t);
        else if (ok) scpy(t, "All controllers this profile uses are connected.", sizeof t);
        else wsprintfA(t, "Not usable right now: \"%s\" is not connected.", miss);
        text(dc, 12, 70, WIN_W - 12, 92, t, ok ? C_GOOD : C_DIM, s_fontS, DT_LEFT);
    } else text(dc, 12, 70, WIN_W - 12, 92, "No profiles yet: press New to create one for the selected controller.", C_DIM, s_fontS, DT_LEFT);
    paint_live(dc);
    paint_funcs(dc);
    text(dc, 14, FUNC_BOT + 4, WIN_W - 12, FUNC_BOT + 24, s_status, s_dirty ? C_ON : C_DIM, s_fontS, DT_LEFT);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/* commands                                                           */
/* ------------------------------------------------------------------ */
static int valid_name(const char *n)
{
    if (!n[0]) return 0;
    for (; *n; n++) if (*n == ',' || *n == '[' || *n == ']' || *n == '=' || *n == ';' || *n == ':') return 0;
    return 1;
}

static void cmd_new(void)
{
    char name[48];
    int i, k = 1;
    if (s_nprof >= MAXPROF) { set_status("Too many profiles."); return; }
    if (s_selDev >= 0) {
        /* name it after the controller: first two words */
        const char *s = s_dev[s_selDev].info.isPad ? "Xbox pad" : s_dev[s_selDev].info.name;
        int w = 0, j = 0;
        for (i = 0; s[i] && j < 40; i++) { if (s[i] == ' ' && ++w == 2) break; if (s[i] != ',' && s[i] != '[' && s[i] != ']' && s[i] != '=' && s[i] != ';' && s[i] != ':') name[j++] = s[i]; }
        name[j] = 0; trim(name);
    } else scpy(name, "Profile", sizeof name);
    if (!name[0]) scpy(name, "Profile", sizeof name);
    if (profile_index(name) >= 0) {
        char base[40];
        scpy(base, name, sizeof base);
        do wsprintfA(name, "%s %d", base, ++k); while (profile_index(name) >= 0);
    }
    memset(&s_prof[s_nprof], 0, sizeof s_prof[0]);
    scpy(s_prof[s_nprof].name, name, sizeof s_prof[0].name);
    s_cur = s_nprof++;
    s_dirty = 1;
    fill_profiles();
    select_func(F_ROLL);
    set_status("New profile. Select a function, then press Listen (or click a control in the live view).");
}

static void cmd_rename(void)
{
    char name[48];
    Profile *p = cur();
    if (!p) return;
    GetWindowTextA(s_cName, name, sizeof name);
    trim(name);
    if (!valid_name(name)) { set_status("Profile names can't be empty or contain , ; : = [ ]"); return; }
    if (ieq(name, p->name)) return;
    if (profile_index(name) >= 0) { set_status("There is already a profile with that name."); return; }
    if (s_ndeleted < (int)(sizeof s_deleted / sizeof s_deleted[0])) scpy(s_deleted[s_ndeleted++], p->name, 48);
    scpy(p->name, name, sizeof p->name);
    s_dirty = 1;
    fill_profiles();
    set_status("Renamed.");
}

static void cmd_delete(void)
{
    Profile *p = cur();
    char t[120];
    if (!p) return;
    wsprintfA(t, "Delete profile \"%s\"?", p->name);
    if (MessageBoxA(s_hwnd, t, "usnf97 controls", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (s_ndeleted < (int)(sizeof s_deleted / sizeof s_deleted[0])) scpy(s_deleted[s_ndeleted++], p->name, 48);
    memmove(&s_prof[s_cur], &s_prof[s_cur + 1], (unsigned)(s_nprof - s_cur - 1) * sizeof s_prof[0]);
    s_nprof--;
    if (s_cur >= s_nprof) s_cur = s_nprof - 1;
    s_dirty = 1;
    fill_profiles();
    select_func(s_selFunc);
}

static void cmd_move(int delta)
{
    static Profile tmp;
    int to = s_cur + delta;
    if (s_cur < 0 || to < 0 || to >= s_nprof) return;
    tmp = s_prof[s_cur]; s_prof[s_cur] = s_prof[to]; s_prof[to] = tmp;
    s_cur = to;
    s_dirty = 1;
    fill_profiles();
    set_status("Order changed: at launch the first usable profile in this list wins.");
}

static void cmd_clear(void)
{
    Profile *p = cur();
    if (!p || !p->n[s_selFunc]) return;
    if (s_selSrc < p->n[s_selFunc]) {
        memmove(&p->src[s_selFunc][s_selSrc], &p->src[s_selFunc][s_selSrc + 1], (unsigned)(p->n[s_selFunc] - s_selSrc - 1) * sizeof(InSrc));
        p->n[s_selFunc]--;
    }
    s_selSrc = p->n[s_selFunc] ? p->n[s_selFunc] - 1 : 0;
    s_dirty = 1;
    sync_options();
    fill_profiles();
    set_status(p->n[s_selFunc] ? "Removed one control (press Clear again to remove the next)." : "Cleared: this function stays on the keyboard.");
}

static void option_changed(int id)
{
    Profile *p = cur();
    InSrc *s;
    if (s_updatingUi || !p || s_selSrc >= p->n[s_selFunc]) return;
    s = &p->src[s_selFunc][s_selSrc];
    if (id == ID_INVERT) s->invert = SendMessageA(s_cInvert, BM_GETCHECK, 0, 0) != 0;
    if (id == ID_TAP) {                          /* Tap applies to all of the function's controls */
        int j, on = SendMessageA(s_cTap, BM_GETCHECK, 0, 0) != 0;
        for (j = 0; j < p->n[s_selFunc]; j++) p->src[s_selFunc][j].tap = on;
    }
    if (id == ID_BELOW && s->hasThresh) s->wantGT = SendMessageA(s_cBelow, BM_GETCHECK, 0, 0) == 0;
    if (id == ID_THRESH && s->hasThresh) {
        char t[16];
        int v;
        GetWindowTextA(s_cThresh, t, sizeof t);
        v = to_int(t, s->threshPct);
        if (v < 1) v = 1;
        if (v > 99) v = 99;
        s->threshPct = v;
    }
    s_dirty = 1;
    InvalidateRect(s_hwnd, NULL, FALSE);
}

static int save_settings(void)
{
    char t[16];
    int v;
    GetWindowTextA(s_cDead, t, sizeof t);
    v = to_int(t, 3); if (v < 0) v = 0; if (v > 50) v = 50;
    wsprintfA(t, "%d", v); WritePrivateProfileStringA("input", "Deadzone", t, s_ini);
    GetWindowTextA(s_cResp, t, sizeof t);
    v = to_int(t, 100); if (v < 25) v = 25; if (v > 1000) v = 1000;
    wsprintfA(t, "%d", v); WritePrivateProfileStringA("input", "StickResponse", t, s_ini);
    WritePrivateProfileStringA("input", "Fallback", SendMessageA(s_cFall, CB_GETCURSEL, 0, 0) == 1 ? "keyboard" : "auto", s_ini);
    return 1;
}

static void cmd_save(void)
{
    if (save_all()) save_settings();
    fill_profiles();
    InvalidateRect(s_hwnd, NULL, FALSE);
}

static int confirm_close(void)
{
    int r;
    if (!s_dirty) return 1;
    r = MessageBoxA(s_hwnd, "Save your changes to usnf97fix.ini?", "usnf97 controls", MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) { if (!save_all()) { InvalidateRect(s_hwnd, NULL, FALSE); return 0; } save_settings(); }
    return 1;
}

static void on_click(int x, int y)
{
    int i;
    for (i = 0; i < s_nhit; i++) {
        Hit *h = &s_hit[i];
        if (x < h->r.left || x >= h->r.right || y < h->r.top || y >= h->r.bottom) continue;
        if (h->kind == 4) {
            Profile *p = cur();
            if (h->idx == s_selFunc && p && p->n[h->idx] > 1 && !s_listen) {
                char t[160], u[96];
                s_selSrc = (s_selSrc + 1) % p->n[h->idx];         /* click again: next control of this function */
                sync_options();
                src_text(&p->src[h->idx][s_selSrc], u, sizeof u);
                wsprintfA(t, "Editing %s (click the row again for the next one). Invert, threshold and Clear apply to it.", u);
                set_status(t);
            } else select_func(h->idx);
            return;
        }
        if (!cur()) { set_status("Create a profile first (New)."); return; }
        if (h->kind == 1) assign_axis(s_selDev, h->idx, 1);
        else if (h->kind == 2) assign_button(s_selDev, h->idx);
        else if (h->kind == 3) {
            int cx = (h->r.left + h->r.right) / 2, cy = (h->r.top + h->r.bottom) / 2, dx = x - cx, dy = y - cy, dir;
            if (dx * dx + dy * dy < 100) dir = -1;
            else if ((dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx)) dir = dy < 0 ? 0 : 2;
            else dir = dx > 0 ? 1 : 3;
            if (s_selFunc != F_HAT && dir < 0) { set_status("Click the edge of the hat for a direction."); return; }
            assign_pov(s_selDev, h->idx, dir);
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* window                                                             */
/* ------------------------------------------------------------------ */
static HWND child(const char *cls, const char *txt, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(ieq(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                             x, y, w, h, s_hwnd, (HMENU)(UINT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)s_font, 1);
    return c;
}

static void create_controls(void)
{
    char t[16];
    int by = WIN_H - 64;
    s_cProf = child("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 90, 8, 380, 300, ID_PROF);
    s_cName = child("EDIT", "", ES_AUTOHSCROLL, 480, 9, 180, 24, ID_NAME);
    child("BUTTON", "Rename", 0, 666, 8, 70, 26, ID_RENAME);
    child("BUTTON", "New", 0, 746, 8, 60, 26, ID_NEW);
    child("BUTTON", "Delete", 0, 812, 8, 64, 26, ID_DELETE);
    child("BUTTON", "Earlier", 0, 886, 8, 70, 26, ID_UP);
    child("BUTTON", "Later", 0, 960, 8, 64, 26, ID_DOWN);
    s_cDev = child("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 90, 40, 570, 300, ID_DEV);
    child("BUTTON", "Rescan", 0, 666, 40, 70, 26, ID_RESCAN);

    s_cListen = child("BUTTON", "Listen", 0, 12, by, 100, 28, ID_LISTEN);
    s_cAdd = child("BUTTON", "Add another", 0, 118, by, 100, 28, ID_ADD);
    child("BUTTON", "Clear", 0, 224, by, 70, 28, ID_CLEAR);
    s_cInvert = child("BUTTON", "Invert", BS_AUTOCHECKBOX, 310, by + 4, 70, 22, ID_INVERT);
    s_cTap = child("BUTTON", "Tap", BS_AUTOCHECKBOX, 384, by + 4, 54, 22, ID_TAP);
    child("STATIC", "Axis threshold %", 0, 446, by + 6, 104, 20, 0);
    s_cThresh = child("EDIT", "50", ES_NUMBER, 552, by + 2, 40, 24, ID_THRESH);
    s_cBelow = child("BUTTON", "when below", BS_AUTOCHECKBOX, 598, by + 4, 96, 22, ID_BELOW);

    child("STATIC", "Dead zone %", 0, 712, by + 6, 76, 20, 0);
    GetPrivateProfileStringA("input", "Deadzone", "3", t, sizeof t, s_ini);
    s_cDead = child("EDIT", t, ES_NUMBER, 790, by + 2, 36, 24, ID_DEADZONE);
    child("STATIC", "Stick response %", 0, 834, by + 6, 104, 20, 0);
    GetPrivateProfileStringA("input", "StickResponse", "100", t, sizeof t, s_ini);
    s_cResp = child("EDIT", t, ES_NUMBER, 940, by + 2, 44, 24, ID_RESPONSE);
    child("STATIC", "If none fits", 0, 712, by + 36, 76, 20, 0);
    s_cFall = child("COMBOBOX", "", CBS_DROPDOWNLIST, 790, by + 32, 194, 200, ID_FALLBACK);
    SendMessageA(s_cFall, CB_ADDSTRING, 0, (LPARAM)"Xbox pad if present, else keys");
    SendMessageA(s_cFall, CB_ADDSTRING, 0, (LPARAM)"Keyboard only");
    GetPrivateProfileStringA("input", "Fallback", "auto", t, sizeof t, s_ini);
    SendMessageA(s_cFall, CB_SETCURSEL, ieq(t, "keyboard") ? 1 : 0, 0);

    child("BUTTON", "Save", 0, WIN_W - 190, by, 84, 28, ID_SAVE);
    child("BUTTON", "Close", 0, WIN_W - 98, by, 84, 28, ID_CLOSE);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        s_hwnd = h;
        create_controls();
        fill_profiles();
        add_pad_slots();
        fill_devices();
        {
            char keep[256];
            scpy(keep, s_status, sizeof keep);
            select_func(F_ROLL);
            scpy(s_status, keep, sizeof s_status);
        }
        start_scan();
        SetTimer(h, 1, 25, NULL);
        return 0;
    case WM_SCANDONE: {
        char keep[128];
        int i;
        keep[0] = 0;
        if (s_selDev >= 0 && s_selDev < s_ndev) scpy(keep, s_dev[s_selDev].info.name, sizeof keep);
        apply_scan();
        s_selDev = -1;
        for (i = 0; i < s_ndev && keep[0]; i++) if (ieq(s_dev[i].info.name, keep)) { s_selDev = i; break; }
        fill_devices();
        fill_profiles();
        if (ieq(s_status, "Scanning for controllers...")) set_status("Select a function below, then press Listen and use the control, or click the control in the live view.");
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_TIMER: {
        DWORD now = GetTickCount();
        int i, padsBefore = 0, padsAfter = 0;
        for (i = 0; i < s_ndev; i++) padsBefore += s_dev[i].info.isPad && s_dev[i].info.connected;
        if (!s_scanning) for (i = 0; i < s_ndev; i++) poll_dev(&s_dev[i], now);
        for (i = 0; i < s_ndev; i++) padsAfter += s_dev[i].info.isPad && s_dev[i].info.connected;
        if (padsBefore != padsAfter) { fill_devices(); fill_profiles(); }
        listen_tick(now);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC: {
        static HBRUSH bg;
        if (!bg) bg = CreateSolidBrush(C_BG);
        SetTextColor((HDC)w, C_TEXT);
        SetBkColor((HDC)w, C_BG);
        return (LRESULT)bg;
    }
    case WM_PAINT:
        paint(h);
        return 0;
    case WM_LBUTTONDOWN:
        on_click(GET_X(l), GET_Y(l));
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (s_updatingUi) return 0;
        switch (id) {
        case ID_PROF:
            if (code == CBN_SELCHANGE) { s_cur = (int)SendMessageA(s_cProf, CB_GETCURSEL, 0, 0); SetWindowTextA(s_cName, cur() ? cur()->name : ""); select_func(s_selFunc); }
            break;
        case ID_DEV:
            if (code == CBN_SELCHANGE) { s_selDev = dev_from_combo((int)SendMessageA(s_cDev, CB_GETCURSEL, 0, 0)); InvalidateRect(h, NULL, FALSE); }
            break;
        case ID_NEW: cmd_new(); break;
        case ID_RENAME: cmd_rename(); break;
        case ID_DELETE: cmd_delete(); break;
        case ID_UP: cmd_move(-1); break;
        case ID_DOWN: cmd_move(1); break;
        case ID_RESCAN: if (!s_scanning) { set_status("Scanning for controllers..."); start_scan(); } break;
        case ID_LISTEN:
            if (!cur()) { set_status("Create a profile first (New)."); break; }
            if (s_listen) { s_listen = 0; SetWindowTextA(s_cListen, "Listen"); set_status("Stopped listening."); }
            else start_listen(1);
            break;
        case ID_ADD: if (cur()) start_listen(2); break;
        case ID_CLEAR: cmd_clear(); break;
        case ID_INVERT: case ID_TAP: case ID_BELOW: option_changed(id); break;
        case ID_THRESH: if (code == 0x0300 /* EN_CHANGE */) option_changed(id); break;
        case ID_DEADZONE: case ID_RESPONSE: if (code == 0x0300) s_dirty = 1; break;
        case ID_FALLBACK: if (code == CBN_SELCHANGE) s_dirty = 1; break;
        case ID_SAVE: cmd_save(); break;
        case ID_CLOSE: if (confirm_close()) DestroyWindow(h); break;
        }
        return 0;
    }
    case WM_CLOSE:
        if (confirm_close()) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* entry                                                              */
/* ------------------------------------------------------------------ */
static void find_ini(void)
{
    char *cmd = GetCommandLineA(), arg[260];
    int i, k = 0;
    /* skip the program name */
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; }
    else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"' && k < 259) arg[k++] = *cmd++; }
    else while (*cmd && k < 259) arg[k++] = *cmd++;
    arg[k] = 0; trim(arg);
    if (arg[0]) { scpy(s_ini, arg, sizeof s_ini); return; }
    GetModuleFileNameA(NULL, s_ini, sizeof s_ini);
    for (i = slen(s_ini); i > 0 && s_ini[i - 1] != '\\' && s_ini[i - 1] != '/'; i--) { }
    s_ini[i] = 0;
    scat(s_ini, "usnf97fix.ini", sizeof s_ini);
}

void __stdcall WinMainCRTStartup(void)
{
    WNDCLASSEXA wc;
    MSG msg;
    RECT r;
    HMODULE m;
    HWND hwnd;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    char title[300];

    find_ini();
    if (GetFileAttributesA(s_ini) == INVALID_FILE_ATTRIBUTES) {
        char t[400];
        wsprintfA(t, "usnf97fix.ini was not found:\n%s\n\nPut usnf97cfg.exe in the game folder (next to ddraw.dll and usnf97fix.ini), or pass the ini path on the command line.", s_ini);
        MessageBoxA(NULL, t, "usnf97 controls", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    for (s_nfunc = NROLE; g_inAliases[s_nfunc - NROLE].name && s_nfunc < MAXF; s_nfunc++) { }

    build_format();
    if ((m = LoadLibraryA("dinput8.dll")) != NULL) {
        HRESULT (WINAPI *create)(HINSTANCE, DWORD, const GUID *, void **, void *) =
            (HRESULT (WINAPI *)(HINSTANCE, DWORD, const GUID *, void **, void *))GetProcAddress(m, "DirectInput8Create");
        if (!create || create(GetModuleHandleA(NULL), 0x0800, &IID_IDirectInput8A_, &s_di, NULL) != DI_OK) s_di = NULL;
    }
    if ((m = LoadLibraryA("xinput1_4.dll")) || (m = LoadLibraryA("xinput1_3.dll")) || (m = LoadLibraryA("xinput9_1_0.dll")))
        s_XInputGetState = (DWORD (WINAPI *)(DWORD, XINPUT_STATE *))GetProcAddress(m, "XInputGetState");
    load_all();
    if (!s_status[0]) scpy(s_status, "Scanning for controllers...", sizeof s_status);

    s_font  = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 5, 0, "Segoe UI");
    s_fontB = CreateFontA(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 5, 0, "Segoe UI");
    s_fontS = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 5, 0, "Segoe UI");

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "usnf97cfg";
    RegisterClassExA(&wc);
    r.left = 0; r.top = 0; r.right = WIN_W; r.bottom = WIN_H;
    AdjustWindowRectEx(&r, style, FALSE, 0);
    wsprintfA(title, "USNF '97 controls  -  %s", s_ini);
    hwnd = CreateWindowExA(0, "usnf97cfg", title, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                           NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
