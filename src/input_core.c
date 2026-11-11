/* usnf97fix input: platform-neutral mapping logic. No libc (also linked into the DLL). */
#include "input_core.h"

/* ---------------- tiny string helpers ---------------- */
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int ieq(const char *a, const char *b) { while (*a && lower(*a) == lower(*b)) a++, b++; return lower(*a) == lower(*b); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, int n, int cap)
{
    int i;
    for (i = 0; i < n && i + 1 < cap && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
int in_contains(const char *hay, const char *needle)
{
    int i, j;
    if (!*needle) return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] && lower(hay[i + j]) == lower(needle[j]); j++) { }
        if (!needle[j]) return 1;
    }
    return 0;
}
static int is_space(char c) { return c == ' ' || c == '\t'; }
static int parse_uint(const char *s, int *v)
{
    int n = 0, any = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; any = 1; if (n > 100000) return 0; }
    if (!any || *s) return 0;
    *v = n;
    return 1;
}

/* ---------------- controls ---------------- */
static const char *const k_diAxes[IN_AXES]  = { "X", "Y", "Z", "RX", "RY", "RZ", "S1", "S2" };
static const char *const k_padAxes[IN_AXES] = { "LX", "LY", "LT", "RX", "RY", "RT", "", "" };
static const char *const k_padButtons[]     = { "A", "B", "X", "Y", "LB", "RB", "BACK", "START", "LS", "RS" };

const char *in_axis_name(int idx, int isPad)
{
    if (idx < 0 || idx >= IN_AXES) return "?";
    return isPad ? k_padAxes[idx] : k_diAxes[idx];
}

static int find_axis(const char *c, int isPad)
{
    int i;
    for (i = 0; i < IN_AXES; i++) {
        const char *n = isPad ? k_padAxes[i] : k_diAxes[i];
        if (*n && ieq(c, n)) return i;
    }
    return -1;
}

/* ---------------- parse ---------------- */
int insrc_parse(const char *s, InSrc *o)
{
    const char *colon = 0, *p;
    char ctl[48];
    int n, i;
    for (i = 0; i < (int)sizeof *o; i++) ((char *)o)[i] = 0;
    o->dev = -1; o->idx = o->idx2 = -1;
    while (is_space(*s)) s++;
    for (p = s; *p; p++) if (*p == ':') { colon = p; break; }
    if (!colon || colon == s) return 0;
    n = (int)(colon - s);
    while (n > 0 && is_space(s[n - 1])) n--;
    scpy(o->devPat, s, n, sizeof o->devPat);
    p = colon + 1;
    while (is_space(*p)) p++;
    n = slen(p);
    while (n > 0 && is_space(p[n - 1])) n--;
    scpy(ctl, p, n, sizeof ctl);
    n = slen(ctl);
    if (!n) return 0;
    /* suffixes: tap "!" (last), threshold ">80" / "<20", invert "~" */
    if (ctl[n - 1] == '!') { o->tap = 1; ctl[--n] = 0; if (!n) return 0; }
    for (i = 0; i < n; i++) {
        if (ctl[i] == '>' || ctl[i] == '<') {
            int v;
            if (!parse_uint(ctl + i + 1, &v) || v > 100) return 0;
            o->hasThresh = 1; o->wantGT = ctl[i] == '>'; o->threshPct = v;
            ctl[i] = 0; n = i;
            break;
        }
    }
    if (n && ctl[n - 1] == '~') { o->invert = 1; ctl[--n] = 0; }
    if (!n) return 0;
    /* axis pair "RT-LT" */
    for (i = 1; i < n; i++) {
        if (ctl[i] == '-') {
            scpy(o->ctl, ctl, i, sizeof o->ctl);
            scpy(o->ctl2, ctl + i + 1, n - i - 1, sizeof o->ctl2);
            return o->ctl[0] && o->ctl2[0];
        }
    }
    scpy(o->ctl, ctl, n, sizeof o->ctl);
    return 1;
}

