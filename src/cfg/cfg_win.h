/* usnf97cfg: the few Win32 declarations the configurator needs (no SDK headers). */
#ifndef CFG_WIN_H
#define CFG_WIN_H

#define WINAPI   __stdcall
#define CALLBACK __stdcall
#define NULL     ((void *)0)
#define TRUE     1
#define FALSE    0
#define IMPORT   __declspec(dllimport)

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef long           LONG;
typedef unsigned int   UINT;
typedef long           HRESULT;
typedef unsigned long  ULONG;
typedef int            INT_PTR;
typedef unsigned int   UINT_PTR;
typedef long           LONG_PTR;
typedef UINT_PTR       WPARAM;
typedef LONG_PTR       LPARAM;
typedef LONG_PTR       LRESULT;
typedef void          *HANDLE;
typedef struct HWND__      *HWND;
typedef struct HINSTANCE__ *HINSTANCE;
typedef HINSTANCE           HMODULE;
typedef struct HDC__       *HDC;
typedef struct HBRUSH__    *HBRUSH;
typedef struct HPEN__      *HPEN;
typedef struct HFONT__     *HFONT;
typedef struct HBITMAP__   *HBITMAP;
typedef struct HICON__     *HICON;
typedef HICON               HCURSOR;
typedef struct HMENU__     *HMENU;
typedef void               *HGDIOBJ;
typedef DWORD               COLORREF;
typedef struct { DWORD Data1; WORD Data2, Data3; BYTE Data4[8]; } GUID;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct {
    UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
    HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; const char *lpszMenuName, *lpszClassName; HICON hIconSm;
} WNDCLASSEXA;
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(void *);
typedef int (WINAPI *FARPROC)(void);

#define RGB(r, g, b) ((COLORREF)((BYTE)(r) | ((WORD)(BYTE)(g) << 8) | ((DWORD)(BYTE)(b) << 16)))
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xffff))
#define GET_X(l)  ((int)(short)LOWORD(l))
#define GET_Y(l)  ((int)(short)HIWORD(l))
#define MAKEINTRESOURCEA(i) ((const char *)(UINT_PTR)(WORD)(i))

/* messages */
#define WM_CREATE        0x0001
#define WM_DESTROY       0x0002
#define WM_PAINT         0x000F
#define WM_CLOSE         0x0010
#define WM_ERASEBKGND    0x0014
#define WM_SETFONT       0x0030
#define WM_KEYDOWN       0x0100
#define WM_COMMAND       0x0111
#define WM_TIMER         0x0113
#define WM_CTLCOLORSTATIC 0x0138
#define WM_LBUTTONDOWN   0x0201
#define WM_USER          0x0400
#define WM_APP           0x8000

/* styles */
#define WS_OVERLAPPED    0x00000000L
#define WS_CAPTION       0x00C00000L
#define WS_SYSMENU       0x00080000L
#define WS_MINIMIZEBOX   0x00020000L
#define WS_CHILD         0x40000000L
#define WS_VISIBLE       0x10000000L
#define WS_TABSTOP       0x00010000L
#define WS_VSCROLL       0x00200000L
#define WS_BORDER        0x00800000L
#define WS_CLIPCHILDREN  0x02000000L
#define WS_EX_CLIENTEDGE 0x00000200L
#define CS_HREDRAW       0x0002
#define CS_VREDRAW       0x0001
#define CW_USEDEFAULT    ((int)0x80000000)
#define SW_SHOW          5

#define BS_PUSHBUTTON    0x0
#define BS_AUTOCHECKBOX  0x3
#define BN_CLICKED       0
#define BM_GETCHECK      0x00F0
#define BM_SETCHECK      0x00F1
#define CBS_DROPDOWNLIST 0x0003
#define CB_ADDSTRING     0x0143
#define CB_GETCURSEL     0x0147
#define CB_RESETCONTENT  0x014B
#define CB_SETCURSEL     0x014E
#define CBN_SELCHANGE    1
#define ES_AUTOHSCROLL   0x0080
#define ES_NUMBER        0x2000
#define EN_KILLFOCUS     0x0200

#define IDC_ARROW        MAKEINTRESOURCEA(32512)
#define COLOR_BTNFACE    15
#define TRANSPARENT      1
#define DEFAULT_GUI_FONT 17
#define SRCCOPY          0x00CC0020
#define PS_SOLID         0
#define NULL_PEN         8
#define NULL_BRUSH       5
#define DT_LEFT          0x0000
#define DT_CENTER        0x0001
#define DT_RIGHT         0x0002
#define DT_VCENTER       0x0004
#define DT_SINGLELINE    0x0020
#define DT_NOPREFIX      0x0800
#define DT_END_ELLIPSIS  0x8000
#define FW_NORMAL        400
#define FW_BOLD          700
#define MB_OK            0x0
#define MB_ICONERROR     0x10
#define MB_ICONWARNING   0x30
#define MB_YESNOCANCEL   0x3
#define MB_YESNO         0x4
#define IDYES            6
#define IDNO             7
#define IDCANCEL         2
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

