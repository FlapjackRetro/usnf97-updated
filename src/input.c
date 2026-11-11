/* usnf97fix input: DirectInput 8 + XInput behind the game's WinMM joystick calls.
 *
 * USNF '97 only knows the WinMM joystick API (one stick X/Y, throttle Z, rudder R,
 * one hat, 4 buttons, read at most every 50 ms). We redirect its four joy* imports
 * to a virtual joystick fed from any number of DirectInput 8 game controllers and
 * XInput pads, mapped per role in usnf97fix.ini. Roles that are not mapped are not
 * advertised, so the game keeps them on the keyboard. Any controller input can also
 * press game keys ([keys] section): extra buttons, hat directions, lever positions. */
#include "shim.h"
#include "input_core.h"

/* ------------------------------------------------------------------ */
/* DirectInput 8 / XInput declarations (no SDK headers)               */
/* ------------------------------------------------------------------ */
#define DI_OK                 0
#define DIERR_INPUTLOST       ((HRESULT)0x8007001EL)
#define DIERR_NOTACQUIRED     ((HRESULT)0x8007000CL)
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
STATIC_ASSERT(sizeof(DIDEVICEINSTANCEA) == 580, didi);
STATIC_ASSERT(sizeof(DIDEVICEOBJECTINSTANCEA) == 316, didoi);
STATIC_ASSERT(sizeof(DIDEVCAPS) == 44, didc);

typedef struct { LONG ax[8]; DWORD pov[4]; BYTE btn[128]; } DIRAW;     /* our device data format */
STATIC_ASSERT(sizeof(DIRAW) == 176, diraw);

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

/* WinMM joystick structures */
#define JOYERR_NOERROR   0
#define JOYERR_PARMS     165
#define JOYERR_UNPLUGGED 167
#define JOYCAPS_HASZ     0x0001
#define JOYCAPS_HASR     0x0002
#define JOYCAPS_HASPOV   0x0010
#define JOYCAPS_POV4DIR  0x0020
#define JOYCAPS_POVCTS   0x0040
#define JOY_RETURNPOVCTS 0x0200
typedef struct {
    WORD wMid, wPid; char szPname[32];
    UINT wXmin, wXmax, wYmin, wYmax, wZmin, wZmax, wNumButtons, wPeriodMin, wPeriodMax;
    UINT wRmin, wRmax, wUmin, wUmax, wVmin, wVmax, wCaps, wMaxAxes, wNumAxes, wMaxButtons;
    char szRegKey[32]; char szOEMVxD[260];
} JOYCAPSA;
typedef struct { UINT wXpos, wYpos, wZpos, wButtons; } JOYINFO;
typedef struct {
    DWORD dwSize, dwFlags, dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos, dwButtons, dwButtonNumber, dwPOV, dwReserved1, dwReserved2;
} JOYINFOEX;
STATIC_ASSERT(sizeof(JOYCAPSA) == 0x194, joycaps);
STATIC_ASSERT(sizeof(JOYINFOEX) == 0x34, joyinfoex);

/* ------------------------------------------------------------------ */
/* state                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    InDevInfo info;
    InDevState st;
    void *di;             /* IDirectInputDevice8A, NULL for XInput pads */
    GUID  guid;
    int   coopSet;
    DWORD padRetry;
} Dev;

enum { R_ROLL, R_PITCH, R_THROTTLE, R_RUDDER, R_HAT, R_B1, R_B2, R_B3, R_B4, R_COUNT };
static const char *const k_roleKey[R_COUNT] = { "Roll", "Pitch", "Throttle", "Rudder", "Hat", "Button1", "Button2", "Button3", "Button4" };
#define MAX_SRC 4

typedef struct { InSrc src[MAX_SRC]; int n; int mapped; } Role;
typedef struct { char name[48]; KeySpec key; InSrc src[MAX_SRC]; int n; int held; DWORD nextRepeat; unsigned char prev[MAX_SRC]; unsigned char primed; } KeyBind;
#define MAX_KEYBINDS 96

typedef struct { int x, y, z, r; unsigned int pov; DWORD buttons; } Virtual;

static CRITICAL_SECTION s_lock;
static volatile int s_inited;
static int      s_enabled;
static void    *s_di;
static DWORD (WINAPI *s_XInputGetState)(DWORD, XINPUT_STATE *);
static Dev      s_dev[IN_MAX_DEVS];
static int      s_ndev;
static Role     s_role[R_COUNT];
static KeyBind  s_keys[MAX_KEYBINDS];
static int      s_nkeys;
static Virtual  s_virt;
static int      s_hasStick, s_hasThrottle, s_hasRudder, s_hasHat, s_present;
static int      s_deadzone, s_logInput, s_keysUnfocused;
static char     s_iniPath[280];
static DIOBJECTDATAFORMAT s_objs[8 + 4 + 128];
static DIDATAFORMAT       s_fmt;


