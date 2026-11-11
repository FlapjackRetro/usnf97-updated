/* usnf97fix: higher internal resolution for the in-flight 3D scene (opt-in,
 * [usnf97fix] RenderScale=2).
 *
 * The cockpit art, HUD, menus and panels stay at the game's resolution; only the 3D
 * scene of the main view is rendered k times larger:
 *
 *  1. FlyingLoop's T_Render(&mainV) goes through hires_T_Render: it renders the view
 *     into our own k x bitmap first (_cb swapped, clip box scaled, _sizeAdjust x k),
 *     then renders it normally. The normal render runs last, so every projection global,
 *     the visible-target list and everything the HUD reads are exactly as before.
 *  2. Right after the normal render the frame is snapshotted.
 *  3. GG_Flush (frame done) compares the finished frame with the snapshot: pixels that
 *     changed were drawn by 2D layers (cockpit, HUD, menus, panels) and are scaled up
 *     k x; everything else comes from the k x render. The presenter shows that
 *     composite instead of the primary surface.
 *
 * k is limited to 2 and the flight height to 600: the engine's per-scanline tables are
 * sized for 1200 lines (_lineStats is followed directly by the texture mapper's state).
 * The 2D-layer test is exact except where an overlay pixel happens to equal the scene
 * pixel under it, which then shows the (identical-colour) high-res scene. */
#include "shim.h"

#define BMP_HDR 0x32

typedef void (__stdcall *TRenderFn)(void *view);
typedef void (__stdcall *GRRenderFn)(void *, void *, void *, int, void *, int, int, int, int);
typedef void (__fastcall *SetClipFn)(int left, int top, int w, int hm1);
typedef void (__fastcall *FlushFn)(int arg);

static int        s_k;
static TRenderFn  s_TRender;
static GRRenderFn s_GRRender;
static SetClipFn  s_SetClip;
static FlushFn    s_flushTramp;
static BYTE     **s_cb, **s_mainBitmap;
static WORD      *s_sizeAdjust;
static void      *s_mainV;

static BYTE  *s_big;             /* our bitmap: header + row table + pixels */
static int    s_bigW, s_bigH;
static BYTE  *s_snap;            /* normal frame right after its 3D render */
static int    s_snapW, s_snapH;
static BYTE  *s_comp;            /* composite handed to the presenter */
static volatile int s_inHires, s_frameReady;
static BYTE   s_tramp[16];
static int    s_logged;

static DWORD rd32(const BYTE *p) { DWORD v; memcpy(&v, p, 4); return v; }

static int bmp_w(const BYTE *b) { return (int)rd32(b + 2); }
static int bmp_h(const BYTE *b) { return (int)rd32(b + 6); }
static BYTE *bmp_row(const BYTE *b, int y) { return (BYTE *)rd32((const BYTE *)rd32(b + 0x22) + 4 * (DWORD)y); }

/* a bitmap in the game's layout (see G_AllocSurfaceBitmap): +2 w, +6 h, +0A pixels,
 * +0E w*h, +22 row table, +26 4*h; row table right after the 0x32-byte header */
static int ensure_big(const BYTE *proto, int w, int h)
{
    DWORD *rows;
    BYTE *pix;
    int y;
    if (s_big && s_bigW == w && s_bigH == h) return 1;
    if (s_big) mem_free(s_big);
    s_big = mem_alloc((unsigned)(BMP_HDR + 4 * h + w * h));
    if (!s_big) return 0;
    memset(s_big, 0, BMP_HDR);
    memcpy(s_big, proto, 2);                          /* type word, as the main bitmap */
    rows = (DWORD *)(s_big + BMP_HDR);
    pix = s_big + BMP_HDR + 4 * h;
    memcpy(s_big + 2, &w, 4);
    memcpy(s_big + 6, &h, 4);
    { DWORD v = (DWORD)pix; memcpy(s_big + 0x0A, &v, 4); }
    { DWORD v = (DWORD)(w * h); memcpy(s_big + 0x0E, &v, 4); }
    { DWORD v = (DWORD)rows; memcpy(s_big + 0x22, &v, 4); }
    { DWORD v = (DWORD)(4 * h); memcpy(s_big + 0x26, &v, 4); }
    for (y = 0; y < h; y++) rows[y] = (DWORD)(pix + y * w);
    memset(pix, 0, (unsigned)(w * h));
    s_bigW = w; s_bigH = h;
    return 1;
}

