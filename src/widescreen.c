/* usnf97fix: wide flight view (opt-in, [usnf97fix] WideFlight=1).
 *
 * When the game's flight resolution is 640x480, flight runs at W x 480 instead (W from
 * WideFlightWidth, or the desktop's aspect ratio: 1704 on a 32:9 screen). Menus stay
 * 640x480. Five places in USNF.EXE change, each found by symbol + byte pattern and
 * verified before writing (see NOTES.md, "Wide flight view"):
 *
 *  1. usnfmain reads the flight mode three times (mov eax,[flightMode]); a stub turns
 *     mode 2 (640x480) into 0x20, a new mode, so entering/leaving flight switches modes.
 *  2. GG_GetMode: mode 0x20 fills _grMode with W x 480 (aspect 0x7fff like 640x480).
 *  3. GG_InitMode: mode 0x20 calls SetDisplayMode(W, 480, 8).
 *  4. HUDInit picks the high-res cockpit art and 640 positions only for exactly
 *     640x480; its four 640 constants become W.
 *  5. render_3d normalizes the projection to the larger screen axis, which makes a wider
 *     screen zoom in (vert-). The aspect computation now sees a 4:3-equivalent width
 *     (min(scrw, scrh*4/3)), and the x scale is then multiplied by that / scrw, so the
 *     vertical FOV stays as at 640x480 and the extra width shows more scene (hor+).
 *     Viewports narrower than 4:3 (the small view windows) are unchanged.
 *
 *  6. G_SUPolygon (Gouraud spans: the horizon haze) counts 4-pixel groups in CL, so
 *     spans longer than 1023 pixels wrap (1704 drew as 680). The count becomes CX;
 *     ECX's low word is otherwise unused there (the colour steps are masked to the
 *     high word), so this is safe.
 *
 *  7. The three affine texture span loops (AC_interpolate_linear_span, ils_w256_unroll,
 *     ils_w256_unroll_nc) have the same 8-bit group counter (mov cl,al ... dec cl). The
 *     texture accumulators use only ECX's high word (ECX = x<<16, steps <<16), so the
 *     count setup becomes a call to "shr eax,2; mov cx,ax; sub edi,4" and the loop
 *     uses dec cx. Affects close-up textured polygons wider than 1023 pixels.
 *
 * Everything else (3D clip box, HUD centre, dialogs, cockpit panels, mouse limits)
 * already follows the screen size. */
#include "shim.h"

#define WIDE_MODE 0x20

static BYTE  s_code[256];
static int   s_codeUsed;
static DWORD s_projW;              /* written by the render_3d stub */
static char  s_modeName[24];

static BYTE *code_alloc(int n)
{
    BYTE *p;
    if (s_codeUsed + n > (int)sizeof s_code) return NULL;
    p = s_code + s_codeUsed;
    s_codeUsed += n;
    return p;
}

static void put32(BYTE *p, DWORD v) { memcpy(p, &v, 4); }
static void put16(BYTE *p, WORD v)  { memcpy(p, &v, 2); }
static DWORD rel32(const BYTE *at, DWORD target) { return target - ((DWORD)at + 4); }

/* first occurrence of pat (with 0xFFFF wildcards) in [lo, hi) */
static DWORD find(DWORD lo, DWORD hi, const unsigned short *pat, int n)
{
    DWORD a;
    int i;
    for (a = lo; a + (DWORD)n <= hi; a++) {
        for (i = 0; i < n; i++)
            if (pat[i] != 0xFFFF && ((const BYTE *)a)[i] != (BYTE)pat[i]) break;
        if (i == n) return a;
    }
    return 0;
}

static DWORD fn_range(const char *sym, DWORD *hi)
{
    DWORD a = patches_symbol(sym), n;
    if (!a) return 0;
    n = patches_symbol_size(a);
    if (!n || n > 0x4000) n = 0x4000;
    *hi = a + n;
    return a;
}