/* ------------------------------------------------------------------ */
/* devices                                                            */
/* ------------------------------------------------------------------ */
static void build_format(void)
{
    static const GUID *const axisGuid[8] = { &GUID_XAxis_, &GUID_YAxis_, &GUID_ZAxis_, &GUID_RxAxis_,
                                             &GUID_RyAxis_, &GUID_RzAxis_, &GUID_Slider_, &GUID_Slider_ };
    int i, n = 0;
    for (i = 0; i < 8; i++, n++) {
        s_objs[n].pguid = axisGuid[i]; s_objs[n].dwOfs = (DWORD)(i * 4);
        s_objs[n].dwType = DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; s_objs[n].dwFlags = DIDOI_ASPECTPOSITION;
    }
    for (i = 0; i < 4; i++, n++) {
        s_objs[n].pguid = &GUID_POV_; s_objs[n].dwOfs = (DWORD)(32 + i * 4);
        s_objs[n].dwType = DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; s_objs[n].dwFlags = 0;
    }
    for (i = 0; i < 128; i++, n++) {
        s_objs[n].pguid = NULL; s_objs[n].dwOfs = (DWORD)(48 + i);
        s_objs[n].dwType = DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; s_objs[n].dwFlags = 0;
    }
    s_fmt.dwSize = sizeof s_fmt; s_fmt.dwObjSize = sizeof(DIOBJECTDATAFORMAT); s_fmt.dwFlags = DIDF_ABSAXIS;
    s_fmt.dwDataSize = sizeof(DIRAW); s_fmt.dwNumObjs = (DWORD)n; s_fmt.rgodf = s_objs;
}

typedef struct { Dev *d; } ObjCtx;
static BOOL WINAPI obj_cb(const DIDEVICEOBJECTINSTANCEA *o, void *ctx)
{
    Dev *d = ((ObjCtx *)ctx)->d;
    if (o->dwOfs < 32 && (o->dwType & DIDFT_AXIS)) {
        int a = (int)(o->dwOfs / 4);
        d->info.axisMask |= 1 << a;
        logf("    %-3s = %s", in_axis_name(a, 0), o->tszName);
    } else if (o->dwOfs >= 32 && o->dwOfs < 48 && (o->dwType & DIDFT_POV)) {
        logf("    POV%lu = %s", (o->dwOfs - 32) / 4 + 1, o->tszName);
    }
    return DIENUM_CONTINUE;
}

static int path_is_xinput(void *dev)
{
    DIPROPGUIDANDPATH p;
    int i;
    memset(&p, 0, sizeof p);
    p.h.dwSize = sizeof p; p.h.dwHeaderSize = sizeof p.h; p.h.dwHow = DIPH_DEVICE;
    if (DEV_GetProperty(dev, DIPROP_GUIDANDPATH, &p.h) != DI_OK) return 0;
    for (i = 0; p.wszPath[i] && p.wszPath[i + 1] && p.wszPath[i + 2]; i++)
        if ((p.wszPath[i] == 'I' || p.wszPath[i] == 'i') && (p.wszPath[i + 1] == 'G' || p.wszPath[i + 1] == 'g') && p.wszPath[i + 2] == '_')
            return 1;
    return 0;
}

static void try_coop(Dev *d)
{
    HWND h = present_hwnd();
    if (d->coopSet || !h || !d->di) return;
    DEV_Unacquire(d->di);
    if (DEV_SetCoop(d->di, h, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND) == DI_OK) d->coopSet = 1;
    DEV_Acquire(d->di);
}

static int open_di_device(const DIDEVICEINSTANCEA *inst, Dev *d)
{
    void *dev = NULL;
    DIPROPRANGE r;
    DIDEVCAPS caps;
    ObjCtx ctx;
    if (DI_CreateDevice(s_di, &inst->guidInstance, &dev) != DI_OK || !dev) return 0;
    if (s_XInputGetState && path_is_xinput(dev)) {           /* handled through XInput instead */
        logf("  %s: XInput device, using the XInput path", inst->tszProductName);
        DI_Release(dev);
        return 0;
    }
    if (DEV_SetDataFormat(dev, &s_fmt) != DI_OK) { DI_Release(dev); return 0; }
    memset(d, 0, sizeof *d);
    d->di = dev;
    d->guid = inst->guidInstance;
    str_cpy(d->info.name, inst->tszProductName[0] ? inst->tszProductName : inst->tszInstanceName, sizeof d->info.name);
    d->info.connected = 1;
    memset(&r, 0, sizeof r);
    r.h.dwSize = sizeof r; r.h.dwHeaderSize = sizeof r.h; r.h.dwHow = DIPH_DEVICE; r.lMin = 0; r.lMax = 65535;
    DEV_SetProperty(dev, DIPROP_RANGE, &r.h);
    memset(&caps, 0, sizeof caps);
    caps.dwSize = sizeof caps;
    DEV_GetCaps(dev, &caps);
    d->info.nButtons = (int)(caps.dwButtons > 128 ? 128 : caps.dwButtons);
    d->info.nPovs = (int)(caps.dwPOVs > 4 ? 4 : caps.dwPOVs);
    logf("  device \"%s\": %lu axes, %lu buttons (B1..B%lu), %lu hat(s)", d->info.name, caps.dwAxes, caps.dwButtons, caps.dwButtons, caps.dwPOVs);
    ctx.d = d;
    DEV_EnumObjects(dev, obj_cb, &ctx);
    if (caps.dwAxes > 8) logf("    note: only the first 8 axes (X Y Z RX RY RZ S1 S2) are reachable");
    try_coop(d);
    DEV_Acquire(dev);
    return 1;
}

typedef struct { DIDEVICEINSTANCEA list[IN_MAX_DEVS]; int n; } EnumList;
static BOOL WINAPI enum_cb(const DIDEVICEINSTANCEA *inst, void *ctx)
{
    EnumList *l = ctx;
    if (l->n < IN_MAX_DEVS) l->list[l->n++] = *inst;
    return DIENUM_CONTINUE;
}

static void resolve_all(void);

