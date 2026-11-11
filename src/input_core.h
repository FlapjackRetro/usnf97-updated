/* usnf97fix input: platform-neutral mapping logic (unit-tested on the host). */
#ifndef INPUT_CORE_H
#define INPUT_CORE_H

#define IN_MAX_DEVS   16
#define IN_AXES       8          /* X Y Z RX RY RZ S1 S2 (pads: LX LY LT RX RY RT) */
#define IN_POVS       4
#define IN_BUTTONS    128
#define IN_AXIS_MAX   65535
#define IN_AXIS_MID   32768
#define IN_POV_CENTER 0xFFFFFFFFu

typedef struct {
    char name[128];
    int  isPad;                  /* XInput pad: control names LX LY LT RX RY RT, A B X Y LB RB BACK START LS RS, DPAD */
    int  padIndex;               /* 1..4 */
    int  axisMask;               /* bit n = axis n exists */
    int  nButtons, nPovs;
    int  connected;
} InDevInfo;

typedef struct {
    int           ax[IN_AXES];   /* 0..65535 */
    unsigned int  pov[IN_POVS];  /* hundredths of degrees, IN_POV_CENTER when centred */
    unsigned char btn[IN_BUTTONS];
} InDevState;

enum { SRC_NONE, SRC_AXIS, SRC_AXISPAIR, SRC_BUTTON, SRC_POV, SRC_POVDIR, SRC_AXIS_GT, SRC_AXIS_LT };

typedef struct {
    int  kind;                   /* after resolve */
    char devPat[64];             /* "Echo", "pad", "pad2", "#1" */
    char ctl[24], ctl2[24];      /* control name(s) as written */
    int  invert;
    int  threshPct;              /* for > / < */
    int  wantGT;                 /* 1 = '>', 0 = '<' (when threshPct set) */
    int  hasThresh;
    int  dev, idx, idx2, dir;    /* resolved: device index, control index, POV direction 0..3 (U R D L) */
} InSrc;

typedef struct { unsigned char scan; unsigned char mods; } KeySpec;   /* mods: 1 shift, 2 ctrl, 4 alt */

/* "Echo:X~", "pad:RT-LT", "#2:B5", "Echo:POV1U", "Echo:S1>80". 0 on syntax error. */
int  insrc_parse(const char *s, InSrc *out);
/* Bind a parsed source to the current device list. 0 if the device or control is not present. */
int  insrc_resolve(InSrc *s, const InDevInfo *devs, int n);
/* Value of an axis source, 0..65535 (centre 32768); -1 if unavailable. */
int  insrc_axis(const InSrc *s, const InDevState *st);
/* Is a button-like source active? (buttons, POV directions, axis thresholds) */
int  insrc_pressed(const InSrc *s, const InDevState *st);
/* Centered deadzone (percent) with rescale so the output still reaches the ends. */
int  in_deadzone(int v, int pct);
/* "g", "E" (= Shift+e), "Shift+F1", "Ctrl+P", "PgDn", or a command alias ("Gear", "Eject"...). */
int  keyspec_parse(const char *s, KeySpec *out);
/* Name for logging. */
const char *in_axis_name(int idx, int isPad);
/* Case-insensitive substring. */
int  in_contains(const char *hay, const char *needle);

/* Command aliases (exported for documentation / tests). */
typedef struct { const char *name, *key, *what; } InAlias;
extern const InAlias g_inAliases[];

#endif