static void __stdcall hires_GRRender(void *a, void *b, void *c, int d, void *e, int f, int g, int h, int i)
{
    if (s_inHires) {
        WORD saved = *s_sizeAdjust;
        *s_sizeAdjust = (WORD)(saved * s_k);
        s_GRRender(a, b, c, d, e, f, g, h, i);
        *s_sizeAdjust = saved;
    } else {
        s_GRRender(a, b, c, d, e, f, g, h, i);
    }
}

static void __stdcall hires_T_Render(void *view)
{
    BYTE *cb = *s_cb;
    int w, h, y;
    if (view != s_mainV || !cb || cb != *s_mainBitmap) { s_TRender(view); return; }
    w = bmp_w(cb); h = bmp_h(cb);
    if (w <= 0 || h <= 0 || h > 600 || !ensure_big(cb, w * s_k, h * s_k)) { s_TRender(view); return; }

    /* k x pass into our bitmap */
    *s_cb = s_big;
    s_SetClip(0, 0, s_bigW, s_bigH - 1);
    s_inHires = 1;
    s_TRender(view);
    s_inHires = 0;
    *s_cb = cb;
    s_SetClip(0, 0, w, h - 1);

    /* normal pass, then snapshot */
    s_TRender(view);
    if (!s_snap || s_snapW != w || s_snapH != h) {
        mem_free(s_snap);
        s_snap = mem_alloc((unsigned)(w * h));
        s_snapW = w; s_snapH = h;
        if (!s_snap) return;
    }
    for (y = 0; y < h; y++) memcpy(s_snap + y * w, bmp_row(cb, y), (unsigned)w);
    s_frameReady = 1;
    if (!s_logged) { s_logged = 1; logf("render scale: 3D at %d x %d for a %d x %d frame", s_bigW, s_bigH, w, h); }
}

static void compose(void)
{
    const BYTE *mb = *s_mainBitmap;
    int w = s_snapW, h = s_snapH, k = s_k, W = s_bigW, x, y, i, j;
    if (!mb || bmp_w(mb) != w || bmp_h(mb) != h || W != w * k) { present_set_hires(NULL, 0, 0, 0, 0); return; }
    LOCK_DD();
    if (!s_comp) s_comp = mem_alloc((unsigned)(W * s_bigH));
    if (s_comp) {
        for (y = 0; y < h; y++) {
            const BYTE *fin = bmp_row(mb, y), *snp = s_snap + y * w;
            BYTE *dst = s_comp + (y * k) * W;
            for (j = 0; j < k; j++) memcpy(dst + j * W, bmp_row(s_big, y * k + j), (unsigned)W);
            for (x = 0; x < w; x++) {
                if (fin[x] == snp[x]) continue;
                for (j = 0; j < k; j++)
                    for (i = 0; i < k; i++) dst[j * W + x * k + i] = fin[x];
            }
        }
        present_set_hires(s_comp, W, s_bigH, w, h);
    }
    UNLOCK_DD();
}

static void __fastcall hires_GG_Flush(int arg)
{
    if (s_frameReady) { s_frameReady = 0; compose(); }
    else present_set_hires(NULL, 0, 0, 0, 0);
    s_flushTramp(arg);
}