/* (Re)build the device list from a fresh enumeration; returns 1 if it changed.
 * Only the input thread (or init, before it starts) touches the device list. */
static int enumerate(int verbose)
{
    static EnumList l;
    int i, j, changed = 0;
    l.n = 0;
    if (s_di) DI_EnumDevices(s_di, DI8DEVCLASS_GAMECTRL, enum_cb, &l);
    else return 0;
    /* drop DirectInput devices that went away */
    for (i = 0; i < s_ndev; ) {
        int found = 0;
        if (!s_dev[i].di) { i++; continue; }
        for (j = 0; j < l.n; j++) if (!memcmp(&l.list[j].guidInstance, &s_dev[i].guid, sizeof(GUID))) found = 1;
        if (found) { i++; continue; }
        logf("input: \"%s\" disconnected", s_dev[i].info.name);
        DEV_Unacquire(s_dev[i].di); DI_Release(s_dev[i].di);
        memmove(&s_dev[i], &s_dev[i + 1], (unsigned)(s_ndev - i - 1) * sizeof(Dev));
        s_ndev--; changed = 1;
    }
    /* add new ones (keep pads at the end) */
    for (j = 0; j < l.n; j++) {
        int known = 0;
        for (i = 0; i < s_ndev; i++) if (s_dev[i].di && !memcmp(&l.list[j].guidInstance, &s_dev[i].guid, sizeof(GUID))) known = 1;
        if (known || s_ndev >= IN_MAX_DEVS - 4) continue;
        {
            Dev d;
            int k;
            if (!verbose) logf("input: new device");
            if (!open_di_device(&l.list[j], &d)) continue;
            for (k = 0; k < s_ndev && s_dev[k].di; k++) { }
            memmove(&s_dev[k + 1], &s_dev[k], (unsigned)(s_ndev - k) * sizeof(Dev));
            s_dev[k] = d; s_ndev++; changed = 1;
        }
    }
    if (changed) resolve_all();
    return changed;
}

static void add_pads(void)
{
    int i;
    if (!s_XInputGetState) return;
    for (i = 0; i < 4 && s_ndev < IN_MAX_DEVS; i++) {
        Dev *d = &s_dev[s_ndev++];
        memset(d, 0, sizeof *d);
        wsprintfA(d->info.name, "XInput pad %d", i + 1);
        d->info.isPad = 1; d->info.padIndex = i + 1;
        d->info.axisMask = 0x3f; d->info.nButtons = 10; d->info.nPovs = 1;
    }
}

static int scale_trigger(BYTE t) { return t * 65535 / 255; }
static int scale_thumb(short v, int flip) { int x = (int)v + 32768; return flip ? 65535 - x : x; }

static void poll_pad(Dev *d, DWORD now)
{
    XINPUT_STATE xs;
    static const WORD bits[10] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080 };
    int i, u, r, dn, l;
    if (!d->info.connected && now < d->padRetry) return;     /* polling absent pads is slow */
    if (s_XInputGetState((DWORD)(d->info.padIndex - 1), &xs) != 0) {
        if (d->info.connected) logf("input: %s disconnected", d->info.name);
        d->info.connected = 0; d->padRetry = now + 1000;
        memset(&d->st, 0, sizeof d->st);
        d->st.ax[0] = d->st.ax[1] = d->st.ax[3] = d->st.ax[4] = IN_AXIS_MID;
        for (i = 0; i < IN_POVS; i++) d->st.pov[i] = IN_POV_CENTER;
        return;
    }
    if (!d->info.connected) logf("input: %s connected", d->info.name);
    d->info.connected = 1;
    d->st.ax[0] = scale_thumb(xs.Gamepad.sThumbLX, 0);
    d->st.ax[1] = scale_thumb(xs.Gamepad.sThumbLY, 1);      /* DirectInput convention: down = larger */
    d->st.ax[2] = scale_trigger(xs.Gamepad.bLeftTrigger);
    d->st.ax[3] = scale_thumb(xs.Gamepad.sThumbRX, 0);
    d->st.ax[4] = scale_thumb(xs.Gamepad.sThumbRY, 1);
    d->st.ax[5] = scale_trigger(xs.Gamepad.bRightTrigger);
    for (i = 0; i < 10; i++) d->st.btn[i] = (xs.Gamepad.wButtons & bits[i]) ? 0x80 : 0;
    u = (xs.Gamepad.wButtons & 1) != 0; dn = (xs.Gamepad.wButtons & 2) != 0;
    l = (xs.Gamepad.wButtons & 4) != 0; r = (xs.Gamepad.wButtons & 8) != 0;
    if (u && r) d->st.pov[0] = 4500; else if (r && dn) d->st.pov[0] = 13500;
    else if (dn && l) d->st.pov[0] = 22500; else if (l && u) d->st.pov[0] = 31500;
    else if (u) d->st.pov[0] = 0; else if (r) d->st.pov[0] = 9000;
    else if (dn) d->st.pov[0] = 18000; else if (l) d->st.pov[0] = 27000;
    else d->st.pov[0] = IN_POV_CENTER;
}

