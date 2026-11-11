/* Minimal DirectDraw (DirectX 3 era) declarations: only what the shim implements. */
#ifndef DDRAW_MIN_H
#define DDRAW_MIN_H
#include "win32_min.h"

#define DD_OK                 0
#define DDENUMRET_OK          1
#define DDENUMRET_CANCEL      0
#define E_NOINTERFACE         ((HRESULT)0x80004002L)
#define E_NOTIMPL             ((HRESULT)0x80004001L)
#define E_FAIL                ((HRESULT)0x80004005L)
#define E_INVALIDARG          ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY         ((HRESULT)0x8007000EL)
#define MAKE_DDHRESULT(c)     ((HRESULT)(0x88760000L | (c)))
#define DDERR_GENERIC         E_FAIL
#define DDERR_INVALIDPARAMS   E_INVALIDARG
#define DDERR_UNSUPPORTED     E_NOTIMPL
#define DDERR_OUTOFMEMORY     E_OUTOFMEMORY
#define DDERR_INVALIDMODE     MAKE_DDHRESULT(120)
#define DDERR_NOTFOUND        MAKE_DDHRESULT(255)
#define DDERR_NOTFLIPPABLE    MAKE_DDHRESULT(582)
#define DDERR_NOTLOCKED       MAKE_DDHRESULT(584)
#define DDERR_ALREADYINITIALIZED MAKE_DDHRESULT(5)
#define DDERR_NOCOLORKEY      MAKE_DDHRESULT(215)
#define DDERR_NOCLIPPERATTACHED MAKE_DDHRESULT(205)
#define DDERR_NOPALETTEATTACHED MAKE_DDHRESULT(209)
#define DDERR_NOOVERLAYHW     MAKE_DDHRESULT(230)

/* DDSURFACEDESC.dwFlags */
#define DDSD_CAPS             0x00000001
#define DDSD_HEIGHT           0x00000002
#define DDSD_WIDTH            0x00000004
#define DDSD_PITCH            0x00000008
#define DDSD_BACKBUFFERCOUNT  0x00000020
#define DDSD_LPSURFACE        0x00000800
#define DDSD_PIXELFORMAT      0x00001000
#define DDSD_REFRESHRATE      0x00040000

/* DDSCAPS */
#define DDSCAPS_BACKBUFFER     0x00000004
#define DDSCAPS_COMPLEX        0x00000008
#define DDSCAPS_FLIP           0x00000010
#define DDSCAPS_FRONTBUFFER    0x00000020
#define DDSCAPS_OFFSCREENPLAIN 0x00000040
#define DDSCAPS_PALETTE        0x00000100
#define DDSCAPS_PRIMARYSURFACE 0x00000200
#define DDSCAPS_SYSTEMMEMORY   0x00000800
#define DDSCAPS_VIDEOMEMORY    0x00004000
#define DDSCAPS_VISIBLE        0x00008000
#define DDSCAPS_LOCALVIDMEM    0x10000000

/* pixel format */
#define DDPF_PALETTEINDEXED8  0x00000020
#define DDPF_RGB              0x00000040

/* Blt */
#define DDBLT_COLORFILL       0x00000400
#define DDBLT_KEYSRC          0x00008000
#define DDBLT_KEYSRCOVERRIDE  0x00010000
#define DDBLT_WAIT            0x01000000
#define DDBLTFAST_SRCCOLORKEY 0x00000001
#define DDCKEY_SRCBLT         0x00000008

/* Caps */
#define DDCAPS_BLT            0x00000040
#define DDCAPS_BLTSTRETCH     0x00000200
#define DDCAPS_PALETTE        0x00008000
#define DDCAPS_COLORKEY       0x00400000
#define DDCAPS_BLTCOLORFILL   0x04000000
#define DDPCAPS_8BIT          0x00000004
#define DDPCAPS_ALLOW256      0x00000040

#define DDWAITVB_BLOCKBEGIN   1

typedef struct { DWORD dwColorSpaceLowValue, dwColorSpaceHighValue; } DDCOLORKEY;
typedef struct { DWORD dwCaps; } DDSCAPS;
typedef struct {
    DWORD dwSize, dwFlags, dwFourCC, dwRGBBitCount, dwRBitMask, dwGBitMask, dwBBitMask, dwRGBAlphaBitMask;
} DDPIXELFORMAT;                                      /* 0x20 */
typedef struct {
    DWORD dwSize;                 /* 0x00 */
    DWORD dwFlags;                /* 0x04 */
    DWORD dwHeight;               /* 0x08 */
    DWORD dwWidth;                /* 0x0c */
    LONG  lPitch;                 /* 0x10 */
    DWORD dwBackBufferCount;      /* 0x14 */
    DWORD dwRefreshRate;          /* 0x18 (union with dwMipMapCount) */
    DWORD dwAlphaBitDepth;        /* 0x1c */
    DWORD dwReserved;             /* 0x20 */
    void *lpSurface;              /* 0x24 */
    DDCOLORKEY ddckCKDestOverlay; /* 0x28 */
    DDCOLORKEY ddckCKDestBlt;     /* 0x30 */
    DDCOLORKEY ddckCKSrcOverlay;  /* 0x38 */
    DDCOLORKEY ddckCKSrcBlt;      /* 0x40 */
    DDPIXELFORMAT ddpfPixelFormat;/* 0x48 */
    DDSCAPS ddsCaps;              /* 0x68 */
} DDSURFACEDESC;                  /* 0x6c */

typedef struct {
    DWORD dwSize, dwDDFX, dwROP, dwDDROP, dwRotationAngle, dwZBufferOpCode, dwZBufferLow, dwZBufferHigh,
          dwZBufferBaseDest, dwZDestConstBitDepth, dwZDestConst, dwZSrcConstBitDepth, dwZSrcConst,
          dwAlphaEdgeBlendBitDepth, dwAlphaEdgeBlend, dwReserved, dwAlphaDestConstBitDepth, dwAlphaDestConst,
          dwAlphaSrcConstBitDepth, dwAlphaSrcConst;
    DWORD dwFillColor;            /* 0x50 */
    DDCOLORKEY ddckDestColorkey;  /* 0x54 */
    DDCOLORKEY ddckSrcColorkey;   /* 0x5c */
} DDBLTFX;                        /* 0x64 */

typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;

typedef HRESULT (WINAPI *LPDDENUMMODESCALLBACK)(DDSURFACEDESC *, void *);
typedef HRESULT (WINAPI *LPDDENUMSURFACESCALLBACK)(void *surf, DDSURFACEDESC *, void *);

#define STATIC_ASSERT(c, n) typedef char static_assert_##n[(c) ? 1 : -1]
STATIC_ASSERT(sizeof(DDSURFACEDESC) == 0x6c, ddsd);
STATIC_ASSERT(sizeof(DDBLTFX) == 0x64, ddbltfx);
STATIC_ASSERT(sizeof(CRITICAL_SECTION) == 24, cs);

#endif