/* inverse of insrc_parse: "dev:ctl[-ctl2][~][>N|<N][!]" */
void insrc_format(const InSrc *s, char *out, int cap)
{
    int n = 0, i;
    const char *parts[4];
    char num[8];
    if (cap <= 0) return;
    parts[0] = s->devPat; parts[1] = ":"; parts[2] = s->ctl; parts[3] = 0;
    out[0] = 0;
    #define APP(str) do { const char *q_ = (str); while (*q_ && n + 1 < cap) out[n++] = *q_++; out[n] = 0; } while (0)
    for (i = 0; parts[i]; i++) APP(parts[i]);
    if (s->ctl2[0]) { APP("-"); APP(s->ctl2); }
    if (s->invert) APP("~");
    if (s->hasThresh) {
        int v = s->threshPct, k = 0;
        char rev[8];
        APP(s->wantGT ? ">" : "<");
        do { rev[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 7);
        for (i = 0; i < k; i++) num[i] = rev[k - 1 - i];
        num[k] = 0;
        APP(num);
    }
    if (s->tap) APP("!");
    #undef APP
}

/* ---------------- resolve ---------------- */
static int find_device(const char *pat, const InDevInfo *d, int n)
{
    int i, v;
    if ((pat[0] == 'p' || pat[0] == 'P') && ieq(pat, "pad")) {
        for (i = 0; i < n; i++) if (d[i].isPad && d[i].connected) return i;
        for (i = 0; i < n; i++) if (d[i].isPad) return i;
        return -1;
    }
    if (slen(pat) == 4 && in_contains(pat, "pad") && pat[3] >= '1' && pat[3] <= '4') {
        for (i = 0; i < n; i++) if (d[i].isPad && d[i].padIndex == pat[3] - '0') return i;
        return -1;
    }
    if (pat[0] == '#' && parse_uint(pat + 1, &v)) {
        int k = 0;
        for (i = 0; i < n; i++) if (!d[i].isPad && ++k == v) return i;
        return -1;
    }
    for (i = 0; i < n; i++) if (in_contains(d[i].name, pat)) return i;
    return -1;
}

int insrc_resolve(InSrc *s, const InDevInfo *devs, int n)
{
    const InDevInfo *d;
    const char *c = s->ctl;
    int i, v;
    s->dev = find_device(s->devPat, devs, n);
    s->kind = SRC_NONE;
    if (s->dev < 0) return 0;
    d = &devs[s->dev];

    if (s->ctl2[0]) {                                 /* pair */
        int a = find_axis(s->ctl, d->isPad), b = find_axis(s->ctl2, d->isPad);
        if (a < 0 || b < 0) return 0;
        s->idx = a; s->idx2 = b; s->kind = SRC_AXISPAIR;
        return 1;
    }
    i = find_axis(c, d->isPad);
    if (i >= 0) {
        s->idx = i;
        s->kind = s->hasThresh ? (s->wantGT ? SRC_AXIS_GT : SRC_AXIS_LT) : SRC_AXIS;
        return 1;
    }
    if (d->isPad) {
        for (i = 0; i < (int)(sizeof k_padButtons / sizeof k_padButtons[0]); i++)
            if (ieq(c, k_padButtons[i])) { s->idx = i; s->kind = SRC_BUTTON; return 1; }
        if (in_contains(c, "DPAD") && slen(c) <= 5) {
            if (slen(c) == 4) { s->idx = 0; s->kind = SRC_POV; return 1; }
            switch (lower(c[4])) {
            case 'u': s->dir = 0; break; case 'r': s->dir = 1; break;
            case 'd': s->dir = 2; break; case 'l': s->dir = 3; break;
            default: return 0;
            }
            s->idx = 0; s->kind = SRC_POVDIR; return 1;
        }
        return 0;
    }
    if ((c[0] == 'B' || c[0] == 'b') && parse_uint(c + 1, &v) && v >= 1 && v <= IN_BUTTONS) {
        s->idx = v - 1; s->kind = SRC_BUTTON; return 1;
    }
    if (lower(c[0]) == 'p' && lower(c[1]) == 'o' && lower(c[2]) == 'v' && c[3] >= '1' && c[3] <= '4') {
        s->idx = c[3] - '1';
        if (!c[4]) { s->kind = SRC_POV; return 1; }
        if (c[5]) return 0;
        switch (lower(c[4])) {
        case 'u': s->dir = 0; break; case 'r': s->dir = 1; break;
        case 'd': s->dir = 2; break; case 'l': s->dir = 3; break;
        default: return 0;
        }
        s->kind = SRC_POVDIR;
        return 1;
    }
    return 0;
}

/* ---------------- evaluate ---------------- */
int insrc_axis(const InSrc *s, const InDevState *st)
{
    int v;
    if (s->dev < 0 || !st) return -1;
    switch (s->kind) {
    case SRC_AXIS:
        v = st->ax[s->idx];
        break;
    case SRC_AXISPAIR:   /* e.g. triggers: RT pushes one way, LT the other */
        v = IN_AXIS_MID + (st->ax[s->idx] - st->ax[s->idx2]) / 2;
        break;
    default:
        return -1;
    }
    if (v < 0) v = 0;
    if (v > IN_AXIS_MAX) v = IN_AXIS_MAX;
    return s->invert ? IN_AXIS_MAX - v : v;
}

static int pov_dir_active(unsigned int pov, int dir)
{
    unsigned int a;
    if (pov == IN_POV_CENTER || pov >= 36000) return 0;
    a = pov;
    switch (dir) {   /* +-67.5 degrees around each direction, so diagonals press both */
    case 0: return a >= 29250 || a <= 6750;
    case 1: return a >= 2250 && a <= 15750;
    case 2: return a >= 11250 && a <= 24750;
    case 3: return a >= 20250 && a <= 33750;
    }
    return 0;
}

int insrc_pressed(const InSrc *s, const InDevState *st)
{
    int v;
    if (s->dev < 0 || !st) return 0;
    switch (s->kind) {
    case SRC_BUTTON:
        return st->btn[s->idx] != 0;
    case SRC_POVDIR:
        return pov_dir_active(st->pov[s->idx], s->dir);
    case SRC_AXIS_GT:
    case SRC_AXIS_LT:
        v = st->ax[s->idx];
        if (s->invert) v = IN_AXIS_MAX - v;
        v = v * 100 / IN_AXIS_MAX;
        return s->kind == SRC_AXIS_GT ? v > s->threshPct : v < s->threshPct;
    }
    return 0;
}

int in_deadzone(int v, int pct)
{
    int d, dz, sign, out;
    if (pct <= 0) return v;
    if (pct > 50) pct = 50;
    d = v - IN_AXIS_MID;
    sign = d < 0 ? -1 : 1;
    if (d < 0) d = -d;
    dz = IN_AXIS_MID * pct / 100;
    if (d <= dz) return IN_AXIS_MID;
    {
        int limit = sign < 0 ? IN_AXIS_MID : IN_AXIS_MAX - IN_AXIS_MID;   /* 32768 down, 32767 up */
        if (d > limit) d = limit;
        out = (d - dz) * limit / (limit - dz);
    }
    v = IN_AXIS_MID + sign * out;
    if (v < 0) v = 0;
    if (v > IN_AXIS_MAX) v = IN_AXIS_MAX;
    return v;
}

/* ---------------- keys ---------------- */
typedef struct { const char *name; unsigned char scan; } KeyName;
static const KeyName k_keys[] = {
    {"Esc",0x01},{"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},{"7",0x08},{"8",0x09},{"9",0x0a},
    {"0",0x0b},{"-",0x0c},{"=",0x0d},{"Backspace",0x0e},{"BS",0x0e},{"Tab",0x0f},
    {"q",0x10},{"w",0x11},{"e",0x12},{"r",0x13},{"t",0x14},{"y",0x15},{"u",0x16},{"i",0x17},{"o",0x18},{"p",0x19},
    {"[",0x1a},{"]",0x1b},{"Enter",0x1c},{"Ctrl",0x1d},
    {"a",0x1e},{"s",0x1f},{"d",0x20},{"f",0x21},{"g",0x22},{"h",0x23},{"j",0x24},{"k",0x25},{"l",0x26},
    {";",0x27},{"'",0x28},{"`",0x29},{"Shift",0x2a},{"\\",0x2b},
    {"z",0x2c},{"x",0x2d},{"c",0x2e},{"v",0x2f},{"b",0x30},{"n",0x31},{"m",0x32},{",",0x33},{".",0x34},{"/",0x35},
    {"Num*",0x37},{"Alt",0x38},{"Space",0x39},{"CapsLock",0x3a},
    {"F1",0x3b},{"F2",0x3c},{"F3",0x3d},{"F4",0x3e},{"F5",0x3f},{"F6",0x40},{"F7",0x41},{"F8",0x42},{"F9",0x43},{"F10",0x44},
    {"Home",0x47},{"Num7",0x47},{"Up",0x48},{"Num8",0x48},{"PgUp",0x49},{"Num9",0x49},{"Num-",0x4a},
    {"Left",0x4b},{"Num4",0x4b},{"Num5",0x4c},{"Right",0x4d},{"Num6",0x4d},{"Num+",0x4e},
    {"End",0x4f},{"Num1",0x4f},{"Down",0x50},{"Num2",0x50},{"PgDn",0x51},{"Num3",0x51},
    {"Insert",0x52},{"Ins",0x52},{"Num0",0x52},{"Delete",0x53},{"Del",0x53},{"F11",0x57},{"F12",0x58},
    {0,0}
};
/* shifted symbols on a US layout -> base key */
static const char k_shifted[]   = "!@#$%^&*()_+{}:\"~|<>?";
static const char k_unshifted[] = "1234567890-=[];'`\\,./";

/* Commands taken from the game's flight menu (FMENUD.MNU) and its key handlers. */
const InAlias g_inAliases[] = {
    {"Gear","g","landing gear"}, {"Flaps","f","flaps"}, {"Brakes","b","wheel/air brakes"}, {"Hook","h","tailhook"},
    {"Autopilot","a","autopilot"}, {"Jammer","j","ECM jammer"}, {"DamageReport","d","damage report"},
    {"Target","t","next target"}, {"WeaponPrev","[","previous weapon"}, {"WeaponNext","]","next weapon"},
    {"FireWeapon","Space","fire selected weapon"}, {"FireGun","Tab","fire gun"},
    {"Chaff","Insert","chaff"}, {"Flare","Delete","flare"}, {"Eject","E","eject"},
    {"ThrottleIdle","1","throttle idle"}, {"Throttle25","2","throttle 25%"}, {"Throttle50","3","throttle 50%"},
    {"Throttle75","4","throttle 75%"}, {"ThrottleFull","5","throttle 100%"}, {"Afterburner","6","afterburner"},
    {"ThrottleDown","7","throttle -5%"}, {"ThrottleUp","8","throttle +5%"},
    {"StickForward","Up","nose down"}, {"StickBack","Down","nose up"}, {"StickLeft","Left","roll left"},
    {"StickRight","Right","roll right"}, {"RudderLeft","End","left rudder"}, {"RudderRight","PgDn","right rudder"},
    {"ViewFront","F1","front view"}, {"ViewBack","F2","back view"}, {"ViewUp","F3","up view"},
    {"ViewTrack","F4","track view"}, {"ViewMissile","F5","player/missile view"}, {"ViewWingman","F6","player/wingman view"},
    {"ViewTarget","F7","player/target view"}, {"ViewFromTarget","F8","target/player view"}, {"ViewFlyBy","F9","fly-by view"},
    {"ViewExternal","F10","external view"}, {"ViewMissileCam","F12","missile camera"}, {"Cockpit","Backspace","show cockpit"},
    {"Pause","Ctrl+p","pause"}, {"SlowMotion","Shift+c","slow motion"}, {"TargetInfo","Ctrl+t","target info"},
    {"RadioSilence","Alt+s","radio silence"}, {"EndMission","Ctrl+q","end mission"},
    {"HudDim","Shift+[","dim HUD"}, {"HudBright","Shift+]","brighten HUD"},
    {"WindowEnvelope","Shift+1","envelope window"}, {"WindowForward","Shift+2","forward view window"},
    {"WindowOther","Shift+3","other view window"}, {"WindowTarget","Shift+4","target window"}, {"WindowRWR","Shift+5","RWR"},
    {"WindowNav","Shift+6","navigation window"}, {"WindowStatus","Shift+7","system status"},
    {"WindowWeapons","Shift+8","weapon status"}, {"WindowRadar","Shift+9","radar window"},
    {"RadarRangeDown",",","radar range"}, {"RadarRangeUp",".","radar range"},
    {0,0,0}
};

static int keyname_scan(const char *s, int *shift)
{
    int i;
    *shift = 0;
    if (s[0] && !s[1]) {                       /* single character: case-sensitive like the game */
        char c = s[0];
        if (c >= 'A' && c <= 'Z') { *shift = 1; c = (char)(c + 32); }
        for (i = 0; k_shifted[i]; i++) if (c == k_shifted[i]) { *shift = 1; c = k_unshifted[i]; break; }
        for (i = 0; k_keys[i].name; i++) if (k_keys[i].name[0] == c && !k_keys[i].name[1]) return k_keys[i].scan;
        return -1;
    }
    for (i = 0; k_keys[i].name; i++) if (ieq(s, k_keys[i].name)) return k_keys[i].scan;
    return -1;
}

int keyspec_parse(const char *s, KeySpec *o)
{
    char part[32];
    int i, mods = 0, shift, scan = -1;
    const char *p;
    while (is_space(*s)) s++;
    for (i = 0; g_inAliases[i].name; i++)
        if (ieq(s, g_inAliases[i].name)) return keyspec_parse(g_inAliases[i].key, o);
    p = s;
    for (;;) {
        int n = 0;
        /* a '+' that is the very last char (or "Shift++") is the key itself */
        while (p[n] && !(p[n] == '+' && n > 0)) n++;
        scpy(part, p, n, sizeof part);
        while (n > 0 && is_space(part[n - 1])) part[--n] = 0;
        if (p[n] == '+') {
            if (ieq(part, "Shift")) mods |= 1;
            else if (ieq(part, "Ctrl")) mods |= 2;
            else if (ieq(part, "Alt")) mods |= 4;
            else {                                   /* not a modifier: the '+' belongs to the name ("Num+") */
                scan = keyname_scan(p, &shift);
                if (scan < 0) return 0;
                if (shift) mods |= 1;
                break;
            }
            p += n + 1;
            while (is_space(*p)) p++;
            continue;
        }
        scan = keyname_scan(part, &shift);
        if (scan < 0) return 0;
        if (shift) mods |= 1;
        break;
    }
    o->scan = (unsigned char)scan;
    o->mods = (unsigned char)mods;
    return 1;
}