static void poll_di(Dev *d)
{
    DIRAW raw;
    HRESULT hr;
    int i;
    try_coop(d);
    DEV_Poll(d->di);
    hr = DEV_GetState(d->di, sizeof raw, &raw);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        DEV_Acquire(d->di);
        hr = DEV_GetState(d->di, sizeof raw, &raw);
    }
    if (hr != DI_OK) {                       /* unplugged or busy: release buttons/hats so nothing sticks */
        memset(d->st.btn, 0, sizeof d->st.btn);
        for (i = 0; i < IN_POVS; i++) d->st.pov[i] = IN_POV_CENTER;
        return;
    }
    for (i = 0; i < 8; i++) d->st.ax[i] = raw.ax[i] < 0 ? 0 : raw.ax[i] > 65535 ? 65535 : (int)raw.ax[i];
    for (i = 0; i < 4; i++) d->st.pov[i] = (LOWORD(raw.pov[i]) == 0xFFFF) ? IN_POV_CENTER : raw.pov[i];
    for (i = 0; i < 128; i++) d->st.btn[i] = raw.btn[i] & 0x80;
}

/* ------------------------------------------------------------------ */
/* mapping                                                            */
/* ------------------------------------------------------------------ */
static void ini_str(const char *sect, const char *key, const char *def, char *out, DWORD cap)
{
    GetPrivateProfileStringA(sect, key, def, out, cap, s_iniPath);
}

/* split "a, b, c" into sources */
static int parse_sources(const char *text, InSrc *out, int max, const char *what)
{
    char part[96];
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        int k = 0;
        while (*p == ' ' || *p == ',') p++;
        while (*p && *p != ',' && k < (int)sizeof part - 1) part[k++] = *p++;
        while (k > 0 && part[k - 1] == ' ') k--;
        part[k] = 0;
        if (!k) continue;
        if (insrc_parse(part, &out[n])) n++;
        else logf("input: can't understand \"%s\" for %s (expected device:control)", part, what);
    }
    return n;
}

static int find_dev(int (*pred)(const Dev *))
{
    int i;
    for (i = 0; i < s_ndev; i++) if (pred(&s_dev[i])) return i;
    return -1;
}
static int is_special(const Dev *d)
{
    return in_contains(d->info.name, "throttle") || in_contains(d->info.name, "quadrant") ||
           in_contains(d->info.name, "pedal") || in_contains(d->info.name, "rudder");
}
static int pred_stick(const Dev *d) { return !d->info.isPad && (d->info.axisMask & 3) == 3 && !is_special(d); }
static int pred_pad(const Dev *d) { return d->info.isPad && d->info.connected; }
static int pred_throttle(const Dev *d) { return !d->info.isPad && (in_contains(d->info.name, "throttle") || in_contains(d->info.name, "quadrant")); }
static int pred_pedals(const Dev *d) { return !d->info.isPad && (in_contains(d->info.name, "pedal") || in_contains(d->info.name, "rudder")); }

static void auto_src(InSrc *s, int dev, const char *ctl)
{
    memset(s, 0, sizeof *s);
    /* address by name so it survives re-enumeration */
    str_cpy(s->devPat, s_dev[dev].info.name, sizeof s->devPat);
    if (s_dev[dev].info.isPad) wsprintfA(s->devPat, "pad%d", s_dev[dev].info.padIndex);
    str_cpy(s->ctl, ctl, sizeof s->ctl);
    s->dev = -1;
}

static int first_axis(int dev, const int *order, int n)
{
    int i;
    for (i = 0; i < n; i++) if (s_dev[dev].info.axisMask & (1 << order[i])) return order[i];
    return -1;
}

/* Fill roles left as "auto" with a sensible guess from the attached devices. */
static void auto_roles(int *isAuto)
{
    int stick = find_dev(pred_stick), pad = find_dev(pred_pad), thr = find_dev(pred_throttle), ped = find_dev(pred_pedals);
    int src = stick >= 0 ? stick : pad;
    static const int thrOrder[] = { 2, 6, 7, 0, 5 }, pedOrder[] = { 5, 2, 0 };
    int a;
    if (src >= 0) {
        int isPad = s_dev[src].info.isPad;
        if (isAuto[R_ROLL])  { auto_src(&s_role[R_ROLL].src[0], src, isPad ? "LX" : "X"); s_role[R_ROLL].n = 1; }
        if (isAuto[R_PITCH]) { auto_src(&s_role[R_PITCH].src[0], src, isPad ? "LY" : "Y"); s_role[R_PITCH].n = 1; }
        if (isAuto[R_HAT] && s_dev[src].info.nPovs) { auto_src(&s_role[R_HAT].src[0], src, isPad ? "DPAD" : "POV1"); s_role[R_HAT].n = 1; }
        {
            static const char *const padB[4] = { "A", "B", "X", "Y" };
            int b;
            for (b = 0; b < 4; b++) if (isAuto[R_B1 + b] && (isPad || s_dev[src].info.nButtons > b)) {
                char nm[8];
                if (isPad) str_cpy(nm, padB[b], sizeof nm); else wsprintfA(nm, "B%d", b + 1);
                auto_src(&s_role[R_B1 + b].src[0], src, nm); s_role[R_B1 + b].n = 1;
            }
        }
        if (isAuto[R_RUDDER] && ped < 0) {
            if (isPad) { auto_src(&s_role[R_RUDDER].src[0], src, "RT-LT"); s_role[R_RUDDER].n = 1; }
            else if (s_dev[src].info.axisMask & (1 << 5)) { auto_src(&s_role[R_RUDDER].src[0], src, "RZ"); s_role[R_RUDDER].n = 1; }
        }
        if (isAuto[R_THROTTLE] && thr < 0 && !isPad) {
            static const int o[] = { 2, 6 };
            a = first_axis(src, o, 2);
            if (a >= 0) { auto_src(&s_role[R_THROTTLE].src[0], src, in_axis_name(a, 0)); s_role[R_THROTTLE].n = 1; }
        }
    }
    if (isAuto[R_THROTTLE] && thr >= 0 && (a = first_axis(thr, thrOrder, 5)) >= 0) {
        auto_src(&s_role[R_THROTTLE].src[0], thr, in_axis_name(a, 0)); s_role[R_THROTTLE].n = 1;
    }
    if (isAuto[R_RUDDER] && ped >= 0 && (a = first_axis(ped, pedOrder, 3)) >= 0) {
        auto_src(&s_role[R_RUDDER].src[0], ped, in_axis_name(a, 0)); s_role[R_RUDDER].n = 1;
    }
}

