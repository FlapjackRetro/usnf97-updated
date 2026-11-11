/* usnf97fix: software DirectDraw (IDirectDraw/2, IDirectDrawSurface/2, IDirectDrawPalette).
 * Everything is 8-bit palettized system memory; the presenter (present.c) puts the
 * front buffer on screen. Covers what USNF '97 calls, plus sane answers for the rest. */
#include "shim.h"

CRITICAL_SECTION g_ddLock;

int dd_lock(void)
{
    DWORD start;
    if (TryEnterCriticalSection(&g_ddLock)) return 1;
    start = GetTickCount();
    while (GetTickCount() - start < 500) {
        Sleep(1);
        if (TryEnterCriticalSection(&g_ddLock)) return 1;
    }
    { static int once; if (!once) { once = 1; logf("dd_lock: lock held >500 ms (game thread suspended?), continuing unlocked"); } }
    return 0;
}
void dd_unlock(int held) { if (held) LeaveCriticalSection(&g_ddLock); }

static const GUID IID_IUnknown_          = {0x00000000,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
static const GUID IID_IDirectDraw_       = {0x6C14DB80,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
static const GUID IID_IDirectDraw2_      = {0xB3A6F3E0,0x2B43,0x11CF,{0xA2,0xDE,0x00,0xAA,0x00,0xB9,0x33,0x56}};
static const GUID IID_IDirectDrawSurface_  = {0x6C14DB81,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};
static const GUID IID_IDirectDrawSurface2_ = {0x57805885,0x6EEC,0x11CF,{0x94,0x41,0xA8,0x23,0x03,0xC1,0x0E,0x27}};
static const GUID IID_IDirectDrawPalette_  = {0x6C14DB84,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}};

static int guid_eq(const GUID *a, const GUID *b) { return a && memcmp(a, b, sizeof(GUID)) == 0; }

static void log_guid(const char *what, const GUID *g)
{
    if (!g) { logf("%s: NULL IID", what); return; }
    logf("%s: unsupported IID %08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X", what, g->Data1, g->Data2, g->Data3,
         g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

#define UNSUPPORTED(name) do { static int once; if (!once) { once = 1; logf("unsupported call: %s", name); } } while (0)

/* ======================================================================== */
/*                                 PALETTE                                  */
/* ======================================================================== */
static void *s_palVtbl[7];

void palette_addref(DDPalette *p) { p->ref++; }
ULONG palette_release(DDPalette *p)
{
    ULONG r = (ULONG)--p->ref;
    if (!r) mem_free(p);
    return r;
}

static HRESULT WINAPI pal_QueryInterface(DDPalette *p, const GUID *iid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (guid_eq(iid, &IID_IUnknown_) || guid_eq(iid, &IID_IDirectDrawPalette_)) { palette_addref(p); *out = p; return DD_OK; }
    log_guid("Palette::QueryInterface", iid);
    *out = NULL; return E_NOINTERFACE;
}
static ULONG   WINAPI pal_AddRef(DDPalette *p) { palette_addref(p); return (ULONG)p->ref; }
static ULONG   WINAPI pal_Release(DDPalette *p) { return palette_release(p); }
static HRESULT WINAPI pal_GetCaps(DDPalette *p, DWORD *caps) { if (!caps) return E_INVALIDARG; *caps = p->caps; return DD_OK; }
static HRESULT WINAPI pal_GetEntries(DDPalette *p, DWORD flags, DWORD base, DWORD n, PALETTEENTRY *e)
{
    (void)flags;
    if (!e || base > 256 || n > 256 - base) return E_INVALIDARG;
    LOCK_DD();
    memcpy(e, &p->entries[base], n * sizeof *e);
    UNLOCK_DD();
    return DD_OK;
}
static HRESULT WINAPI pal_Initialize(DDPalette *p, void *dd, DWORD f, PALETTEENTRY *e) { (void)p; (void)dd; (void)f; (void)e; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI pal_SetEntries(DDPalette *p, DWORD flags, DWORD base, DWORD n, const PALETTEENTRY *e)
{
    int visible;
    (void)flags;
    if (!e || base > 256 || n > 256 - base) return E_INVALIDARG;
    LOCK_DD();
    memcpy(&p->entries[base], e, n * sizeof *e);
    visible = 0;
    {   /* is this the palette on screen? */
        DDSurface *pr = present_primary();
        visible = pr && pr->pal == p;
    }
    UNLOCK_DD();
    if (visible) present_mark_dirty(1);
    return DD_OK;
}

HRESULT palette_create(DWORD flags, const PALETTEENTRY *init, DDPalette **out)
{
    DDPalette *p = mem_alloc(sizeof *p);
    if (!p) return DDERR_OUTOFMEMORY;
    p->vtbl = s_palVtbl;
    p->ref = 1;
    p->caps = flags;
    if (init) memcpy(p->entries, init, sizeof p->entries);
    *out = p;
    return DD_OK;
}

/* ======================================================================== */
/*                                 SURFACE                                  */
/* ======================================================================== */
static void *s_surfVtbl[39];

void surface_addref(DDSurface *s) { s->ref++; }

static void surface_destroy(DDSurface *s)
{
    if (s->caps & DDSCAPS_PRIMARYSURFACE) present_release_primary(s);
    LOCK_DD();
    if (s->dd && s->dd->primary == s) s->dd->primary = NULL;
    UNLOCK_DD();
    if (s->pal) palette_release(s->pal);
    if (s->back) { s->back->front = NULL; surface_release(s->back); }
    mem_free(s->mem);
    mem_free(s);
}

ULONG surface_release(DDSurface *s)
{
    ULONG r = (ULONG)--s->ref;
    if (!r) surface_destroy(s);
    return r;
}

static DDSurface *alloc_surface(DDObject *dd, int w, int h, DWORD caps)
{
    DDSurface *s = mem_alloc(sizeof *s);
    if (!s) return NULL;
    s->vtbl  = s_surfVtbl;
    s->ref   = 1;
    s->dd    = dd;
    s->caps  = caps;
    s->w     = w;
    s->h     = h;
    s->pitch = (w + 3) & ~3;
    s->mem   = mem_alloc((unsigned)(s->pitch * h));
    if (!s->mem) { mem_free(s); return NULL; }
    return s;
}

static void fill_desc(const DDSurface *s, DDSURFACEDESC *d, BYTE *mem)
{
    DWORD size = d->dwSize >= sizeof *d ? sizeof *d : d->dwSize;
    memset((BYTE *)d + 4, 0, size > 4 ? size - 4 : 0);
    d->dwSize   = size ? size : sizeof *d;
    d->dwFlags  = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_LPSURFACE;
    d->dwHeight = (DWORD)s->h;
    d->dwWidth  = (DWORD)s->w;
    d->lPitch   = s->pitch;
    d->lpSurface = mem;
    if (s->back) { d->dwFlags |= DDSD_BACKBUFFERCOUNT; d->dwBackBufferCount = 1; }
    d->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    d->ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
    d->ddpfPixelFormat.dwRGBBitCount = 8;
    d->ddsCaps.dwCaps = s->caps;
}

HRESULT surface_create(DDObject *dd, const DDSURFACEDESC *desc, DDSurface **out)
{
    DWORD caps = (desc->dwFlags & DDSD_CAPS) ? desc->ddsCaps.dwCaps : DDSCAPS_OFFSCREENPLAIN;
    DDSurface *s;

    if (desc->dwFlags & DDSD_PIXELFORMAT) {
        if (desc->ddpfPixelFormat.dwRGBBitCount != 8 && desc->ddpfPixelFormat.dwRGBBitCount != 0) {
            logf("CreateSurface: %lu-bit surface requested; only 8-bit is emulated", desc->ddpfPixelFormat.dwRGBBitCount);
            return DDERR_INVALIDPARAMS;
        }
    }

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        int w = dd->modeW ? dd->modeW : 640, h = dd->modeH ? dd->modeH : 480;
        DWORD bb = (desc->dwFlags & DDSD_BACKBUFFERCOUNT) ? desc->dwBackBufferCount : 0;
        caps |= DDSCAPS_VISIBLE | DDSCAPS_FRONTBUFFER | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
        caps &= ~DDSCAPS_SYSTEMMEMORY;
        s = alloc_surface(dd, w, h, caps);
        if (!s) return DDERR_OUTOFMEMORY;
        if (bb) {
            DDSurface *b;
            if (bb > 1) logf("CreateSurface: %lu back buffers requested, emulating 1", bb);
            b = alloc_surface(dd, w, h, (caps & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER | DDSCAPS_VISIBLE)) | DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_COMPLEX);
            if (!b) { surface_release(s); return DDERR_OUTOFMEMORY; }
            b->front = s;
            s->back = b;
        }
        LOCK_DD();
        dd->primary = s;
        UNLOCK_DD();
        present_set_primary(s);
        logf("CreateSurface: primary %dx%d, %lu back buffer(s)", w, h, bb);
    } else {
        if (!(desc->dwFlags & DDSD_WIDTH) || !(desc->dwFlags & DDSD_HEIGHT) ||
            !desc->dwWidth || !desc->dwHeight || desc->dwWidth > 8192 || desc->dwHeight > 8192)
            return DDERR_INVALIDPARAMS;
        caps &= ~DDSCAPS_VIDEOMEMORY;
        caps |= DDSCAPS_SYSTEMMEMORY;
        s = alloc_surface(dd, (int)desc->dwWidth, (int)desc->dwHeight, caps);
        if (!s) return DDERR_OUTOFMEMORY;
        logf("CreateSurface: offscreen %lux%lu caps=%08lX", desc->dwWidth, desc->dwHeight, caps);
    }
    *out = s;
    return DD_OK;
}

/* --- pixel ops (call with g_ddLock held) --- */
static int clip_rect(RECT *r, int w, int h)
{
    if (r->left < 0) r->left = 0;
    if (r->top < 0) r->top = 0;
    if (r->right > w) r->right = w;
    if (r->bottom > h) r->bottom = h;
    return r->right > r->left && r->bottom > r->top;
}

static void fill8(DDSurface *d, RECT r, BYTE c)
{
    int y;
    if (!clip_rect(&r, d->w, d->h)) return;
    for (y = r.top; y < r.bottom; y++)
        memset(d->mem + y * d->pitch + r.left, c, (unsigned)(r.right - r.left));
}

static void blit8(DDSurface *d, RECT dr, DDSurface *s, RECT sr, int keyed, BYTE key)
{
    int dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    int x, y;
    BYTE *tmp = NULL;
    const BYTE *sbase; int spitch;

    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    if (dw == sw && dh == sh) {
        /* 1:1 - clip dst and shift src to match, then clip src and shift dst */
        RECT c = dr;
        if (!clip_rect(&c, d->w, d->h)) return;
        sr.left += c.left - dr.left; sr.top += c.top - dr.top;
        sr.right = sr.left + (c.right - c.left); sr.bottom = sr.top + (c.bottom - c.top);
        dr = c;
        c = sr;
        if (!clip_rect(&c, s->w, s->h)) return;
        dr.left += c.left - sr.left; dr.top += c.top - sr.top;
        dr.right = dr.left + (c.right - c.left); dr.bottom = dr.top + (c.bottom - c.top);
        sr = c;
        dw = dr.right - dr.left; dh = dr.bottom - dr.top;
        if (!keyed) {
            if (d->mem == s->mem && dr.top > sr.top) {       /* overlapping, copy bottom-up */
                for (y = dh - 1; y >= 0; y--)
                    memmove(d->mem + (dr.top + y) * d->pitch + dr.left, s->mem + (sr.top + y) * s->pitch + sr.left, (unsigned)dw);
            } else {
                for (y = 0; y < dh; y++)
                    memmove(d->mem + (dr.top + y) * d->pitch + dr.left, s->mem + (sr.top + y) * s->pitch + sr.left, (unsigned)dw);
            }
            return;
        }
        sbase = s->mem; spitch = s->pitch;
        if (d->mem == s->mem) {
            tmp = mem_alloc((unsigned)(s->pitch * s->h));
            if (!tmp) return;
            memcpy(tmp, s->mem, (unsigned)(s->pitch * s->h));
            sbase = tmp;
        }
        for (y = 0; y < dh; y++) {
            BYTE *dp = d->mem + (dr.top + y) * d->pitch + dr.left;
            const BYTE *sp = sbase + (sr.top + y) * spitch + sr.left;
            for (x = 0; x < dw; x++) if (sp[x] != key) dp[x] = sp[x];
        }
        mem_free(tmp);
        return;
    }

    /* stretch (nearest); clip src to the surface first, then walk the clipped dst */
    {
        RECT c = sr;
        if (!clip_rect(&c, s->w, s->h)) return;
        sbase = s->mem; spitch = s->pitch;
        if (d->mem == s->mem) {
            tmp = mem_alloc((unsigned)(s->pitch * s->h));
            if (!tmp) return;
            memcpy(tmp, s->mem, (unsigned)(s->pitch * s->h));
            sbase = tmp;
        }
        for (y = dr.top; y < dr.bottom; y++) {
            int syy;
            BYTE *dp;
            if (y < 0 || y >= d->h) continue;
            syy = sr.top + (int)((unsigned)(y - dr.top) * (unsigned)sh / (unsigned)dh);
            if (syy < c.top || syy >= c.bottom) continue;
            dp = d->mem + y * d->pitch;
            for (x = dr.left; x < dr.right; x++) {
                int sxx;
                BYTE v;
                if (x < 0 || x >= d->w) continue;
                sxx = sr.left + (int)((unsigned)(x - dr.left) * (unsigned)sw / (unsigned)dw);
                if (sxx < c.left || sxx >= c.right) continue;
                v = sbase[syy * spitch + sxx];
                if (!keyed || v != key) dp[x] = v;
            }
        }
        mem_free(tmp);
    }
}

static void after_write(DDSurface *d)
{
    if (present_is_primary_mem(d)) present_mark_dirty(0);
}

static HRESULT WINAPI s_QueryInterface(DDSurface *s, const GUID *iid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (guid_eq(iid, &IID_IUnknown_) || guid_eq(iid, &IID_IDirectDrawSurface_) || guid_eq(iid, &IID_IDirectDrawSurface2_)) {
        surface_addref(s); *out = s; return DD_OK;
    }
    log_guid("Surface::QueryInterface", iid);
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI s_AddRef(DDSurface *s) { surface_addref(s); return (ULONG)s->ref; }
static ULONG WINAPI s_Release(DDSurface *s) { return surface_release(s); }
static HRESULT WINAPI s_AddAttachedSurface(DDSurface *s, DDSurface *a) { (void)s; (void)a; UNSUPPORTED("Surface::AddAttachedSurface"); return DDERR_UNSUPPORTED; }
static HRESULT WINAPI s_AddOverlayDirtyRect(DDSurface *s, RECT *r) { (void)s; (void)r; return DDERR_UNSUPPORTED; }

static HRESULT WINAPI s_Blt(DDSurface *d, RECT *dstRect, DDSurface *src, RECT *srcRect, DWORD flags, DDBLTFX *fx)
{
    RECT dr, sr;
    if (dstRect) dr = *dstRect; else { dr.left = dr.top = 0; dr.right = d->w; dr.bottom = d->h; }
    LOCK_DD();
    if (flags & DDBLT_COLORFILL) {
        if (!fx) { UNLOCK_DD(); return E_INVALIDARG; }
        fill8(d, dr, (BYTE)fx->dwFillColor);
    } else if (src) {
        int keyed = 0; BYTE key = 0;
        if (srcRect) sr = *srcRect; else { sr.left = sr.top = 0; sr.right = src->w; sr.bottom = src->h; }
        if ((flags & DDBLT_KEYSRCOVERRIDE) && fx) { keyed = 1; key = (BYTE)fx->ddckSrcColorkey.dwColorSpaceLowValue; }
        else if ((flags & DDBLT_KEYSRC) && src->hasSrcKey) { keyed = 1; key = (BYTE)src->srcKey.dwColorSpaceLowValue; }
        blit8(d, dr, src, sr, keyed, key);
    } else {
        UNLOCK_DD();
        UNSUPPORTED("Surface::Blt (no source, flags)");
        return DDERR_INVALIDPARAMS;
    }
    UNLOCK_DD();
    after_write(d);
    return DD_OK;
}
static HRESULT WINAPI s_BltBatch(DDSurface *s, void *b, DWORD n, DWORD f) { (void)s; (void)b; (void)n; (void)f; UNSUPPORTED("Surface::BltBatch"); return DDERR_UNSUPPORTED; }
static HRESULT WINAPI s_BltFast(DDSurface *d, DWORD x, DWORD y, DDSurface *src, RECT *srcRect, DWORD trans)
{
    RECT dr, sr;
    if (!src) return E_INVALIDARG;
    if (srcRect) sr = *srcRect; else { sr.left = sr.top = 0; sr.right = src->w; sr.bottom = src->h; }
    dr.left = (LONG)x; dr.top = (LONG)y; dr.right = dr.left + (sr.right - sr.left); dr.bottom = dr.top + (sr.bottom - sr.top);
    LOCK_DD();
    blit8(d, dr, src, sr, (trans & DDBLTFAST_SRCCOLORKEY) && src->hasSrcKey, (BYTE)src->srcKey.dwColorSpaceLowValue);
    UNLOCK_DD();
    after_write(d);
    return DD_OK;
}
static HRESULT WINAPI s_DeleteAttachedSurface(DDSurface *s, DWORD f, DDSurface *a) { (void)s; (void)f; (void)a; UNSUPPORTED("Surface::DeleteAttachedSurface"); return DDERR_UNSUPPORTED; }
static HRESULT WINAPI s_EnumAttachedSurfaces(DDSurface *s, void *ctx, LPDDENUMSURFACESCALLBACK cb)
{
    DDSURFACEDESC d;
    if (!cb) return E_INVALIDARG;
    if (s->back) {
        d.dwSize = sizeof d;
        fill_desc(s->back, &d, s->back->mem);
        surface_addref(s->back);
        cb(s->back, &d, ctx);
    }
    return DD_OK;
}
static HRESULT WINAPI s_EnumOverlayZOrders(DDSurface *s, DWORD f, void *c, void *cb) { (void)s; (void)f; (void)c; (void)cb; return DDERR_UNSUPPORTED; }

static HRESULT WINAPI s_Flip(DDSurface *s, DDSurface *target, DWORD flags)
{
    BYTE *t;
    (void)target; (void)flags;
    if (!s->back) return DDERR_NOTFLIPPABLE;
    LOCK_DD();
    t = s->mem; s->mem = s->back->mem; s->back->mem = t;
    UNLOCK_DD();
    present_mark_dirty(1);
    return DD_OK;
}
static HRESULT WINAPI s_GetAttachedSurface(DDSurface *s, DDSCAPS *caps, DDSurface **out)
{
    if (!caps || !out) return E_INVALIDARG;
    if (s->back && (caps->dwCaps & DDSCAPS_BACKBUFFER)) { surface_addref(s->back); *out = s->back; return DD_OK; }
    *out = NULL;
    return DDERR_NOTFOUND;
}
static HRESULT WINAPI s_GetBltStatus(DDSurface *s, DWORD f) { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI s_GetCaps(DDSurface *s, DDSCAPS *c) { if (!c) return E_INVALIDARG; c->dwCaps = s->caps; return DD_OK; }
static HRESULT WINAPI s_GetClipper(DDSurface *s, void **c) { (void)s; if (c) *c = NULL; return DDERR_NOCLIPPERATTACHED; }
static HRESULT WINAPI s_GetColorKey(DDSurface *s, DWORD f, DDCOLORKEY *k)
{
    if (!k) return E_INVALIDARG;
    if ((f & DDCKEY_SRCBLT) && s->hasSrcKey) { *k = s->srcKey; return DD_OK; }
    return DDERR_NOCOLORKEY;
}
static HRESULT WINAPI s_GetDC(DDSurface *s, HDC *h) { (void)s; if (h) *h = NULL; UNSUPPORTED("Surface::GetDC"); return DDERR_UNSUPPORTED; }
static HRESULT WINAPI s_GetFlipStatus(DDSurface *s, DWORD f) { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI s_GetOverlayPosition(DDSurface *s, LONG *x, LONG *y) { (void)s; (void)x; (void)y; return DDERR_NOOVERLAYHW; }
static HRESULT WINAPI s_GetPalette(DDSurface *s, DDPalette **out)
{
    if (!out) return E_INVALIDARG;
    if (!s->pal) { *out = NULL; return DDERR_NOPALETTEATTACHED; }
    palette_addref(s->pal); *out = s->pal; return DD_OK;
}
static HRESULT WINAPI s_GetPixelFormat(DDSurface *s, DDPIXELFORMAT *pf)
{
    (void)s;
    if (!pf) return E_INVALIDARG;
    memset(pf, 0, sizeof *pf);
    pf->dwSize = sizeof *pf; pf->dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB; pf->dwRGBBitCount = 8;
    return DD_OK;
}
static HRESULT WINAPI s_GetSurfaceDesc(DDSurface *s, DDSURFACEDESC *d)
{
    if (!d) return E_INVALIDARG;
    fill_desc(s, d, NULL);
    d->dwFlags &= ~DDSD_LPSURFACE;
    return DD_OK;
}
static HRESULT WINAPI s_Initialize(DDSurface *s, void *dd, DDSURFACEDESC *d) { (void)s; (void)dd; (void)d; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI s_IsLost(DDSurface *s) { (void)s; return DD_OK; }
static HRESULT WINAPI s_Lock(DDSurface *s, RECT *r, DDSURFACEDESC *d, DWORD flags, HANDLE ev)
{
    BYTE *p;
    (void)flags; (void)ev;
    if (!d) return E_INVALIDARG;
    LOCK_DD();
    p = s->mem;
    if (r) {
        RECT c = *r;
        if (!clip_rect(&c, s->w, s->h)) { UNLOCK_DD(); return E_INVALIDARG; }
        p += c.top * s->pitch + c.left;
    }
    s->locks++;
    UNLOCK_DD();
    fill_desc(s, d, p);
    /* Locking another surface after drawing to the primary marks the end of GG_Flush. */
    if (!present_is_primary_mem(s)) present_mark_dirty(2);
    return DD_OK;
}
static HRESULT WINAPI s_ReleaseDC(DDSurface *s, HDC h) { (void)s; (void)h; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI s_Restore(DDSurface *s) { (void)s; return DD_OK; }
static HRESULT WINAPI s_SetClipper(DDSurface *s, void *c) { (void)s; (void)c; return DD_OK; }
static HRESULT WINAPI s_SetColorKey(DDSurface *s, DWORD f, DDCOLORKEY *k)
{
    if (f & DDCKEY_SRCBLT) {
        if (k) { s->srcKey = *k; s->hasSrcKey = 1; } else s->hasSrcKey = 0;
    }
    return DD_OK;
}
static HRESULT WINAPI s_SetOverlayPosition(DDSurface *s, LONG x, LONG y) { (void)s; (void)x; (void)y; return DDERR_NOOVERLAYHW; }
static HRESULT WINAPI s_SetPalette(DDSurface *s, DDPalette *p)
{
    DDPalette *old;
    LOCK_DD();
    old = s->pal;
    if (p) palette_addref(p);
    s->pal = p;
    if (s->back) {  /* a flip chain shares one palette */
        if (s->back->pal) palette_release(s->back->pal);
        if (p) palette_addref(p);
        s->back->pal = p;
    }
    UNLOCK_DD();
    if (old) palette_release(old);
    if (s->caps & DDSCAPS_PRIMARYSURFACE) present_mark_dirty(1);
    return DD_OK;
}
static HRESULT WINAPI s_Unlock(DDSurface *s, void *p)
{
    (void)p;
    if (s->locks <= 0) return DDERR_NOTLOCKED;
    s->locks--;
    after_write(s);
    return DD_OK;
}
static HRESULT WINAPI s_UpdateOverlay(DDSurface *s, RECT *a, DDSurface *d, RECT *b, DWORD f, void *fx) { (void)s; (void)a; (void)d; (void)b; (void)f; (void)fx; return DDERR_NOOVERLAYHW; }
static HRESULT WINAPI s_UpdateOverlayDisplay(DDSurface *s, DWORD f) { (void)s; (void)f; return DDERR_NOOVERLAYHW; }
static HRESULT WINAPI s_UpdateOverlayZOrder(DDSurface *s, DWORD f, DDSurface *r) { (void)s; (void)f; (void)r; return DDERR_NOOVERLAYHW; }
static HRESULT WINAPI s_GetDDInterface(DDSurface *s, void **out)
{
    if (!out) return E_INVALIDARG;
    s->dd->ref++;
    *out = s->dd;               /* IDirectDraw (v1) pointer, as the real runtime returns */
    return DD_OK;
}
static HRESULT WINAPI s_PageLock(DDSurface *s, DWORD f) { (void)s; (void)f; return DD_OK; }
static HRESULT WINAPI s_PageUnlock(DDSurface *s, DWORD f) { (void)s; (void)f; return DD_OK; }

/* ======================================================================== */
/*                          IDirectDraw / IDirectDraw2                      */
/* ======================================================================== */
static void *s_dd1Vtbl[23];
static void *s_dd2Vtbl[24];

static const struct { int w, h; } k_modes[] = { {320,200}, {640,480}, {800,600}, {1024,768}, {1600,1200} };

static HRESULT dd_QueryInterface(DDObject *dd, const GUID *iid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (guid_eq(iid, &IID_IUnknown_) || guid_eq(iid, &IID_IDirectDraw_)) { dd->ref++; *out = &dd->vtbl1; return DD_OK; }
    if (guid_eq(iid, &IID_IDirectDraw2_)) { dd->ref++; *out = &dd->vtbl2; return DD_OK; }
    log_guid("DirectDraw::QueryInterface", iid);
    *out = NULL; return E_NOINTERFACE;
}
static ULONG dd_AddRef(DDObject *dd) { return (ULONG)++dd->ref; }
static ULONG dd_Release(DDObject *dd)
{
    ULONG r = (ULONG)--dd->ref;
    /* Not freed: surfaces released after their DirectDraw object still point at it. */
    if (!r) logf("DirectDraw released");
    return r;
}
static HRESULT dd_Compact(DDObject *dd) { (void)dd; return DD_OK; }
static HRESULT dd_CreateClipper(DDObject *dd, DWORD f, void **c, void *o) { (void)dd; (void)f; (void)o; if (c) *c = NULL; UNSUPPORTED("DirectDraw::CreateClipper"); return DDERR_UNSUPPORTED; }
static HRESULT dd_CreatePalette(DDObject *dd, DWORD f, const PALETTEENTRY *e, DDPalette **out, void *o)
{
    (void)dd; (void)o;
    if (!out) return E_INVALIDARG;
    return palette_create(f, e, out);
}
static HRESULT dd_CreateSurface(DDObject *dd, const DDSURFACEDESC *d, DDSurface **out, void *o)
{
    (void)o;
    if (!d || !out) return E_INVALIDARG;
    return surface_create(dd, d, out);
}
static HRESULT dd_DuplicateSurface(DDObject *dd, DDSurface *s, DDSurface **o) { (void)dd; (void)s; (void)o; UNSUPPORTED("DirectDraw::DuplicateSurface"); return DDERR_UNSUPPORTED; }
static HRESULT dd_EnumDisplayModes(DDObject *dd, DWORD flags, DDSURFACEDESC *filter, void *ctx, LPDDENUMMODESCALLBACK cb)
{
    unsigned i;
    (void)dd; (void)flags; (void)filter;
    if (!cb) return E_INVALIDARG;
    for (i = 0; i < sizeof k_modes / sizeof k_modes[0]; i++) {
        DDSURFACEDESC d;
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        d.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
        d.dwWidth = (DWORD)k_modes[i].w; d.dwHeight = (DWORD)k_modes[i].h; d.lPitch = k_modes[i].w;
        d.dwRefreshRate = 60;
        d.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        d.ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
        d.ddpfPixelFormat.dwRGBBitCount = 8;
        if (cb(&d, ctx) == DDENUMRET_CANCEL) break;
    }
    return DD_OK;
}
static HRESULT dd_EnumSurfaces(DDObject *dd, DWORD f, DDSURFACEDESC *d, void *c, void *cb) { (void)dd; (void)f; (void)d; (void)c; (void)cb; UNSUPPORTED("DirectDraw::EnumSurfaces"); return DDERR_UNSUPPORTED; }
static HRESULT dd_FlipToGDISurface(DDObject *dd) { (void)dd; return DD_OK; }
static void fill_caps(BYTE *c)
{
    DWORD size;
    if (!c) return;
    size = *(DWORD *)c;
    if (size < 8 || size > 0x200) size = 0x13c;
    memset(c + 4, 0, size - 4);
    *(DWORD *)(c + 0x04) = DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_PALETTE | DDCAPS_COLORKEY | DDCAPS_BLTCOLORFILL;
    *(DWORD *)(c + 0x18) = DDPCAPS_8BIT | DDPCAPS_ALLOW256;
    if (size > 0x44) {
        *(DWORD *)(c + 0x3c) = 16u << 20;   /* dwVidMemTotal */
        *(DWORD *)(c + 0x40) = 12u << 20;   /* dwVidMemFree */
    }
}
static HRESULT dd_GetCaps(DDObject *dd, BYTE *drv, BYTE *hel) { (void)dd; fill_caps(drv); fill_caps(hel); return DD_OK; }
static HRESULT dd_GetDisplayMode(DDObject *dd, DDSURFACEDESC *d)
{
    if (!d) return E_INVALIDARG;
    memset(d, 0, sizeof *d);
    d->dwSize = sizeof *d;
    d->dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    d->dwWidth = (DWORD)(dd->modeW ? dd->modeW : 640); d->dwHeight = (DWORD)(dd->modeH ? dd->modeH : 480);
    d->lPitch = (LONG)d->dwWidth; d->dwRefreshRate = 60;
    d->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    d->ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
    d->ddpfPixelFormat.dwRGBBitCount = 8;
    return DD_OK;
}
static HRESULT dd_GetFourCCCodes(DDObject *dd, DWORD *n, DWORD *codes) { (void)dd; (void)codes; if (n) *n = 0; return DD_OK; }
static HRESULT dd_GetGDISurface(DDObject *dd, DDSurface **out)
{
    if (!out) return E_INVALIDARG;
    if (!dd->primary) { *out = NULL; return DDERR_NOTFOUND; }
    surface_addref(dd->primary); *out = dd->primary; return DD_OK;
}
static HRESULT dd_GetMonitorFrequency(DDObject *dd, DWORD *f) { (void)dd; if (f) *f = 60; return DD_OK; }
static HRESULT dd_GetScanLine(DDObject *dd, DWORD *l)
{
    static LONGLONG base;
    DWORD period = (DWORD)qpc_freq() / 60u, ph;
    int h = dd->modeH ? dd->modeH : 480;
    if (!l) return E_INVALIDARG;
    if (!base) base = qpc_now();
    if (!period) period = 1;
    ph = (DWORD)(qpc_now() - base) % period;           /* 32-bit math only (no CRT helpers) */
    *l = ph / (period / (DWORD)(h + 45) + 1u);
    return DD_OK;
}
static HRESULT dd_GetVerticalBlankStatus(DDObject *dd, BOOL *b)
{
    DWORD line = 0; int h = dd->modeH ? dd->modeH : 480;
    if (!b) return E_INVALIDARG;
    dd_GetScanLine(dd, &line);
    *b = line >= (DWORD)h;
    return DD_OK;
}
static HRESULT dd_Initialize(DDObject *dd, GUID *g) { (void)dd; (void)g; return DDERR_ALREADYINITIALIZED; }
static HRESULT dd_RestoreDisplayMode(DDObject *dd) { (void)dd; return DD_OK; }
static HRESULT dd_SetCooperativeLevel(DDObject *dd, HWND hwnd, DWORD flags)
{
    logf("SetCooperativeLevel(hwnd=%08lX, flags=%08lX)", (DWORD)hwnd, flags);
    dd->hwnd = hwnd;
    dd->coopFlags = flags;
    if (hwnd) present_set_window(hwnd);
    return DD_OK;
}
static HRESULT dd_SetDisplayMode(DDObject *dd, DWORD w, DWORD h, DWORD bpp)
{
    logf("SetDisplayMode(%lu x %lu x %lu)", w, h, bpp);
    if (bpp != 8 || !w || !h || w > 4096 || h > 4096) {
        logf("  -> refused: only 8-bit modes are emulated");
        return DDERR_INVALIDMODE;
    }
    dd->modeW = (int)w; dd->modeH = (int)h; dd->modeBpp = 8;
    present_set_mode((int)w, (int)h);
    return DD_OK;
}
static HRESULT dd_WaitForVerticalBlank(DDObject *dd, DWORD f, HANDLE e) { (void)dd; (void)f; (void)e; present_wait_vblank(); return DD_OK; }
static HRESULT dd_GetAvailableVidMem(DDObject *dd, DDSCAPS *c, DWORD *total, DWORD *freeMem)
{
    (void)dd; (void)c;
    if (total) *total = 16u << 20;
    if (freeMem) *freeMem = 12u << 20;
    return DD_OK;
}

/* thin per-interface entry points: v1 'this' is the object, v2 'this' is &obj->vtbl2 */

static HRESULT WINAPI v1_QueryInterface(void *t, const GUID *i, void **o) { return dd_QueryInterface(DD_FROM_V1(t), i, o); }
static HRESULT WINAPI v2_QueryInterface(void *t, const GUID *i, void **o) { return dd_QueryInterface(DD_FROM_V2(t), i, o); }
static ULONG   WINAPI v1_AddRef(void *t)  { return dd_AddRef(DD_FROM_V1(t)); }
static ULONG   WINAPI v2_AddRef(void *t)  { return dd_AddRef(DD_FROM_V2(t)); }
static ULONG   WINAPI v1_Release(void *t) { return dd_Release(DD_FROM_V1(t)); }
static ULONG   WINAPI v2_Release(void *t) { return dd_Release(DD_FROM_V2(t)); }
#define FWD0(n)                 static HRESULT WINAPI v1_##n(void *t) { return dd_##n(DD_FROM_V1(t)); } \
                                static HRESULT WINAPI v2_##n(void *t) { return dd_##n(DD_FROM_V2(t)); }
#define FWD1(n,A)               static HRESULT WINAPI v1_##n(void *t, A a) { return dd_##n(DD_FROM_V1(t), a); } \
                                static HRESULT WINAPI v2_##n(void *t, A a) { return dd_##n(DD_FROM_V2(t), a); }
#define FWD2(n,A,B)             static HRESULT WINAPI v1_##n(void *t, A a, B b) { return dd_##n(DD_FROM_V1(t), a, b); } \
                                static HRESULT WINAPI v2_##n(void *t, A a, B b) { return dd_##n(DD_FROM_V2(t), a, b); }
#define FWD3(n,A,B,C)           static HRESULT WINAPI v1_##n(void *t, A a, B b, C c) { return dd_##n(DD_FROM_V1(t), a, b, c); } \
                                static HRESULT WINAPI v2_##n(void *t, A a, B b, C c) { return dd_##n(DD_FROM_V2(t), a, b, c); }
#define FWD4(n,A,B,C,D)         static HRESULT WINAPI v1_##n(void *t, A a, B b, C c, D d) { return dd_##n(DD_FROM_V1(t), a, b, c, d); } \
                                static HRESULT WINAPI v2_##n(void *t, A a, B b, C c, D d) { return dd_##n(DD_FROM_V2(t), a, b, c, d); }
FWD0(Compact)
FWD3(CreateClipper, DWORD, void **, void *)
FWD4(CreatePalette, DWORD, const PALETTEENTRY *, DDPalette **, void *)
FWD3(CreateSurface, const DDSURFACEDESC *, DDSurface **, void *)
FWD2(DuplicateSurface, DDSurface *, DDSurface **)
FWD4(EnumDisplayModes, DWORD, DDSURFACEDESC *, void *, LPDDENUMMODESCALLBACK)
FWD4(EnumSurfaces, DWORD, DDSURFACEDESC *, void *, void *)
FWD0(FlipToGDISurface)
FWD2(GetCaps, BYTE *, BYTE *)
FWD1(GetDisplayMode, DDSURFACEDESC *)
FWD2(GetFourCCCodes, DWORD *, DWORD *)
FWD1(GetGDISurface, DDSurface **)
FWD1(GetMonitorFrequency, DWORD *)
FWD1(GetScanLine, DWORD *)
FWD1(GetVerticalBlankStatus, BOOL *)
FWD1(Initialize, GUID *)
FWD0(RestoreDisplayMode)
FWD2(SetCooperativeLevel, HWND, DWORD)
FWD2(WaitForVerticalBlank, DWORD, HANDLE)
static HRESULT WINAPI v1_SetDisplayMode(void *t, DWORD w, DWORD h, DWORD bpp) { return dd_SetDisplayMode(DD_FROM_V1(t), w, h, bpp); }
static HRESULT WINAPI v2_SetDisplayMode(void *t, DWORD w, DWORD h, DWORD bpp, DWORD rate, DWORD flags)
{ (void)rate; (void)flags; return dd_SetDisplayMode(DD_FROM_V2(t), w, h, bpp); }
static HRESULT WINAPI v2_GetAvailableVidMem(void *t, DDSCAPS *c, DWORD *a, DWORD *b) { return dd_GetAvailableVidMem(DD_FROM_V2(t), c, a, b); }

#define DDV(v) { (void *)v##_QueryInterface, (void *)v##_AddRef, (void *)v##_Release, (void *)v##_Compact, \
    (void *)v##_CreateClipper, (void *)v##_CreatePalette, (void *)v##_CreateSurface, (void *)v##_DuplicateSurface, \
    (void *)v##_EnumDisplayModes, (void *)v##_EnumSurfaces, (void *)v##_FlipToGDISurface, (void *)v##_GetCaps, \
    (void *)v##_GetDisplayMode, (void *)v##_GetFourCCCodes, (void *)v##_GetGDISurface, (void *)v##_GetMonitorFrequency, \
    (void *)v##_GetScanLine, (void *)v##_GetVerticalBlankStatus, (void *)v##_Initialize, (void *)v##_RestoreDisplayMode, \
    (void *)v##_SetCooperativeLevel, (void *)v##_SetDisplayMode, (void *)v##_WaitForVerticalBlank }

static void init_vtables(void)
{
    static int done;
    void *v1[23] = DDV(v1);
    void *v2[23] = DDV(v2);
    void *sv[39] = {
        (void *)s_QueryInterface, (void *)s_AddRef, (void *)s_Release, (void *)s_AddAttachedSurface,
        (void *)s_AddOverlayDirtyRect, (void *)s_Blt, (void *)s_BltBatch, (void *)s_BltFast,
        (void *)s_DeleteAttachedSurface, (void *)s_EnumAttachedSurfaces, (void *)s_EnumOverlayZOrders, (void *)s_Flip,
        (void *)s_GetAttachedSurface, (void *)s_GetBltStatus, (void *)s_GetCaps, (void *)s_GetClipper,
        (void *)s_GetColorKey, (void *)s_GetDC, (void *)s_GetFlipStatus, (void *)s_GetOverlayPosition,
        (void *)s_GetPalette, (void *)s_GetPixelFormat, (void *)s_GetSurfaceDesc, (void *)s_Initialize,
        (void *)s_IsLost, (void *)s_Lock, (void *)s_ReleaseDC, (void *)s_Restore,
        (void *)s_SetClipper, (void *)s_SetColorKey, (void *)s_SetOverlayPosition, (void *)s_SetPalette,
        (void *)s_Unlock, (void *)s_UpdateOverlay, (void *)s_UpdateOverlayDisplay, (void *)s_UpdateOverlayZOrder,
        (void *)s_GetDDInterface, (void *)s_PageLock, (void *)s_PageUnlock };
    void *pv[7] = { (void *)pal_QueryInterface, (void *)pal_AddRef, (void *)pal_Release, (void *)pal_GetCaps,
                    (void *)pal_GetEntries, (void *)pal_Initialize, (void *)pal_SetEntries };
    if (done) return;
    done = 1;
    memcpy(s_dd1Vtbl, v1, sizeof v1);
    memcpy(s_dd2Vtbl, v2, sizeof v2);
    s_dd2Vtbl[23] = (void *)v2_GetAvailableVidMem;
    memcpy(s_surfVtbl, sv, sizeof sv);
    memcpy(s_palVtbl, pv, sizeof pv);
}

HRESULT dd_create(void **out)
{
    DDObject *dd;
    init_vtables();
    dd = mem_alloc(sizeof *dd);
    if (!dd) return DDERR_OUTOFMEMORY;
    dd->vtbl1 = s_dd1Vtbl;
    dd->vtbl2 = s_dd2Vtbl;
    dd->ref = 1;
    *out = &dd->vtbl1;
    return DD_OK;
}