void hires_apply(void)
{
    DWORD fly, flyHi, tr, trHi, gr, flush, site, a, old;
    int k = (int)GetPrivateProfileIntA("usnf97fix", "RenderScale", 1, g_iniPath);
    if (k <= 1) return;
    if (k > 2) { logf("render scale: %d is above the engine's limit; using 2", k); k = 2; }

    fly = patches_symbol("?FlyingLoop@@YAXXZ"); tr = patches_symbol("_T_Render@4");
    gr = patches_symbol("_GRRender@36"); flush = patches_symbol("@GG_Flush@4");
    s_SetClip = (SetClipFn)patches_symbol("@G_SetClipBox@16");
    s_cb = (BYTE **)patches_symbol("_cb"); s_mainBitmap = (BYTE **)patches_symbol("_mainBitmap");
    s_sizeAdjust = (WORD *)patches_symbol("_sizeAdjust"); s_mainV = (void *)patches_symbol("_mainV");
    if (!fly || !tr || !gr || !flush || !s_SetClip || !s_cb || !s_mainBitmap || !s_sizeAdjust || !s_mainV) {
        logf("render scale: symbols missing; off"); return;
    }
    flyHi = fly + (patches_symbol_size(fly) ? patches_symbol_size(fly) : 0x2000);
    trHi = tr + (patches_symbol_size(tr) ? patches_symbol_size(tr) : 0x200);

    /* FlyingLoop: push offset _mainV; call _T_Render@4 */
    for (site = 0, a = fly; a + 10 <= flyHi; a++) {
        const BYTE *p = (const BYTE *)a;
        if (p[0] == 0x68 && rd32(p + 1) == (DWORD)s_mainV && p[5] == 0xE8 && a + 10 + rd32(p + 6) == tr) { site = a + 5; break; }
    }
    if (!site) { logf("render scale: FlyingLoop main-view render not found; off"); return; }
    /* T_Render: call _GRRender@36 */
    for (a = tr; a + 5 <= trHi; a++) {
        const BYTE *p = (const BYTE *)a;
        if (p[0] == 0xE8 && a + 5 + rd32(p + 1) == gr) break;
    }
    if (a + 5 > trHi) { logf("render scale: T_Render's GRRender call not found; off"); return; }
    /* GG_Flush entry: mov eax, fs:[0]  (64 A1 00000000) */
    if (memcmp((const void *)flush, "\x64\xA1\x00\x00\x00\x00", 6) != 0) { logf("render scale: GG_Flush entry not as expected; off"); return; }

    s_k = k;
    s_TRender = (TRenderFn)tr;
    s_GRRender = (GRRenderFn)gr;
    memcpy(s_tramp, (const void *)flush, 6);
    s_tramp[6] = 0xE9; { DWORD r = (flush + 6) - ((DWORD)s_tramp + 11); memcpy(s_tramp + 7, &r, 4); }
    if (!VirtualProtect(s_tramp, sizeof s_tramp, PAGE_EXECUTE_READWRITE, &old)) { logf("render scale: VirtualProtect failed; off"); return; }
    FlushInstructionCache(GetCurrentProcess(), s_tramp, sizeof s_tramp);
    s_flushTramp = (FlushFn)(void *)s_tramp;

    {
        BYTE b[6];
        DWORD r = (DWORD)hires_T_Render - (site + 5), r2 = (DWORD)hires_GRRender - (a + 5), r3 = (DWORD)hires_GG_Flush - (flush + 5);
        int n = 0;
        b[0] = 0xE8; memcpy(b + 1, &r, 4);  n += write_code((void *)site, b, 5);
        b[0] = 0xE8; memcpy(b + 1, &r2, 4); n += write_code((void *)a, b, 5);
        b[0] = 0xE9; memcpy(b + 1, &r3, 4); b[5] = 0x90; n += write_code((void *)flush, b, 6);
        logf("render scale: %d x for the 3D scene (%d/3 patches; FlyingLoop %08lX, T_Render %08lX, GG_Flush %08lX)", k, n, site, a, flush);
    }
}