static void resolve_all(void)
{
    InDevInfo info[IN_MAX_DEVS];
    int i, j;
    for (i = 0; i < s_ndev; i++) info[i] = s_dev[i].info;
    for (i = 0; i < R_COUNT; i++)
        for (j = 0; j < s_role[i].n; j++) insrc_resolve(&s_role[i].src[j], info, s_ndev);
    for (i = 0; i < s_nkeys; i++)
        for (j = 0; j < s_keys[i].n; j++) insrc_resolve(&s_keys[i].src[j], info, s_ndev);
}

static void describe(const InSrc *s, char *out, int cap)
{
    wsprintfA(out, "%s:%s%s%s%s", s->devPat, s->ctl, s->ctl2[0] ? "-" : "", s->ctl2, s->invert ? "~" : "");
    (void)cap;
}

static void load_mapping(void)
{
    char buf[512];
    int isAuto[R_COUNT], i, j;
    static char section[8192];

    for (i = 0; i < R_COUNT; i++) {
        ini_str("input", k_roleKey[i], "auto", buf, sizeof buf);
        s_role[i].n = 0;
        isAuto[i] = str_ieq(buf, "auto") || !buf[0];
        if (isAuto[i] || str_ieq(buf, "none") || str_ieq(buf, "keyboard")) continue;
        s_role[i].n = parse_sources(buf, s_role[i].src, MAX_SRC, k_roleKey[i]);
    }
    auto_roles(isAuto);

    /* [keys]: "<key or command>=<source>[, <source>...]" */
    s_nkeys = 0;
    if (GetPrivateProfileSectionA("keys", section, sizeof section, s_iniPath)) {
        char *p = section;
        while (*p && s_nkeys < MAX_KEYBINDS) {
            char *eq = p, *line = p;
            unsigned int len = str_len(p);
            p += len + 1;
            if (line[0] == ';' || line[0] == '#') continue;
            while (*eq && *eq != '=') eq++;
            if (!*eq) continue;
            *eq = 0;
            {
                KeyBind *kb = &s_keys[s_nkeys];
                char *name = line;
                int n;
                while (*name == ' ') name++;
                for (n = (int)str_len(name); n > 0 && name[n - 1] == ' '; n--) name[n - 1] = 0;
                memset(kb, 0, sizeof *kb);
                if (!keyspec_parse(name, &kb->key)) { logf("input: [keys] unknown key or command \"%s\"", name); continue; }
                str_cpy(kb->name, name, sizeof kb->name);
                kb->n = parse_sources(eq + 1, kb->src, MAX_SRC, name);
                if (kb->n) s_nkeys++;
            }
        }
    }
    /* Pads have no throttle axis: with an auto-mapped pad, RB/LB step the throttle. */
    if (isAuto[R_THROTTLE] && !s_role[R_THROTTLE].n && s_role[R_ROLL].n && in_contains(s_role[R_ROLL].src[0].devPat, "pad")) {
        static const char *const cmd[2] = { "ThrottleUp", "ThrottleDown" }, *const btn[2] = { "RB", "LB" };
        for (i = 0; i < 2 && s_nkeys < MAX_KEYBINDS; i++) {
            int used = 0, k;
            for (k = 0; k < s_nkeys; k++)
                for (j = 0; j < s_keys[k].n; j++)
                    if (in_contains(s_keys[k].src[j].devPat, "pad") && str_ieq(s_keys[k].src[j].ctl, btn[i])) used = 1;
            if (used) continue;
            memset(&s_keys[s_nkeys], 0, sizeof s_keys[s_nkeys]);
            keyspec_parse(cmd[i], &s_keys[s_nkeys].key);
            str_cpy(s_keys[s_nkeys].name, cmd[i], sizeof s_keys[s_nkeys].name);
            str_cpy(s_keys[s_nkeys].src[0].devPat, s_role[R_ROLL].src[0].devPat, sizeof s_keys[s_nkeys].src[0].devPat);
            str_cpy(s_keys[s_nkeys].src[0].ctl, btn[i], sizeof s_keys[s_nkeys].src[0].ctl);
            s_keys[s_nkeys].src[0].dev = -1;
            s_keys[s_nkeys].n = 1;
            s_nkeys++;
        }
    }
    resolve_all();

    s_hasStick = s_role[R_ROLL].n || s_role[R_PITCH].n;
    s_hasThrottle = s_role[R_THROTTLE].n > 0;
    s_hasRudder = s_role[R_RUDDER].n > 0;
    s_hasHat = s_role[R_HAT].n > 0;
    s_present = s_hasStick || s_hasThrottle || s_hasRudder || s_hasHat;
    for (i = R_B1; i <= R_B4; i++) if (s_role[i].n) s_present = 1;

    logf("input mapping (anything not listed stays on the keyboard):");
    for (i = 0; i < R_COUNT; i++) {
        char line[400], d[96];
        line[0] = 0;
        for (j = 0; j < s_role[i].n; j++) {
            describe(&s_role[i].src[j], d, sizeof d);
            if (j) str_cat(line, ", ", sizeof line);
            str_cat(line, d, sizeof line);
            if (s_role[i].src[j].kind == SRC_NONE) str_cat(line, " (not found)", sizeof line);
        }
        logf("  %-9s %s%s", k_roleKey[i], s_role[i].n ? line : "keyboard", (isAuto[i] && s_role[i].n) ? "  [auto]" : "");
    }
    for (i = 0; i < s_nkeys; i++) {
        char d[96];
        describe(&s_keys[i].src[0], d, sizeof d);
        logf("  key %-12s <- %s%s%s", s_keys[i].name, d, s_keys[i].n > 1 ? " (+more)" : "",
             s_keys[i].src[0].kind == SRC_NONE ? " (not found)" : "");
    }
    if (!s_present) logf("  no controller mapped: the game sees no joystick");
}