/* "call stub; nop; nop" over a 7-byte instruction */
static int call7(DWORD site, const BYTE *stub)
{
    BYTE b[7];
    b[0] = 0xE8; put32(b + 1, (DWORD)stub - (site + 5)); b[5] = b[6] = 0x90;
    return write_code((void *)site, b, 7);
}

static int wide_width(void)
{
    int w = (int)GetPrivateProfileIntA("usnf97fix", "WideFlightWidth", 0, g_iniPath);
    if (w <= 0) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        if (sw <= 0 || sh <= 0) return 0;
        w = 480 * sw / sh;
    }
    /* multiple of 8: the game copies frames assuming row length == width, and every mode
     * it shipped with (640/800/1024/1600) is a multiple of 32. Our surfaces pad rows to
     * 4 bytes, so e.g. 1706 got 1708-byte rows and every 2D layer came out sheared. */
    w &= ~7;
    if (w > 2560) w = 2560;
    return w;
}

void widescreen_apply(void)
{
    DWORD lo, hi, a, fm = 0, gr, scrw, scrh, ax;
    int W, n;
    BYTE *s;

    if (!GetPrivateProfileIntA("usnf97fix", "WideFlight", 0, g_iniPath)) return;
    W = wide_width();
    if (W <= 640) { logf("wide flight: screen is not wider than 4:3 (width %d); off", W); return; }
    if (!VirtualProtect(s_code, sizeof s_code, PAGE_EXECUTE_READWRITE, &a)) { logf("wide flight: VirtualProtect failed"); return; }

    gr = patches_symbol("_grMode");
    scrw = patches_symbol("scrw"); scrh = patches_symbol("scrh"); ax = patches_symbol("_aspectx");
    if (!gr || !scrw || !scrh || !ax) { logf("wide flight: symbols missing; off"); return; }

    /* ---- locate and verify every site before changing anything ---- */
    DWORD um[3], getJa, initSite, initJe, initDef, initCont, hud[4], rSite1, rSite2, gmHi, spanSite;
    DWORD linStart[3], linDec[3];
    {
        /* 1. usnfmain: mov eax,[fm]; mov ecx,[shell]; cmp ecx,eax  (enter + leave) */
        static const unsigned short p1[] = { 0xA1,0xFFFF,0xFFFF,0xFFFF,0xFFFF, 0x8B,0x0D,0xFFFF,0xFFFF,0xFFFF,0xFFFF, 0x3B,0xC8 };
        unsigned short p2[7];
        if (!(lo = fn_range("?usnfmain@@YAXXZ", &hi))) { logf("wide flight: usnfmain not found; off"); return; }
        um[0] = find(lo, hi, p1, 13);
        if (!um[0]) { logf("wide flight: usnfmain mode check not found; off"); return; }
        fm = *(DWORD *)(um[0] + 1);
        um[2] = find(um[0] + 13, hi, p1, 13);
        /* mov eax,[fm]; push eax (InitGraphicsMode argument) */
        p2[0] = 0xA1; p2[1] = fm & 0xFF; p2[2] = (fm >> 8) & 0xFF; p2[3] = (fm >> 16) & 0xFF; p2[4] = fm >> 24; p2[5] = 0x50; p2[6] = 0xE8;
        um[1] = find(um[0] + 13, hi, p2, 7);
        if (!um[1] || !um[2] || *(DWORD *)(um[2] + 1) != fm) { logf("wide flight: usnfmain sites not as expected; off"); return; }
    }
    {
        /* 2. GG_GetMode: mov eax,[mode]; dec eax; cmp eax,0Fh; ja rel32 */
        static const unsigned short p[] = { 0xA1,0xFFFF,0xFFFF,0xFFFF,0xFFFF, 0x48, 0x83,0xF8,0x0F, 0x0F,0x87 };
        if (!(lo = fn_range("@GG_GetMode@0", &gmHi)) || find(lo, lo + 11, p, 11) != lo) { logf("wide flight: GG_GetMode not as expected; off"); return; }
        getJa = lo + 9;
    }
    {
        /* 3. GG_InitMode: cmp ecx,10h; je short; jmp short (default)  and the 320x200 block
         *    "push 8; push 0C8h; push 140h; jmp short cont" for the continuation */
        static const unsigned short p[] = { 0x83,0xF9,0x10, 0x74,0xFFFF, 0xEB,0xFFFF };
        static const unsigned short q[] = { 0x6A,0x08, 0x68,0xC8,0x00,0x00,0x00, 0x68,0x40,0x01,0x00,0x00, 0xEB };
        DWORD c;
        if (!(lo = fn_range("@GG_InitMode@0", &hi)) || !(initSite = find(lo, hi, p, 7)) || !(c = find(lo, hi, q, 13))) {
            logf("wide flight: GG_InitMode not as expected; off"); return;
        }
        initJe = initSite + 5 + (signed char)((BYTE *)initSite)[4];
        initDef = initSite + 7 + (signed char)((BYTE *)initSite)[6];
        initCont = c + 14 + (signed char)((BYTE *)c)[13];
    }
    {
        /* 4. HUDInit: cmp word [w],280h (x2); mov esi,280h ... cmp [w],si; mov ebx,280h ... cmp [w],bx */
        unsigned short pc[9], ps[5], pb[5];
        DWORD w = gr + 0xA;
        pc[0] = 0x66; pc[1] = 0x81; pc[2] = 0x3D; pc[3] = w & 0xFF; pc[4] = (w >> 8) & 0xFF; pc[5] = (w >> 16) & 0xFF; pc[6] = w >> 24; pc[7] = 0x80; pc[8] = 0x02;
        ps[0] = 0xBE; ps[1] = 0x80; ps[2] = 0x02; ps[3] = 0; ps[4] = 0;
        pb[0] = 0xBB; pb[1] = 0x80; pb[2] = 0x02; pb[3] = 0; pb[4] = 0;
        if (!(lo = fn_range("_HUDInit@0", &hi))) { logf("wide flight: HUDInit not found; off"); return; }
        hud[0] = find(lo, hi, pc, 9);
        hud[1] = hud[0] ? find(hud[0] + 9, hi, pc, 9) : 0;
        hud[2] = find(lo, hi, ps, 5);
        hud[3] = find(lo, hi, pb, 5);
        if (!hud[0] || !hud[1] || !hud[2] || !hud[3] ||
            *(WORD *)(hud[2] + 10) != 0x3966 || *(DWORD *)(hud[2] + 13) != w ||   /* mov edi,1E0h; cmp [w],si */
            *(WORD *)(hud[3] + 5) != 0x3966 || *(DWORD *)(hud[3] + 8) != w) {     /* cmp [w],bx */
            logf("wide flight: HUDInit 640x480 checks not as expected; off"); return;
        }
    }
    {
        /* 5. render_3d: mov bx,[scrh]; mov cx,[scrw]; mov ax,[aspect]   and
         *    mov cx,[aspectx]; cmp cx,7FFFh  (start of the matrix scaling) */
        unsigned short p[14], q[11];
        p[0] = 0x66; p[1] = 0x8B; p[2] = 0x1D; p[3] = scrh & 0xFF; p[4] = (scrh >> 8) & 0xFF; p[5] = (scrh >> 16) & 0xFF; p[6] = scrh >> 24;
        p[7] = 0x66; p[8] = 0x8B; p[9] = 0x0D; p[10] = scrw & 0xFF; p[11] = (scrw >> 8) & 0xFF; p[12] = (scrw >> 16) & 0xFF; p[13] = scrw >> 24;
        q[0] = 0x66; q[1] = 0x8B; q[2] = 0x0D; q[3] = ax & 0xFF; q[4] = (ax >> 8) & 0xFF; q[5] = (ax >> 16) & 0xFF; q[6] = ax >> 24;
        q[7] = 0x66; q[8] = 0x81; q[9] = 0xF9; q[10] = 0xFF;
        if (!(lo = fn_range("render_3d", &hi)) || !(rSite1 = find(lo, hi, p, 14)) || !(rSite2 = find(rSite1, hi, q, 11))) {
            logf("wide flight: render_3d projection setup not as expected; off"); return;
        }
        rSite1 += 7;
    }

    {
        /* 6. G_SUPolygon span loop: rol eax,10h; dec cl; jne back  (after mov cx,[len]; sar cx,2) */
        static const unsigned short p[] = { 0xC1,0xC0,0x10, 0xFE,0xC9, 0x75 };
        static const unsigned short q[] = { 0x66,0xC1,0xF9,0x02 };
        DWORD sq;
        if (!(lo = fn_range("@G_SUPolygon@8", &hi)) || !(spanSite = find(lo, hi, p, 6)) ||
            find(spanSite + 1, hi, p, 6) || !(sq = find(lo, spanSite, q, 4)) || spanSite - sq > 0x40) {
            logf("wide flight: G_SUPolygon span loop not as expected; off"); return;
        }
        spanSite += 3;
    }

    {
        /* 7. affine texture spans: shr eax,2; mov cl,al; sub edi,4 ... rol eax,10h; dec cl; mov [edi],eax; jne */
        static const unsigned short p[] = { 0xC1,0xE8,0x02, 0x8A,0xC8, 0x83,0xEF,0x04 };
        static const unsigned short q[] = { 0xC1,0xC0,0x10, 0xFE,0xC9, 0x89,0x07, 0x75 };
        DWORD l2 = patches_symbol("AC_interpolate_linear_span"), h2 = patches_symbol("interpolate_perspective_span"), c;
        int i;
        if (!l2 || !h2 || h2 <= l2 || h2 - l2 > 0x4000) { logf("wide flight: texture span loops not found; off"); return; }
        for (c = l2, i = 0; i < 3; i++) {
            if (!(linStart[i] = find(c, h2, p, 8)) || !(linDec[i] = find(linStart[i], h2, q, 8)) || linDec[i] - linStart[i] > 0x80) {
                logf("wide flight: texture span loop %d not as expected; off", i); return;
            }
            linDec[i] += 3;
            c = linDec[i];
        }
        if (find(c, h2, p, 8)) { logf("wide flight: more texture span loops than expected; off"); return; }
    }

    /* ---- build stubs ---- */
    wsprintfA(s_modeName, "%dx480x256", W);

    BYTE *stubMode = s = code_alloc(16);            /* returns flight mode, 2 -> 0x20 */
    s[0] = 0xA1; put32(s + 1, fm);
    s[5] = 0x83; s[6] = 0xF8; s[7] = 0x02;
    s[8] = 0x75; s[9] = 0x05;
    s[10] = 0xB8; put32(s + 11, WIDE_MODE);
    s[15] = 0xC3;

    BYTE *stubGet = s = code_alloc(9 + 6 * 9 + 10 + 1);   /* GG_GetMode, unknown mode */
    {
        DWORD jaTarget = getJa + 6 + *(LONG *)(getJa + 2);
        static const WORD off[6] = { 4, 0xA, 0xC, 6, 8, 0xE };
        WORD val[6];
        int i, k = 0;
        val[0] = WIDE_MODE; val[1] = (WORD)W; val[2] = 480; val[3] = 0x100; val[4] = 0x7FFF; val[5] = 0xFFFF;
        s[k++] = 0x83; s[k++] = 0xF8; s[k++] = WIDE_MODE - 1;          /* cmp eax, 1Fh (mode-1) */
        s[k++] = 0x0F; s[k++] = 0x85; put32(s + k, rel32(s + k, jaTarget)); k += 4;
        for (i = 0; i < 6; i++) {                                         /* mov word [gr+off], val */
            s[k++] = 0x66; s[k++] = 0xC7; s[k++] = 0x05; put32(s + k, gr + off[i]); k += 4; put16(s + k, val[i]); k += 2;
        }
        s[k++] = 0xC7; s[k++] = 0x05; put32(s + k, gr); k += 4; put32(s + k, (DWORD)s_modeName); k += 4;
        s[k++] = 0xC3;
    }

    BYTE *stubInit = s = code_alloc(3 + 6 + 3 + 6 + 2 + 5 + 5 + 5);   /* GG_InitMode */
    {
        int k = 0;
        s[k++] = 0x83; s[k++] = 0xF9; s[k++] = 0x10;                               /* cmp ecx,10h */
        s[k++] = 0x0F; s[k++] = 0x84; put32(s + k, rel32(s + k, initJe)); k += 4;   /* je 1600x1200 */
        s[k++] = 0x83; s[k++] = 0xF9; s[k++] = WIDE_MODE;                           /* cmp ecx,20h */
        s[k++] = 0x0F; s[k++] = 0x85; put32(s + k, rel32(s + k, initDef)); k += 4;  /* jne default */
        s[k++] = 0x6A; s[k++] = 0x08;                                               /* push 8 */
        s[k++] = 0x68; put32(s + k, 480); k += 4;                                   /* push 480 */
        s[k++] = 0x68; put32(s + k, (DWORD)W); k += 4;                              /* push W */
        s[k++] = 0xE9; put32(s + k, rel32(s + k, initCont)); k += 4;               /* jmp SetDisplayMode */
    }

    BYTE *stubProjW = s = code_alloc(2 + 7 + 3 + 2 + 5 + 2 + 7 + 2 + 2 + 2 + 6 + 3);
    {
        int k = 0;
        s[k++] = 0x50; s[k++] = 0x52;                                               /* push eax; push edx */
        s[k++] = 0x0F; s[k++] = 0xB7; s[k++] = 0x05; put32(s + k, scrh); k += 4;   /* movzx eax,[scrh] */
        s[k++] = 0xC1; s[k++] = 0xE0; s[k++] = 0x02;                               /* shl eax,2 */
        s[k++] = 0x33; s[k++] = 0xD2;                                               /* xor edx,edx */
        s[k++] = 0xB9; put32(s + k, 3); k += 4;                                     /* mov ecx,3 */
        s[k++] = 0xF7; s[k++] = 0xF1;                                               /* div ecx */
        s[k++] = 0x0F; s[k++] = 0xB7; s[k++] = 0x0D; put32(s + k, scrw); k += 4;   /* movzx ecx,[scrw] */
        s[k++] = 0x3B; s[k++] = 0xC8;                                               /* cmp ecx,eax */
        s[k++] = 0x76; s[k++] = 0x02;                                               /* jbe +2 */
        s[k++] = 0x8B; s[k++] = 0xC8;                                               /* mov ecx,eax */
        s[k++] = 0x89; s[k++] = 0x0D; put32(s + k, (DWORD)&s_projW); k += 4;      /* mov [projW],ecx */
        s[k++] = 0x5A; s[k++] = 0x58; s[k++] = 0xC3;                               /* pop edx; pop eax; ret */
    }

    BYTE *stubAx = s = code_alloc(2 + 7 + 6 + 3 + 7 + 2 + 2 + 1 + 2 + 6 + 7 + 3);
    {
        int k = 0;
        s[k++] = 0x50; s[k++] = 0x52;                                               /* push eax; push edx */
        s[k++] = 0x0F; s[k++] = 0xBF; s[k++] = 0x05; put32(s + k, ax); k += 4;     /* movsx eax,[aspectx] */
        s[k++] = 0x8B; s[k++] = 0x0D; put32(s + k, (DWORD)&s_projW); k += 4;      /* mov ecx,[projW] */
        s[k++] = 0x0F; s[k++] = 0xAF; s[k++] = 0xC1;                               /* imul eax,ecx */
        s[k++] = 0x0F; s[k++] = 0xB7; s[k++] = 0x0D; put32(s + k, scrw); k += 4;   /* movzx ecx,[scrw] */
        s[k++] = 0x85; s[k++] = 0xC9;                                               /* test ecx,ecx */
        s[k++] = 0x74; s[k++] = 0x09;                                               /* jz +9 */
        s[k++] = 0x99;                                                              /* cdq */
        s[k++] = 0xF7; s[k++] = 0xF9;                                               /* idiv ecx */
        s[k++] = 0x66; s[k++] = 0xA3; put32(s + k, ax); k += 4;                     /* mov [aspectx],ax */
        s[k++] = 0x66; s[k++] = 0x8B; s[k++] = 0x0D; put32(s + k, ax); k += 4;     /* mov cx,[aspectx] */
        s[k++] = 0x5A; s[k++] = 0x58; s[k++] = 0xC3;                               /* pop edx; pop eax; ret */
    }
    BYTE *stubLin = s = code_alloc(3 + 3 + 3 + 1);
    if (s) {
        s[0] = 0xC1; s[1] = 0xE8; s[2] = 0x02;          /* shr eax,2 */
        s[3] = 0x66; s[4] = 0x8B; s[5] = 0xC8;          /* mov cx,ax */
        s[6] = 0x83; s[7] = 0xEF; s[8] = 0x04;          /* sub edi,4 */
        s[9] = 0xC3;
    }
    if (!stubLin || !stubMode || !stubGet || !stubInit || !stubProjW || !stubAx) { logf("wide flight: stub buffer too small; off"); return; }
    FlushInstructionCache(GetCurrentProcess(), s_code, sizeof s_code);

    /* ---- apply ---- */
    n = 0;
    for (a = 0; a < 3; a++) {
        BYTE b[5];
        b[0] = 0xE8; put32(b + 1, (DWORD)stubMode - (um[a] + 5));
        n += write_code((void *)um[a], b, 5);
    }
    {
        LONG r = (LONG)((DWORD)stubGet - (getJa + 6));
        n += write_code((void *)(getJa + 2), &r, 4);
    }
    {
        BYTE b[7];
        b[0] = 0xE9; put32(b + 1, (DWORD)stubInit - (initSite + 5)); b[5] = b[6] = 0x90;
        n += write_code((void *)initSite, b, 7);
    }
    {
        WORD w16 = (WORD)W;
        DWORD w32 = (DWORD)W;
        n += write_code((void *)(hud[0] + 7), &w16, 2);
        n += write_code((void *)(hud[1] + 7), &w16, 2);
        n += write_code((void *)(hud[2] + 1), &w32, 4);
        n += write_code((void *)(hud[3] + 1), &w32, 4);
    }
    {
        static const BYTE decCx[2] = { 0x66, 0x49 };   /* dec cx */
        n += write_code((void *)spanSite, decCx, 2);
    }
    for (a = 0; a < 3; a++) {
        static const BYTE decCx[2] = { 0x66, 0x49 };
        BYTE b[8];
        b[0] = 0xE8; put32(b + 1, (DWORD)stubLin - (linStart[a] + 5)); b[5] = b[6] = b[7] = 0x90;
        n += write_code((void *)linStart[a], b, 8);
        n += write_code((void *)linDec[a], decCx, 2);
    }
    n += call7(rSite1, stubProjW);
    n += call7(rSite2, stubAx);
    (void)gmHi;
    logf("wide flight: %d x 480 for flight at the 640x480 setting (%d/18 patches; usnfmain %08lX/%08lX/%08lX, "
         "GetMode %08lX, InitMode %08lX, HUDInit %08lX, render_3d %08lX/%08lX, spans %08lX)",
         W, n, um[0], um[1], um[2], getJa, initSite, hud[0], rSite1, rSite2, spanSite);
}
