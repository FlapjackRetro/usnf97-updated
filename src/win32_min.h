/* Minimal Win32 declarations for the USNF '97 DirectDraw shim.
 * The shim deliberately avoids the Windows SDK headers so it builds the same way
 * with clang/lld on Linux and with MSVC on Windows (no CRT, no SDK headers). */
#ifndef WIN32_MIN_H
#define WIN32_MIN_H

#define WINAPI   __stdcall
#define CALLBACK __stdcall
#define IMPORT   __declspec(dllimport)

typedef int            BOOL;
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef long           LONG;
typedef unsigned int   UINT;
typedef long           HRESULT;
typedef unsigned int   UINT_PTR;
typedef long           LONG_PTR;
typedef unsigned long  ULONG;
typedef UINT_PTR       WPARAM;
typedef LONG_PTR       LPARAM;
typedef LONG_PTR       LRESULT;
typedef void          *HANDLE;
typedef void          *HWND;
typedef void          *HDC;
typedef void          *HINSTANCE;
typedef void          *HMODULE;
typedef void          *HMONITOR;
typedef void          *HCURSOR;
typedef void          *HBRUSH;
typedef void          *HGDIOBJ;
typedef void          *HMENU;
typedef long long      LONGLONG;
typedef unsigned long long ULONGLONG;

#define TRUE  1
#define FALSE 0
#ifndef NULL
#define NULL ((void*)0)
#endif

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { DWORD Data1; WORD Data2; WORD Data3; BYTE Data4[8]; } GUID;
typedef GUID IID;
typedef union { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;

/* CRITICAL_SECTION is 24 bytes on x86 */
typedef struct { void *DebugInfo; LONG LockCount; LONG RecursionCount; HANDLE OwningThread;
                 HANDLE LockSemaphore; UINT_PTR SpinCount; } CRITICAL_SECTION;

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD   (WINAPI *LPTHREAD_START_ROUTINE)(void *);
typedef int     (WINAPI *FARPROC)(void);

typedef struct { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; } MONITORINFO;

typedef struct {
    DWORD biSize; LONG biWidth; LONG biHeight; WORD biPlanes; WORD biBitCount;
    DWORD biCompression; DWORD biSizeImage; LONG biXPelsPerMeter; LONG biYPelsPerMeter;
    DWORD biClrUsed; DWORD biClrImportant;
} BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[256]; } BITMAPINFO256;

typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore; BOOL fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;

/* ---- constants ---- */
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define INFINITE           0xFFFFFFFFul
#define WAIT_TIMEOUT       0x102ul
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_READWRITE     0x04
#define MEM_COMMIT         0x1000
#define MEM_RESERVE        0x2000
#define MEM_RELEASE        0x8000
#define GENERIC_READ       0x80000000ul
#define GENERIC_WRITE      0x40000000ul
#define FILE_SHARE_READ    1
#define FILE_SHARE_WRITE   2
#define OPEN_EXISTING      3
#define CREATE_ALWAYS      2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFul
#define THREAD_PRIORITY_ABOVE_NORMAL 1

#define GWL_WNDPROC   (-4)
#define GWL_STYLE     (-16)
#define GWL_EXSTYLE   (-20)
#define WS_OVERLAPPED 0x00000000ul
#define WS_POPUP      0x80000000ul
#define WS_VISIBLE    0x10000000ul
#define WS_CAPTION    0x00C00000ul
#define WS_SYSMENU    0x00080000ul
#define WS_THICKFRAME 0x00040000ul
#define WS_MINIMIZEBOX 0x00020000ul
#define WS_MAXIMIZEBOX 0x00010000ul
#define WS_EX_APPWINDOW 0x00040000ul
#define WS_EX_TOPMOST   0x00000008ul
#define SWP_NOSIZE        0x0001
#define SWP_NOMOVE        0x0002
#define SWP_NOZORDER      0x0004
#define SWP_NOACTIVATE    0x0010
#define SWP_FRAMECHANGED  0x0020
#define SWP_SHOWWINDOW    0x0040
#define SWP_NOOWNERZORDER 0x0200
#define MONITOR_DEFAULTTONEAREST 2
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define COLORONCOLOR 3
#define HALFTONE     4
#define DIB_RGB_COLORS 0
#define SRCCOPY  0x00CC0020ul
#define BLACKNESS 0x00000042ul
#define BI_RGB 0
#define HTCLIENT 1