/* ------------------------------------------------------------------ */
/* evaluation, key injection, logging                                 */
/* ------------------------------------------------------------------ */
static const InDevState *state_of(const InSrc *s)
{
    if (s->dev < 0 || s->dev >= s_ndev) return NULL;
    return &s_dev[s->dev].st;
}

static int role_axis(int r, int def)
{
    int j;
    for (j = 0; j < s_role[r].n; j++) {
        int v = insrc_axis(&s_role[r].src[j], state_of(&s_role[r].src[j]));
        if (v >= 0) return v;
    }
    return def;
}
static int role_pressed(int r)
{
    int j;
    for (j = 0; j < s_role[r].n; j++) if (insrc_pressed(&s_role[r].src[j], state_of(&s_role[r].src[j]))) return 1;
    return 0;
}
static unsigned int role_pov(void)
{
    int j;
    for (j = 0; j < s_role[R_HAT].n; j++) {
        const InSrc *s = &s_role[R_HAT].src[j];
        const InDevState *st = state_of(s);
        if (st && s->kind == SRC_POV && st->pov[s->idx] != IN_POV_CENTER) return st->pov[s->idx];
    }
    return IN_POV_CENTER;
}

static void post_scan(HWND h, unsigned char scan, int down, int alt)
{
    UINT vk = MapVirtualKeyA(scan, 1 /* MAPVK_VSC_TO_VK */);
    LPARAM lp = 1 | ((LPARAM)scan << 16) | (alt ? (1 << 29) : 0) | (down ? 0 : (LPARAM)0xC0000000u);
    PostMessageA(h, alt ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP), vk, lp);
}

static void send_key(const KeySpec *k, int down)
{
    HWND h = present_hwnd();
    int alt = (k->mods & 4) != 0;
    if (!h) return;
    if (down) {
        if (k->mods & 2) post_scan(h, 0x1d, 1, alt);
        if (k->mods & 1) post_scan(h, 0x2a, 1, alt);
        if (alt) post_scan(h, 0x38, 1, 1);
        post_scan(h, k->scan, 1, alt);
    } else {
        post_scan(h, k->scan, 0, alt);
        if (alt) post_scan(h, 0x38, 0, 1);
        if (k->mods & 1) post_scan(h, 0x2a, 0, alt);
        if (k->mods & 2) post_scan(h, 0x1d, 0, alt);
    }
}

static void update_keys(DWORD now)
{
    int i, j, focused;
    HWND h = present_hwnd();
    focused = h && (s_keysUnfocused || GetForegroundWindow() == h);
    for (i = 0; i < s_nkeys; i++) {
        KeyBind *kb = &s_keys[i];
        int on = 0, tap = 0;
        for (j = 0; j < kb->n; j++) {
            int p = insrc_pressed(&kb->src[j], state_of(&kb->src[j])) ? 1 : 0;
            if (kb->src[j].tap) {
                /* lever positions etc.: tap on each activation; the first poll only primes */
                if (kb->primed && p && !kb->prev[j] && focused) tap = 1;
                kb->prev[j] = (unsigned char)p;
            } else if (p) on = 1;
        }
        kb->primed = 1;
        if (!focused) on = 0;
        if (tap && !kb->held) {
            send_key(&kb->key, 1); send_key(&kb->key, 0);
            if (s_logInput) logf("input: key %s tap", kb->name);
        }
        if (on && !kb->held) {
            kb->held = 1; send_key(&kb->key, 1);
            kb->nextRepeat = now + 350;
            if (s_logInput) logf("input: key %s down", kb->name);
        } else if (!on && kb->held) {
            kb->held = 0; send_key(&kb->key, 0);
        } else if (on && (kb->key.scan == 0x08 || kb->key.scan == 0x09) && !kb->key.mods && now >= kb->nextRepeat) {
            /* ThrottleDown/ThrottleUp (7/8) step 5% per press: repeat while held */
            send_key(&kb->key, 0); send_key(&kb->key, 1);
            kb->nextRepeat = now + 120;
        }
    }
}

static DWORD s_logT0;
#define T_MS ((GetTickCount() - s_logT0))
#define T_S  (T_MS / 1000)
#define T_C  ((T_MS % 1000) / 10)