/* kernel32 */
IMPORT DWORD   WINAPI GetModuleFileNameA(HMODULE, char *, DWORD);
IMPORT HMODULE WINAPI GetModuleHandleA(const char *);
IMPORT HMODULE WINAPI LoadLibraryA(const char *);
IMPORT FARPROC WINAPI GetProcAddress(HMODULE, const char *);
IMPORT void    WINAPI ExitProcess(UINT);
IMPORT DWORD   WINAPI GetTickCount(void);
IMPORT void    WINAPI Sleep(DWORD);
IMPORT HANDLE  WINAPI CreateThread(void *, DWORD, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
IMPORT BOOL    WINAPI CloseHandle(HANDLE);
IMPORT DWORD   WINAPI GetPrivateProfileStringA(const char *, const char *, const char *, char *, DWORD, const char *);
IMPORT UINT    WINAPI GetPrivateProfileIntA(const char *, const char *, int, const char *);
IMPORT DWORD   WINAPI GetPrivateProfileSectionA(const char *, char *, DWORD, const char *);
IMPORT BOOL    WINAPI WritePrivateProfileStringA(const char *, const char *, const char *, const char *);
IMPORT BOOL    WINAPI WritePrivateProfileSectionA(const char *, const char *, const char *);
IMPORT DWORD   WINAPI GetFileAttributesA(const char *);
IMPORT BOOL    WINAPI CopyFileA(const char *, const char *, BOOL);
IMPORT char *  WINAPI GetCommandLineA(void);
IMPORT DWORD   WINAPI GetLastError(void);

/* user32 */
IMPORT WORD    WINAPI RegisterClassExA(const WNDCLASSEXA *);
IMPORT HWND    WINAPI CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
IMPORT LRESULT WINAPI DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
IMPORT BOOL    WINAPI GetMessageA(MSG *, HWND, UINT, UINT);
IMPORT BOOL    WINAPI TranslateMessage(const MSG *);
IMPORT LRESULT WINAPI DispatchMessageA(const MSG *);
IMPORT BOOL    WINAPI IsDialogMessageA(HWND, MSG *);
IMPORT void    WINAPI PostQuitMessage(int);
IMPORT BOOL    WINAPI ShowWindow(HWND, int);
IMPORT BOOL    WINAPI UpdateWindow(HWND);
IMPORT BOOL    WINAPI InvalidateRect(HWND, const RECT *, BOOL);
IMPORT UINT_PTR WINAPI SetTimer(HWND, UINT_PTR, UINT, void *);
IMPORT BOOL    WINAPI KillTimer(HWND, UINT_PTR);
IMPORT HDC     WINAPI BeginPaint(HWND, PAINTSTRUCT *);
IMPORT BOOL    WINAPI EndPaint(HWND, const PAINTSTRUCT *);
IMPORT int     WINAPI FillRect(HDC, const RECT *, HBRUSH);
IMPORT int     WINAPI FrameRect(HDC, const RECT *, HBRUSH);
IMPORT int     WINAPI DrawTextA(HDC, const char *, int, RECT *, UINT);
IMPORT LRESULT WINAPI SendMessageA(HWND, UINT, WPARAM, LPARAM);
IMPORT BOOL    WINAPI PostMessageA(HWND, UINT, WPARAM, LPARAM);
IMPORT int     WINAPI MessageBoxA(HWND, const char *, const char *, UINT);
IMPORT HCURSOR WINAPI LoadCursorA(HINSTANCE, const char *);
IMPORT BOOL    WINAPI GetClientRect(HWND, RECT *);
IMPORT BOOL    WINAPI AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD);
IMPORT int     __cdecl wsprintfA(char *, const char *, ...);
IMPORT BOOL    WINAPI SetWindowTextA(HWND, const char *);
IMPORT int     WINAPI GetWindowTextA(HWND, char *, int);
IMPORT BOOL    WINAPI EnableWindow(HWND, BOOL);
IMPORT BOOL    WINAPI DestroyWindow(HWND);
IMPORT HBRUSH  WINAPI GetSysColorBrush(int);
IMPORT DWORD   WINAPI GetSysColor(int);
IMPORT HWND    WINAPI GetFocus(void);

/* gdi32 */
IMPORT HBRUSH  WINAPI CreateSolidBrush(COLORREF);
IMPORT HPEN    WINAPI CreatePen(int, int, COLORREF);
IMPORT HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ);
IMPORT BOOL    WINAPI DeleteObject(HGDIOBJ);
IMPORT HGDIOBJ WINAPI GetStockObject(int);
IMPORT int     WINAPI SetBkMode(HDC, int);
IMPORT COLORREF WINAPI SetTextColor(HDC, COLORREF);
IMPORT COLORREF WINAPI SetBkColor(HDC, COLORREF);
IMPORT HFONT   WINAPI CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const char *);
IMPORT BOOL    WINAPI Rectangle(HDC, int, int, int, int);
IMPORT BOOL    WINAPI RoundRect(HDC, int, int, int, int, int, int);
IMPORT BOOL    WINAPI Ellipse(HDC, int, int, int, int);
IMPORT BOOL    WINAPI MoveToEx(HDC, int, int, POINT *);
IMPORT BOOL    WINAPI LineTo(HDC, int, int);
IMPORT HDC     WINAPI CreateCompatibleDC(HDC);
IMPORT HBITMAP WINAPI CreateCompatibleBitmap(HDC, int, int);
IMPORT BOOL    WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
IMPORT BOOL    WINAPI DeleteDC(HDC);

#endif