#define WM_DESTROY      0x0002
#define WM_SIZE         0x0005
#define WM_PAINT        0x000F
#define WM_CLOSE        0x0010
#define WM_ERASEBKGND   0x0014
#define WM_ACTIVATEAPP  0x001C
#define WM_SETCURSOR    0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCHITTEST    0x0084
#define WM_KEYDOWN      0x0100
#define WM_KEYUP        0x0101
#define WM_SYSKEYDOWN   0x0104
#define WM_SYSKEYUP     0x0105
#define WM_SYSCHAR      0x0106
#define WM_SYSCOMMAND   0x0112
#define WM_MOUSEFIRST   0x0200
#define WM_MOUSEWHEEL   0x020A
#define WM_MOUSELAST    0x020D
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_APP          0x8000
#define SC_KEYMENU      0xF100
#define VK_RETURN       0x0D

/* ---- kernel32 ---- */
IMPORT void   WINAPI InitializeCriticalSection(CRITICAL_SECTION *);
IMPORT void   WINAPI EnterCriticalSection(CRITICAL_SECTION *);
IMPORT void   WINAPI LeaveCriticalSection(CRITICAL_SECTION *);
IMPORT BOOL   WINAPI TryEnterCriticalSection(CRITICAL_SECTION *);
IMPORT HANDLE WINAPI CreateEventA(void *, BOOL, BOOL, const char *);
IMPORT BOOL   WINAPI SetEvent(HANDLE);
IMPORT DWORD  WINAPI WaitForSingleObject(HANDLE, DWORD);
IMPORT HANDLE WINAPI CreateThread(void *, DWORD, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
IMPORT BOOL   WINAPI SetThreadPriority(HANDLE, int);
IMPORT void   WINAPI Sleep(DWORD);
IMPORT BOOL   WINAPI QueryPerformanceCounter(LARGE_INTEGER *);
IMPORT BOOL   WINAPI QueryPerformanceFrequency(LARGE_INTEGER *);
IMPORT HMODULE WINAPI GetModuleHandleA(const char *);
IMPORT DWORD  WINAPI GetModuleFileNameA(HMODULE, char *, DWORD);
IMPORT HMODULE WINAPI LoadLibraryA(const char *);
IMPORT FARPROC WINAPI GetProcAddress(HMODULE, const char *);
IMPORT BOOL   WINAPI VirtualProtect(void *, DWORD, DWORD, DWORD *);
IMPORT void * WINAPI VirtualAlloc(void *, DWORD, DWORD, DWORD);
IMPORT BOOL   WINAPI VirtualFree(void *, DWORD, DWORD);
IMPORT BOOL   WINAPI FlushInstructionCache(HANDLE, const void *, DWORD);
IMPORT HANDLE WINAPI GetCurrentProcess(void);
IMPORT HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMPORT BOOL   WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
IMPORT BOOL   WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMPORT BOOL   WINAPI CloseHandle(HANDLE);
IMPORT DWORD  WINAPI GetFileSize(HANDLE, DWORD *);
IMPORT DWORD  WINAPI GetFileAttributesA(const char *);
IMPORT DWORD  WINAPI GetShortPathNameA(const char *, char *, DWORD);
IMPORT UINT   WINAPI GetPrivateProfileIntA(const char *, const char *, int, const char *);
IMPORT DWORD  WINAPI GetPrivateProfileStringA(const char *, const char *, const char *, char *, DWORD, const char *);
IMPORT DWORD  WINAPI GetPrivateProfileSectionA(const char *, char *, DWORD, const char *);
IMPORT LONG   WINAPI InterlockedCompareExchange(LONG volatile *, LONG, LONG);
IMPORT DWORD  WINAPI GetTickCount(void);
IMPORT BOOL   WINAPI DisableThreadLibraryCalls(HMODULE);
IMPORT void   WINAPI OutputDebugStringA(const char *);
typedef struct { DWORD ExceptionCode; DWORD ExceptionFlags; void *ExceptionRecord; void *ExceptionAddress;
                 DWORD NumberParameters; UINT_PTR ExceptionInformation[15]; } EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; BYTE *ContextRecord; } EXCEPTION_POINTERS;