static void log_changes(void)
{
    if (!s_logT0) s_logT0 = GetTickCount();
    static InDevState last[IN_MAX_DEVS];
    static char lastName[IN_MAX_DEVS][128];
    int i, k;
    for (i = 0; i < s_ndev; i++) {
        Dev *d = &s_dev[i];
        if (!str_eq(lastName[i], d->info.name)) {
            /* new device: dump its starting state so held toggles/sliders are visible */
            char line[512];
            int n = 0;
            last[i] = d->st; str_cpy(lastName[i], d->info.name, 128);
            n += wsprintfA(line + n, "input: %s initial:", d->info.name);
            for (k = 0; k < IN_AXES && n < 440; k++)
                if (d->info.axisMask & (1 << k)) n += wsprintfA(line + n, " %s=%d%%", in_axis_name(k, d->info.isPad), d->st.ax[k] * 100 / 65535);
            for (k = 0; k < IN_POVS && n < 440; k++)
                if (d->st.pov[k] != IN_POV_CENTER) n += wsprintfA(line + n, " POV%d=%lu", k + 1, d->st.pov[k] / 100);
            for (k = 0; k < IN_BUTTONS && n < 470; k++)
                if (d->st.btn[k]) n += wsprintfA(line + n, " B%d", k + 1);
            logf("%s", line);
            continue;
        }
        for (k = 0; k < IN_BUTTONS; k++) if ((last[i].btn[k] != 0) != (d->st.btn[k] != 0)) {
            if (d->info.isPad) {
                static const char *const pb[10] = { "A", "B", "X", "Y", "LB", "RB", "BACK", "START", "LS", "RS" };
                if (k < 10) logf("input: [%lu.%02lu] %s:%s %s", T_S, T_C, d->info.name, pb[k], d->st.btn[k] ? "down" : "up");
            } else logf("input: [%lu.%02lu] %s:B%d %s", T_S, T_C, d->info.name, k + 1, d->st.btn[k] ? "down" : "up");
        }
        for (k = 0; k < IN_POVS; k++) if (last[i].pov[k] != d->st.pov[k]) {
            if (d->st.pov[k] == IN_POV_CENTER) logf("input: [%lu.%02lu] %s:%s%d centred", T_S, T_C, d->info.name, d->info.isPad ? "DPAD" : "POV", k + 1);
            else logf("input: [%lu.%02lu] %s:%s%d %lu deg", T_S, T_C, d->info.name, d->info.isPad ? "DPAD" : "POV", k + 1, d->st.pov[k] / 100);
        }
        for (k = 0; k < IN_AXES; k++) {
            int dv = d->st.ax[k] - last[i].ax[k];
            if (!(d->info.axisMask & (1 << k))) continue;
            if (dv > 6553 || dv < -6553) {
                logf("input: [%lu.%02lu] %s:%s %d%%", T_S, T_C, d->info.name, in_axis_name(k, d->info.isPad), d->st.ax[k] * 100 / 65535);
                last[i].ax[k] = d->st.ax[k];
            }
        }
        for (k = 0; k < IN_BUTTONS; k++) last[i].btn[k] = d->st.btn[k];
        for (k = 0; k < IN_POVS; k++) last[i].pov[k] = d->st.pov[k];
    }
}

static void compute_virtual(void)     /* the only state game threads read */
{
    Virtual v;
    int b;
    v.x = in_deadzone(role_axis(R_ROLL, IN_AXIS_MID), s_deadzone);
    v.y = in_deadzone(role_axis(R_PITCH, IN_AXIS_MID), s_deadzone);
    v.z = role_axis(R_THROTTLE, IN_AXIS_MAX);          /* game: minimum Z = full power, so default idle */
    v.r = in_deadzone(role_axis(R_RUDDER, IN_AXIS_MID), s_deadzone);
    v.pov = role_pov();
    v.buttons = 0;
    for (b = 0; b < 4; b++) if (role_pressed(R_B1 + b)) v.buttons |= 1u << b;
    EnterCriticalSection(&s_lock);
    s_virt = v;
    LeaveCriticalSection(&s_lock);
}

static DWORD WINAPI input_thread(void *arg)
{
    DWORD lastEnum = GetTickCount();
    (void)arg;
    for (;;) {
        DWORD now = GetTickCount();
        int i;
        if (now - lastEnum > 2000) { enumerate(0); lastEnum = now; }
        for (i = 0; i < s_ndev; i++) {
            if (s_dev[i].di) poll_di(&s_dev[i]);
            else poll_pad(&s_dev[i], now);
        }
        compute_virtual();
        update_keys(now);
        if (s_logInput) log_changes();
        Sleep(4);
    }
}

/* ------------------------------------------------------------------ */
/* init                                                               */
/* ------------------------------------------------------------------ */
int input_enabled(void)
{
    return s_enabled;
}

void input_configure(void)
{
    str_cpy(s_iniPath, g_gameDir, sizeof s_iniPath);
    str_cat(s_iniPath, "usnf97fix.ini", sizeof s_iniPath);
    s_enabled = GetPrivateProfileIntA("input", "Enable", 1, s_iniPath) != 0;
    InitializeCriticalSection(&s_lock);
}

static void input_init_locked(void);
static void input_init(void)
{
    if (s_inited) return;                 /* set only after init completes */
    EnterCriticalSection(&s_lock);        /* other game threads wait here during the first init */
    if (!s_inited) { input_init_locked(); s_inited = 1; }
    LeaveCriticalSection(&s_lock);
}

