/* usnf97fix: internal declarations shared by the shim's modules. */
#ifndef SHIM_H
#define SHIM_H
#include "ddraw_min.h"

#define SHIM_VERSION "0.1.0"

/* ---------- config (usnf97fix.ini, section [usnf97fix]) ---------- */
typedef struct {
    int  windowMode;        /* 0 = windowed, 1 = borderless fullscreen */
    int  windowScale;       /* windowed: integer multiple of 640x480; 0 = auto fit */
    int  filter;            /* 0 = nearest (sharp), 1 = smooth (GDI halftone) */
    int  aspectCorrect;     /* 1 = show 320x200 at 4:3 like a CRT did */
    int  stretch;           /* 1 = fill the window, ignore aspect */
    int  vsync;             /* 1 = pace presentation to the compositor (DwmFlush) */
    int  frameCap;          /* 0 = off; else max game fps (only lowers the cap) */
    int  fixGameSpeed;      /* 1 = wait for real time instead of clamping the timestep */
    int  minFrameTicks;     /* game timestep floor in 1/256 s (game hard-codes 5) */
    int  pauseInBackground; /* 1 = vanilla: game pauses when it loses focus */
    int  confineMouse;      /* 1 = keep the pointer in the game picture while it is active */
    int  traceMessages;     /* debug: log keyboard/activation window messages */
    int  log;               /* 1 = write usnf97fix.log */
    char cdPath[260];       /* "" = auto (game folder if USNF_10.LIB is there, else scan CDs) */
} Config;

extern Config g_cfg;
extern char   g_gameDir[260];   /* exe folder, with trailing backslash */
extern char   g_iniPath[280];   /* usnf97fix.ini */

void config_load(void);
void log_open(void);
void logf(const char *fmt, ...);
void log_raw(const char *s);
void crash_logger_install(HINSTANCE dll);

/* ---------- tiny CRT ---------- */
void *memset(void *d, int c, unsigned int n);
void *memcpy(void *d, const void *s, unsigned int n);
void *memmove(void *d, const void *s, unsigned int n);
int   memcmp(const void *a, const void *b, unsigned int n);
unsigned int str_len(const char *s);
int   str_eq(const char *a, const char *b);
int   str_ieq(const char *a, const char *b);
void  str_cpy(char *d, const char *s, unsigned int cap);
void  str_cat(char *d, const char *s, unsigned int cap);
void *mem_alloc(unsigned int n);     /* zeroed */
void  mem_free(void *p);

/* ---------- timing ---------- */
void      time_init(void);
LONGLONG  qpc_now(void);
LONGLONG  qpc_freq(void);
void      wait_until(LONGLONG qpcTarget);   /* sleep + short spin */

/* ---------- DirectDraw objects ---------- */
typedef struct DDPalette DDPalette;
typedef struct DDSurface DDSurface;
typedef struct DDObject  DDObject;

struct DDPalette {
    void        *vtbl;
    LONG         ref;
    DWORD        caps;
    PALETTEENTRY entries[256];
};

struct DDSurface {
    void       *vtbl;
    LONG        ref;
    DDObject   *dd;
    DWORD       caps;          /* DDSCAPS_* */
    int         w, h, pitch;
    BYTE       *mem;           /* current pixel buffer (swapped by Flip) */
    DDSurface  *back;          /* primary: attached back buffer (flip chain) */
    DDSurface  *front;         /* back buffer: owning primary */
    DDPalette  *pal;
    DDCOLORKEY  srcKey;
    int         hasSrcKey;
    int         locks;
};

struct DDObject {
    void      *vtbl1;          /* IDirectDraw  */
    void      *vtbl2;          /* IDirectDraw2 */
    LONG       ref;
    HWND       hwnd;
    DWORD      coopFlags;
    int        modeW, modeH, modeBpp;
    DDSurface *primary;
};

#define DD_FROM_V1(p) ((DDObject *)(p))
#define DD_FROM_V2(p) ((DDObject *)((BYTE *)(p) - sizeof(void *)))

extern CRITICAL_SECTION g_ddLock;      /* guards surface memory / palette / primary */
/* The game suspends its render thread on focus loss (possibly mid-Blt), so never wait
 * forever: after 500 ms dd_lock() proceeds unlocked rather than deadlocking. */
int  dd_lock(void);
void dd_unlock(int held);
#define LOCK_DD()   int dd_held_ = dd_lock()
#define UNLOCK_DD() dd_unlock(dd_held_)

HRESULT dd_create(void **out);
HRESULT surface_create(DDObject *dd, const DDSURFACEDESC *desc, DDSurface **out);
HRESULT palette_create(DWORD flags, const PALETTEENTRY *init, DDPalette **out);
void    surface_addref(DDSurface *s);
ULONG   surface_release(DDSurface *s);
void    palette_addref(DDPalette *p);
ULONG   palette_release(DDPalette *p);

/* ---------- presentation / window ---------- */
void present_init(void);
void present_set_window(HWND hwnd);                 /* from SetCooperativeLevel */
void present_set_mode(int w, int h);                /* from SetDisplayMode */
void present_set_primary(DDSurface *primary);
void present_release_primary(DDSurface *primary);   /* no-op unless it is the current one */
void present_mark_dirty(int commit);                /* commit=1: frame complete, present now */
void present_wait_vblank(void);
int  present_is_primary_mem(const DDSurface *s);    /* is s the visible front buffer? */
DDSurface *present_primary(void);                   /* call with g_ddLock held */

HWND present_hwnd(void);

/* ---------- input (input.c) ---------- */
void input_configure(void);          /* DllMain: read [input] Enable */
int  input_enabled(void);
UINT WINAPI hook_joyGetNumDevs(void);
UINT WINAPI hook_joyGetDevCapsA(UINT_PTR id, void *caps, UINT cb);
UINT WINAPI hook_joyGetPos(UINT id, void *ji);
UINT WINAPI hook_joyGetPosEx(UINT id, void *ji);

/* exe IAT hooks (installed from DllMain) */
HWND WINAPI hook_CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
BOOL WINAPI hook_GetCursorPos(POINT *);
int  WINAPI hook_ShowCursor(BOOL);
BOOL WINAPI hook_ClipCursor(const RECT *);

/* ---------- exe patching ---------- */
DWORD patches_symbol(const char *name);            /* USNF.SMS lookup, 0 if unknown */
const char *patches_symbol_at(DWORD addr, DWORD *base);
void hang_watch_start(HWND hwnd);
void patches_apply(void);                          /* DllMain: CD path, IAT hooks, speed fix */
int  iat_hook(HMODULE mod, const char *dll, const char *func, void *repl, void **orig);

#endif