#define CONTEXT_EIP_OFFSET 0xB8                 /* x86 CONTEXT.Eip */
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096ul
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0
typedef LONG (WINAPI *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *);
IMPORT void * WINAPI AddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER);

/* ---- user32 ---- */
IMPORT LONG    WINAPI SetWindowLongA(HWND, int, LONG);
IMPORT LONG    WINAPI GetWindowLongA(HWND, int);
IMPORT LRESULT WINAPI CallWindowProcA(WNDPROC, HWND, UINT, WPARAM, LPARAM);
IMPORT LRESULT WINAPI DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
IMPORT HWND    WINAPI CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
IMPORT BOOL    WINAPI GetCursorPos(POINT *);
IMPORT BOOL    WINAPI ScreenToClient(HWND, POINT *);
IMPORT BOOL    WINAPI ClientToScreen(HWND, POINT *);
IMPORT BOOL    WINAPI ClipCursor(const RECT *);
IMPORT BOOL    WINAPI GetClipCursor(RECT *);
IMPORT BOOL    WINAPI GetClientRect(HWND, RECT *);
IMPORT BOOL    WINAPI GetWindowRect(HWND, RECT *);
IMPORT BOOL    WINAPI AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD);
IMPORT BOOL    WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
IMPORT BOOL    WINAPI PostMessageA(HWND, UINT, WPARAM, LPARAM);
IMPORT HDC     WINAPI GetDC(HWND);
IMPORT int     WINAPI ReleaseDC(HWND, HDC);
IMPORT HDC     WINAPI BeginPaint(HWND, PAINTSTRUCT *);
IMPORT BOOL    WINAPI EndPaint(HWND, const PAINTSTRUCT *);
IMPORT int     WINAPI GetSystemMetrics(int);
IMPORT HMONITOR WINAPI MonitorFromWindow(HWND, DWORD);
IMPORT BOOL    WINAPI GetMonitorInfoA(HMONITOR, MONITORINFO *);
IMPORT BOOL    WINAPI SetProcessDPIAware(void);
IMPORT BOOL    WINAPI IsIconic(HWND);
IMPORT BOOL    WINAPI IsWindow(HWND);
IMPORT int     WINAPI ShowCursor(BOOL);
IMPORT HCURSOR WINAPI SetCursor(HCURSOR);
IMPORT HCURSOR WINAPI LoadCursorA(HINSTANCE, const char *);
IMPORT int     WINAPI FillRect(HDC, const RECT *, HBRUSH);
IMPORT int     WINAPI MessageBoxA(HWND, const char *, const char *, UINT);
IMPORT HWND    WINAPI GetForegroundWindow(void);
IMPORT UINT    WINAPI MapVirtualKeyA(UINT, UINT);
IMPORT int     __cdecl wsprintfA(char *, const char *, ...);
/* varargs without the CRT: compiler builtins (clang), classic x86 stack walk (MSVC) */
#if defined(__clang__) || defined(__GNUC__)
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#else
typedef char *va_list;
#define va_start(ap, last) ((ap) = (va_list)&(last) + ((sizeof(last) + 3) & ~3))
#define va_end(ap)         ((ap) = (va_list)0)
#endif
IMPORT int     WINAPI wvsprintfA(char *, const char *, va_list);
#define IDC_ARROW ((const char *)32512)

/* ---- gdi32 ---- */
IMPORT int     WINAPI StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void *, const void *, UINT, DWORD);
IMPORT int     WINAPI SetStretchBltMode(HDC, int);
IMPORT BOOL    WINAPI SetBrushOrgEx(HDC, int, int, POINT *);
IMPORT HGDIOBJ WINAPI GetStockObject(int);
#define BLACK_BRUSH 4

/* ---- winmm ---- */
IMPORT UINT WINAPI timeBeginPeriod(UINT);
IMPORT UINT WINAPI timeEndPeriod(UINT);

/* ---- helpers ---- */
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xffff))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)(((WORD)(l)) | (((DWORD)(WORD)(h)) << 16)))

#endif