static void input_init_locked(void)
{
    HMODULE m;
    int i;
    s_deadzone = (int)GetPrivateProfileIntA("input", "Deadzone", 3, s_iniPath);
    s_logInput = (int)GetPrivateProfileIntA("input", "LogInput", 0, s_iniPath);
    s_keysUnfocused = (int)GetPrivateProfileIntA("input", "KeysWhenUnfocused", 0, s_iniPath);
    build_format();

    m = LoadLibraryA("xinput1_4.dll");
    if (!m) m = LoadLibraryA("xinput1_3.dll");
    if (!m) m = LoadLibraryA("xinput9_1_0.dll");
    if (m) s_XInputGetState = (DWORD (WINAPI *)(DWORD, XINPUT_STATE *))GetProcAddress(m, "XInputGetState");

    m = LoadLibraryA("dinput8.dll");
    if (m) {
        HRESULT (WINAPI *create)(HINSTANCE, DWORD, const GUID *, void **, void *) =
            (HRESULT (WINAPI *)(HINSTANCE, DWORD, const GUID *, void **, void *))GetProcAddress(m, "DirectInput8Create");
        if (!create || create(GetModuleHandleA(NULL), 0x0800, &IID_IDirectInput8A_, &s_di, NULL) != DI_OK) s_di = NULL;
    }
    logf("input: DirectInput 8 %s, XInput %s", s_di ? "ok" : "UNAVAILABLE", s_XInputGetState ? "ok" : "unavailable");

    enumerate(1);
    add_pads();
    for (i = 0; i < s_ndev; i++) if (s_dev[i].info.isPad) poll_pad(&s_dev[i], GetTickCount());
    for (i = 0; i < s_ndev; i++) if (s_dev[i].info.isPad && s_dev[i].info.connected) logf("  device \"%s\" (XInput): LX LY LT RX RY RT, A B X Y LB RB BACK START LS RS, DPAD", s_dev[i].info.name);
    load_mapping();
    for (i = 0; i < s_ndev; i++) if (s_dev[i].di) poll_di(&s_dev[i]);
    compute_virtual();
    CreateThread(NULL, 0, input_thread, NULL, 0, NULL);
}

/* ------------------------------------------------------------------ */
/* the game's WinMM joystick calls                                    */
/* ------------------------------------------------------------------ */
UINT WINAPI hook_joyGetNumDevs(void)
{
    input_init();
    return 16;
}

UINT WINAPI hook_joyGetDevCapsA(UINT_PTR id, void *caps, UINT cb)
{
    JOYCAPSA *c = caps;
    input_init();
    if (!c || cb < sizeof(JOYCAPSA)) return JOYERR_PARMS;
    if (id != 0 || !s_present) return JOYERR_UNPLUGGED;
    memset(c, 0, sizeof *c);
    c->wMid = 0xFFFF; c->wPid = 0x0097;
    str_cpy(c->szPname, "usnf97fix joystick", sizeof c->szPname);
    if (s_hasStick) { c->wXmax = 65535; c->wYmax = 65535; }
    if (s_hasThrottle) c->wZmax = 65535;
    if (s_hasRudder) c->wRmax = 65535;
    c->wNumButtons = 4;
    c->wPeriodMin = 10; c->wPeriodMax = 1000;
    c->wCaps = (s_hasThrottle ? JOYCAPS_HASZ : 0) | (s_hasRudder ? JOYCAPS_HASR : 0) |
               (s_hasHat ? (JOYCAPS_HASPOV | JOYCAPS_POV4DIR | JOYCAPS_POVCTS) : 0);
    c->wMaxAxes = 6;
    c->wNumAxes = (UINT)((s_hasStick ? 2 : 0) + s_hasThrottle + s_hasRudder);
    c->wMaxButtons = 32;
    str_cpy(c->szRegKey, "DINPUT.DLL", sizeof c->szRegKey);
    return JOYERR_NOERROR;
}

static Virtual snapshot(void)
{
    Virtual v;
    EnterCriticalSection(&s_lock);
    v = s_virt;
    LeaveCriticalSection(&s_lock);
    return v;
}

UINT WINAPI hook_joyGetPos(UINT id, void *p)
{
    JOYINFO *ji = p;
    Virtual v;
    input_init();
    if (!ji) return JOYERR_PARMS;
    if (id != 0 || !s_present) return JOYERR_UNPLUGGED;
    v = snapshot();
    ji->wXpos = (UINT)v.x; ji->wYpos = (UINT)v.y; ji->wZpos = (UINT)v.z; ji->wButtons = v.buttons;
    return JOYERR_NOERROR;
}

UINT WINAPI hook_joyGetPosEx(UINT id, void *p)
{
    JOYINFOEX *ji = p;
    Virtual v;
    DWORD b, n = 0;
    input_init();
    if (!ji || ji->dwSize < sizeof(JOYINFOEX)) return JOYERR_PARMS;
    if (id != 0 || !s_present) return JOYERR_UNPLUGGED;
    v = snapshot();
    ji->dwXpos = (DWORD)v.x; ji->dwYpos = (DWORD)v.y; ji->dwZpos = (DWORD)v.z; ji->dwRpos = (DWORD)v.r;
    ji->dwUpos = ji->dwVpos = 0;
    ji->dwButtons = v.buttons;
    for (b = 0; b < 4; b++) if (v.buttons & (1u << b)) n++;
    ji->dwButtonNumber = n;
    if (v.pov == IN_POV_CENTER) ji->dwPOV = 0xFFFF;               /* JOY_POVCENTERED */
    else if (ji->dwFlags & JOY_RETURNPOVCTS) ji->dwPOV = v.pov;
    else ji->dwPOV = ((v.pov + 4500) / 9000 % 4) * 9000;          /* 4-way like an old hat */
    return JOYERR_NOERROR;
}
