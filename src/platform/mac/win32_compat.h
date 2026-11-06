// Win32 compatibility layer for the macOS build of Toolscreen.
//
// The shared Toolscreen sources were written against <windows.h>. On macOS this header is
// force-included into every C++ translation unit (see cmake/macos/ToolscreenMacOS.cmake) and
// provides the subset of the Win32 API surface those sources use, implemented on top of
// POSIX/Cocoa in win32_compat.cpp and mac_platform.mm.
//
// Semantics follow Win64 (LLP64): LONG/DWORD are 32-bit even though `long` is 64-bit here.
// "Client" coordinates for the game window are framebuffer pixels with a top-left origin, which
// is what the Windows code expects from a DPI-aware process.
#pragma once

#if !defined(_WIN32)

#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <string>
#include <strings.h>

#define TOOLSCREEN_WIN32_COMPAT 1

// -------------------------------------------------------------------------------------------
// Calling conventions / attributes / MSVC keywords
// -------------------------------------------------------------------------------------------
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define STDMETHODCALLTYPE
#define __stdcall
#define __cdecl
#define __fastcall
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
#define DECLSPEC_NOINLINE __attribute__((noinline))
#define FORCEINLINE __forceinline
#define UNREFERENCED_PARAMETER(P) (void)(P)
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _Reserved_
#define _Use_decl_annotations_
#define _Check_return_
#define _Success_(x)
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define _TRUNCATE ((size_t)-1)
#define TOOLSCREEN_WIN32_NOT_SUPPORTED 1

// -------------------------------------------------------------------------------------------
// Path separators (shared sources use these instead of hard-coded backslashes)
// -------------------------------------------------------------------------------------------
#define TS_WSEP L"/"
#define TS_SEP "/"
#define TS_WSEP_CHAR L'/'
#define TS_SEP_CHAR '/'

// -------------------------------------------------------------------------------------------
// Basic types (LLP64 semantics)
// -------------------------------------------------------------------------------------------
typedef int BOOL;
typedef BOOL* PBOOL;
typedef BOOL* LPBOOL;
typedef uint8_t BYTE;
typedef BYTE BOOLEAN;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;
typedef uint16_t WORD;
typedef WORD* PWORD;
typedef WORD* LPWORD;
typedef uint32_t DWORD;
typedef DWORD* PDWORD;
typedef DWORD* LPDWORD;
typedef int32_t LONG;
typedef LONG* PLONG;
typedef LONG* LPLONG;
typedef uint32_t ULONG;
typedef ULONG* PULONG;
typedef uint32_t UINT;
typedef UINT* PUINT;
typedef int INT;
typedef int16_t SHORT;
typedef uint16_t USHORT;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef wchar_t WCHAR;
typedef WCHAR TCHAR;
typedef float FLOAT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t DWORD64;
typedef uint64_t DWORDLONG;
typedef int64_t INT64;
typedef uint64_t UINT64;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef ULONG_PTR* PULONG_PTR;
typedef DWORD_PTR* PDWORD_PTR;
typedef size_t SIZE_T;
typedef intptr_t SSIZE_T;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef int32_t HRESULT;
typedef WORD ATOM;
typedef DWORD COLORREF;
typedef DWORD LCID;
typedef WORD LANGID;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef char* PSTR;
typedef const char* LPCSTR;
typedef const char* PCSTR;
typedef wchar_t* LPWSTR;
typedef wchar_t* PWSTR;
typedef const wchar_t* LPCWSTR;
typedef const wchar_t* PCWSTR;
typedef LPWSTR LPTSTR;
typedef LPCWSTR LPCTSTR;
typedef wchar_t* PWCHAR;
// Function addresses are untyped here (MSVC lets FARPROC convert to/from void* implicitly; clang
// does not), which keeps `LPVOID p = GetProcAddress(...)` and reinterpret_casts compiling.
typedef void* FARPROC;
typedef void* PROC;
typedef void* NEARPROC;

// Distinct opaque handle types, like <windows.h> with STRICT.
#define DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__* name
typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE* LPHANDLE;
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
typedef HINSTANCE HMODULE;
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HICON);
typedef HICON HCURSOR;
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HKEY);
typedef HKEY* PHKEY;
DECLARE_HANDLE(HHOOK);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HRAWINPUT);
DECLARE_HANDLE(HDROP);
DECLARE_HANDLE(HDESK);
DECLARE_HANDLE(HWINSTA);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HINTERNET_COMPAT_UNUSED);
typedef void* HGDIOBJ;
typedef void* HGLOBAL;
typedef void* HLOCAL;
typedef void* HINTERNET;

typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT(CALLBACK* HOOKPROC)(int code, WPARAM wParam, LPARAM lParam);
typedef BOOL(CALLBACK* WNDENUMPROC)(HWND, LPARAM);
typedef void(CALLBACK* TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef DWORD(WINAPI* LPTHREAD_START_ROUTINE)(LPVOID);

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

// -------------------------------------------------------------------------------------------
// Structures
// -------------------------------------------------------------------------------------------
typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT* LPCRECT;
typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;
typedef struct tagPOINTS { SHORT x, y; } POINTS;
typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *PMSG, *LPMSG;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    struct { DWORD LowPart; DWORD HighPart; } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED { ULONG_PTR Internal; ULONG_PTR InternalHigh; DWORD Offset; DWORD OffsetHigh; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;

typedef struct tagCURSORINFO { DWORD cbSize; DWORD flags; HCURSOR hCursor; POINT ptScreenPos; } CURSORINFO, *PCURSORINFO, *LPCURSORINFO;
typedef struct _ICONINFO { BOOL fIcon; DWORD xHotspot; DWORD yHotspot; HBITMAP hbmMask; HBITMAP hbmColor; } ICONINFO, *PICONINFO;
typedef struct _ICONINFOEXW {
    DWORD cbSize; BOOL fIcon; DWORD xHotspot; DWORD yHotspot; HBITMAP hbmMask; HBITMAP hbmColor;
    WORD wResID; WCHAR szModName[MAX_PATH]; WCHAR szResName[MAX_PATH];
} ICONINFOEXW, *PICONINFOEXW;
typedef struct tagBITMAP { LONG bmType; LONG bmWidth; LONG bmHeight; LONG bmWidthBytes; WORD bmPlanes; WORD bmBitsPixel; LPVOID bmBits; } BITMAP;
typedef struct tagMONITORINFO { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; } MONITORINFO, *LPMONITORINFO;
typedef struct tagMONITORINFOEXW { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; WCHAR szDevice[32]; } MONITORINFOEXW;

typedef struct tagKEYBDINPUT { WORD wVk; WORD wScan; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; } KEYBDINPUT;
typedef struct tagMOUSEINPUT { LONG dx; LONG dy; DWORD mouseData; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; } MOUSEINPUT;
typedef struct tagHARDWAREINPUT { DWORD uMsg; WORD wParamL; WORD wParamH; } HARDWAREINPUT;
typedef struct tagINPUT {
    DWORD type;
    union { MOUSEINPUT mi; KEYBDINPUT ki; HARDWAREINPUT hi; };
} INPUT, *PINPUT, *LPINPUT;

typedef struct tagKBDLLHOOKSTRUCT { DWORD vkCode; DWORD scanCode; DWORD flags; DWORD time; ULONG_PTR dwExtraInfo; } KBDLLHOOKSTRUCT, *PKBDLLHOOKSTRUCT, *LPKBDLLHOOKSTRUCT;
typedef struct tagMSLLHOOKSTRUCT { POINT pt; DWORD mouseData; DWORD flags; DWORD time; ULONG_PTR dwExtraInfo; } MSLLHOOKSTRUCT;

typedef struct tagRAWINPUTHEADER { DWORD dwType; DWORD dwSize; HANDLE hDevice; WPARAM wParam; } RAWINPUTHEADER;
typedef struct tagRAWMOUSE {
    USHORT usFlags;
    union { ULONG ulButtons; struct { USHORT usButtonFlags; USHORT usButtonData; }; };
    ULONG ulRawButtons; LONG lLastX; LONG lLastY; ULONG ulExtraInformation;
} RAWMOUSE;
typedef struct tagRAWKEYBOARD { USHORT MakeCode; USHORT Flags; USHORT Reserved; USHORT VKey; UINT Message; ULONG ExtraInformation; } RAWKEYBOARD;
typedef struct tagRAWHID { DWORD dwSizeHid; DWORD dwCount; BYTE bRawData[1]; } RAWHID;
typedef struct tagRAWINPUT { RAWINPUTHEADER header; union { RAWMOUSE mouse; RAWKEYBOARD keyboard; RAWHID hid; } data; } RAWINPUT, *PRAWINPUT;
typedef struct tagRAWINPUTDEVICE { USHORT usUsagePage; USHORT usUsage; DWORD dwFlags; HWND hwndTarget; } RAWINPUTDEVICE;

typedef struct tagWINDOWPLACEMENT { UINT length; UINT flags; UINT showCmd; POINT ptMinPosition; POINT ptMaxPosition; RECT rcNormalPosition; } WINDOWPLACEMENT;
typedef struct tagWINDOWPOS { HWND hwnd; HWND hwndInsertAfter; int x; int y; int cx; int cy; UINT flags; } WINDOWPOS, *LPWINDOWPOS;
typedef struct tagMINMAXINFO { POINT ptReserved; POINT ptMaxSize; POINT ptMaxPosition; POINT ptMinTrackSize; POINT ptMaxTrackSize; } MINMAXINFO;
typedef struct tagTRACKMOUSEEVENT { DWORD cbSize; DWORD dwFlags; HWND hwndTrack; DWORD dwHoverTime; } TRACKMOUSEEVENT, *LPTRACKMOUSEEVENT;
typedef struct tagWNDCLASSEXW {
    UINT cbSize; UINT style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra; HINSTANCE hInstance; HICON hIcon;
    HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName; LPCWSTR lpszClassName; HICON hIconSm;
} WNDCLASSEXW;
typedef struct tagCREATESTRUCTW { LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; int cy; int cx; int y; int x; LONG style; LPCWSTR lpszName; LPCWSTR lpszClass; DWORD dwExStyle; } CREATESTRUCTW;
typedef struct tagOFNW {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter; DWORD nMaxCustFilter;
    DWORD nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle; DWORD nMaxFileTitle; LPCWSTR lpstrInitialDir;
    LPCWSTR lpstrTitle; DWORD Flags; WORD nFileOffset; WORD nFileExtension; LPCWSTR lpstrDefExt; LPARAM lCustData; void* lpfnHook;
    LPCWSTR lpTemplateName; void* pvReserved; DWORD dwReserved; DWORD FlagsEx;
} OPENFILENAMEW, *LPOPENFILENAMEW;
typedef OPENFILENAMEW OPENFILENAME;

typedef struct _MODULEINFO { LPVOID lpBaseOfDll; DWORD SizeOfImage; LPVOID EntryPoint; } MODULEINFO, *LPMODULEINFO;
typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress; PVOID AllocationBase; DWORD AllocationProtect; SIZE_T RegionSize; DWORD State; DWORD Protect; DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize; LONG biWidth; LONG biHeight; WORD biPlanes; WORD biBitCount; DWORD biCompression; DWORD biSizeImage;
    LONG biXPelsPerMeter; LONG biYPelsPerMeter; DWORD biClrUsed; DWORD biClrImportant;
} BITMAPINFOHEADER;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct tagBITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
typedef struct _BLENDFUNCTION { BYTE BlendOp; BYTE BlendFlags; BYTE SourceConstantAlpha; BYTE AlphaFormat; } BLENDFUNCTION;

typedef struct _GUID { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; } GUID;
typedef GUID KNOWNFOLDERID;
typedef const GUID& REFKNOWNFOLDERID;
typedef const GUID& REFGUID;
typedef const GUID& REFIID;

typedef struct tagFILTERKEYS { UINT cbSize; DWORD dwFlags; DWORD iWaitMSec; DWORD iDelayMSec; DWORD iRepeatMSec; DWORD iBounceMSec; } FILTERKEYS;

typedef struct _CRITICAL_SECTION_COMPAT_UNUSED { int unused; } CRITICAL_SECTION_COMPAT_UNUSED;

// -------------------------------------------------------------------------------------------
// Macros
// -------------------------------------------------------------------------------------------
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))
#define GET_KEYSTATE_WPARAM(wParam) (LOWORD(wParam))
#define GET_XBUTTON_WPARAM(wParam) (HIWORD(wParam))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#define IS_INTRESOURCE(_r) ((((ULONG_PTR)(_r)) >> 16) == 0)
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | (7 << 16) | 0x80000000)))
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define ZeroMemory(dst, len) memset((dst), 0, (len))
#define SecureZeroMemory(dst, len) memset((dst), 0, (len))
#define CopyMemory(dst, src, len) memcpy((dst), (src), (len))
#define MoveMemory(dst, src, len) memmove((dst), (src), (len))
#define FillMemory(dst, len, fill) memset((dst), (fill), (len))
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define TEXT(s) L##s
#define _T(s) L##s

// -------------------------------------------------------------------------------------------
// Error codes
// -------------------------------------------------------------------------------------------
#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_OUTOFMEMORY 14L
#define ERROR_NOT_READY 21L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_HANDLE_EOF 38L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_MOD_NOT_FOUND 126L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_TIMEOUT 1460L
#define ERROR_CANCELLED 1223L
#define ERROR_INVALID_WINDOW_HANDLE 1400L
#define ERROR_CLASS_ALREADY_EXISTS 1410L
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_TIMEOUT 258L
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)
#define INFINITE 0xFFFFFFFF
#define STILL_ACTIVE 259

// -------------------------------------------------------------------------------------------
// Files
// -------------------------------------------------------------------------------------------
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define GENERIC_EXECUTE 0x20000000L
#define GENERIC_ALL 0x10000000L
#define DELETE 0x00010000L
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008
#define CP_ACP 0
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_ERR_INVALID_CHARS 0x00000080

// -------------------------------------------------------------------------------------------
// Memory / module / process
// -------------------------------------------------------------------------------------------
#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_FREE 0x00010000
#define MEM_IMAGE 0x01000000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_VM_READ 0x0010
#define SYNCHRONIZE 0x00100000L
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_IDLE (-15)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#define CREATE_WAITABLE_TIMER_MANUAL_RESET 0x00000001
#define TIMER_ALL_ACCESS 0x1F0003
#define EVENT_ALL_ACCESS 0x1F0003
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
#define DLL_PROCESS_DETACH 0

// -------------------------------------------------------------------------------------------
// Window messages
// -------------------------------------------------------------------------------------------
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETREDRAW 0x000B
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_INPUT_DEVICE_CHANGE 0x00FE
#define WM_INPUT 0x00FF
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_UNICHAR 0x0109
#define WM_KEYLAST 0x0109
#define UNICODE_NOCHAR 0xFFFF
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_MENUCHAR 0x0120
#define WM_ENTERIDLE 0x0121
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_MOUSELAST 0x020E
#define WM_PARENTNOTIFY 0x0210
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define WM_SIZING 0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_MOVING 0x0216
#define WM_DEVICECHANGE 0x0219
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_DROPFILES 0x0233
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CHAR 0x0286
#define WM_MOUSEHOVER 0x02A1
#define WM_MOUSELEAVE 0x02A3
#define WM_DPICHANGED 0x02E0
#define WM_DISPLAYCHANGE 0x007E
#define WM_GETICON 0x007F
#define WM_SETICON 0x0080
#define WM_STYLECHANGING 0x007C
#define WM_STYLECHANGED 0x007D
#define WM_USER 0x0400
#define WM_APP 0x8000

#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SIZE_MAXSHOW 3
#define SIZE_MAXHIDE 4
#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define HTCLIENT 1
#define HTCAPTION 2
#define HTNOWHERE 0
#define MA_ACTIVATE 1
#define MA_NOACTIVATE 3

#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define WHEEL_DELTA 120
#define WHEEL_PAGESCROLL (UINT_MAX)

#define KF_EXTENDED 0x0100
#define KF_DLGMODE 0x0800
#define KF_MENUMODE 0x1000
#define KF_ALTDOWN 0x2000
#define KF_REPEAT 0x4000
#define KF_UP 0x8000

#define LLKHF_EXTENDED 0x00000001
#define LLKHF_LOWER_IL_INJECTED 0x00000002
#define LLKHF_INJECTED 0x00000010
#define LLKHF_ALTDOWN 0x00000020
#define LLKHF_UP 0x00000080
#define WH_KEYBOARD_LL 13
#define WH_MOUSE_LL 14
#define WH_GETMESSAGE 3
#define WH_CALLWNDPROC 4
#define HC_ACTION 0

#define INPUT_MOUSE 0
#define INPUT_KEYBOARD 1
#define INPUT_HARDWARE 2
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002
#define KEYEVENTF_UNICODE 0x0004
#define KEYEVENTF_SCANCODE 0x0008
#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
#define MOUSEEVENTF_MIDDLEDOWN 0x0020
#define MOUSEEVENTF_MIDDLEUP 0x0040
#define MOUSEEVENTF_XDOWN 0x0080
#define MOUSEEVENTF_XUP 0x0100
#define MOUSEEVENTF_WHEEL 0x0800
#define MOUSEEVENTF_HWHEEL 0x01000
#define MOUSEEVENTF_ABSOLUTE 0x8000
#define MOUSEEVENTF_VIRTUALDESK 0x4000

#define RID_INPUT 0x10000003
#define RID_HEADER 0x10000005
#define RIM_TYPEMOUSE 0
#define RIM_TYPEKEYBOARD 1
#define RIM_TYPEHID 2
#define MOUSE_MOVE_RELATIVE 0
#define MOUSE_MOVE_ABSOLUTE 1
#define MOUSE_VIRTUAL_DESKTOP 0x02
#define RI_KEY_MAKE 0
#define RI_KEY_BREAK 1
#define RI_KEY_E0 2
#define RI_KEY_E1 4
#define RIDEV_INPUTSINK 0x00000100
#define RIDEV_REMOVE 0x00000001
#define RI_MOUSE_LEFT_BUTTON_DOWN 0x0001
#define RI_MOUSE_LEFT_BUTTON_UP 0x0002
#define RI_MOUSE_RIGHT_BUTTON_DOWN 0x0004
#define RI_MOUSE_RIGHT_BUTTON_UP 0x0008
#define RI_MOUSE_MIDDLE_BUTTON_DOWN 0x0010
#define RI_MOUSE_MIDDLE_BUTTON_UP 0x0020
#define RI_MOUSE_BUTTON_4_DOWN 0x0040
#define RI_MOUSE_BUTTON_4_UP 0x0080
#define RI_MOUSE_BUTTON_5_DOWN 0x0100
#define RI_MOUSE_BUTTON_5_UP 0x0200
#define RI_MOUSE_WHEEL 0x0400
#define RI_MOUSE_HWHEEL 0x0800

#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
#define MAPVK_VK_TO_VSC_EX 4

// -------------------------------------------------------------------------------------------
// Virtual key codes (identical values to WinUser.h; configs store these, so they stay portable)
// -------------------------------------------------------------------------------------------
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_KANA 0x15
#define VK_HANGUL 0x15
#define VK_JUNJA 0x17
#define VK_FINAL 0x18
#define VK_HANJA 0x19
#define VK_KANJI 0x19
#define VK_ESCAPE 0x1B
#define VK_CONVERT 0x1C
#define VK_NONCONVERT 0x1D
#define VK_ACCEPT 0x1E
#define VK_MODECHANGE 0x1F
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_SLEEP 0x5F
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_BROWSER_BACK 0xA6
#define VK_BROWSER_FORWARD 0xA7
#define VK_BROWSER_REFRESH 0xA8
#define VK_BROWSER_STOP 0xA9
#define VK_BROWSER_SEARCH 0xAA
#define VK_BROWSER_FAVORITES 0xAB
#define VK_BROWSER_HOME 0xAC
#define VK_VOLUME_MUTE 0xAD
#define VK_VOLUME_DOWN 0xAE
#define VK_VOLUME_UP 0xAF
#define VK_MEDIA_NEXT_TRACK 0xB0
#define VK_MEDIA_PREV_TRACK 0xB1
#define VK_MEDIA_STOP 0xB2
#define VK_MEDIA_PLAY_PAUSE 0xB3
#define VK_LAUNCH_MAIL 0xB4
#define VK_LAUNCH_MEDIA_SELECT 0xB5
#define VK_LAUNCH_APP1 0xB6
#define VK_LAUNCH_APP2 0xB7
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2
#define VK_PROCESSKEY 0xE5
#define VK_PACKET 0xE7
#define VK_ATTN 0xF6
#define VK_CRSEL 0xF7
#define VK_EXSEL 0xF8
#define VK_EREOF 0xF9
#define VK_PLAY 0xFA
#define VK_ZOOM 0xFB
#define VK_NONAME 0xFC
#define VK_PA1 0xFD
#define VK_OEM_CLEAR 0xFE

// -------------------------------------------------------------------------------------------
// Windowing constants
// -------------------------------------------------------------------------------------------
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_WNDPROC (-4)
#define GWLP_HINSTANCE (-6)
#define GWLP_USERDATA (-21)
#define GWLP_ID (-12)
#define GCLP_HCURSOR (-12)
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW (WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_TRANSPARENT 0x00000020L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_LAYERED 0x00080000
#define WS_EX_NOACTIVATE 0x08000000L
#define WS_EX_APPWINDOW 0x00040000L
#define WS_EX_WINDOWEDGE 0x00000100L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_STATICEDGE 0x00020000L
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOCOPYBITS 0x0100
#define SWP_NOOWNERZORDER 0x0200
#define SWP_NOSENDCHANGING 0x0400
#define SWP_ASYNCWINDOWPOS 0x4000
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_MESSAGE ((HWND)-3)
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define GA_PARENT 1
#define GA_ROOT 2
#define GA_ROOTOWNER 3
#define GW_OWNER 4
#define GW_HWNDNEXT 2
#define MONITOR_DEFAULTTONULL 0x00000000
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define MONITORINFOF_PRIMARY 0x00000001
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXVSCROLL 2
#define SM_CYHSCROLL 3
#define SM_CYCAPTION 4
#define SM_CXBORDER 5
#define SM_CYBORDER 6
#define SM_CXCURSOR 13
#define SM_CYCURSOR 14
#define SM_CXFRAME 32
#define SM_CYFRAME 33
#define SM_CXSIZEFRAME SM_CXFRAME
#define SM_CYSIZEFRAME SM_CYFRAME
#define SM_SWAPBUTTON 23
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SM_CMONITORS 80
#define SM_CXPADDEDBORDER 92
#define CURSOR_SHOWING 0x00000001
#define CURSOR_SUPPRESSED 0x00000002
#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define IDC_IBEAM MAKEINTRESOURCEW(32513)
#define IDC_WAIT MAKEINTRESOURCEW(32514)
#define IDC_CROSS MAKEINTRESOURCEW(32515)
#define IDC_UPARROW MAKEINTRESOURCEW(32516)
#define IDC_SIZENWSE MAKEINTRESOURCEW(32642)
#define IDC_SIZENESW MAKEINTRESOURCEW(32643)
#define IDC_SIZEWE MAKEINTRESOURCEW(32644)
#define IDC_SIZENS MAKEINTRESOURCEW(32645)
#define IDC_SIZEALL MAKEINTRESOURCEW(32646)
#define IDC_NO MAKEINTRESOURCEW(32648)
#define IDC_HAND MAKEINTRESOURCEW(32649)
#define IDC_APPSTARTING MAKEINTRESOURCEW(32650)
#define IDC_HELP MAKEINTRESOURCEW(32651)
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#define LR_DEFAULTCOLOR 0x00000000
#define LR_DEFAULTSIZE 0x00000040
#define LR_LOADFROMFILE 0x00000010
#define LR_SHARED 0x00008000
#define LR_COPYFROMRESOURCE 0x00004000
#define LR_CREATEDIBSECTION 0x00002000
#define CS_HREDRAW 0x0002
#define CS_VREDRAW 0x0001
#define CS_OWNDC 0x0020
#define CW_USEDEFAULT ((int)0x80000000)
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define QS_ALLINPUT 0x04FF
#define MWMO_INPUTAVAILABLE 0x0004
#define SPI_GETMOUSESPEED 0x0070
#define SPI_SETMOUSESPEED 0x0071
#define SPI_GETKEYBOARDDELAY 0x0016
#define SPI_SETKEYBOARDDELAY 0x0017
#define SPI_GETKEYBOARDSPEED 0x000A
#define SPI_SETKEYBOARDSPEED 0x000B
#define SPI_GETFILTERKEYS 0x0032
#define SPI_SETFILTERKEYS 0x0033
#define SPI_GETWORKAREA 0x0030
#define SPIF_UPDATEINIFILE 0x0001
#define SPIF_SENDCHANGE 0x0002
#define SPIF_SENDWININICHANGE SPIF_SENDCHANGE
#define FKF_FILTERKEYSON 0x00000001
#define FKF_AVAILABLE 0x00000002
#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_YESNO 0x00000004L
#define MB_YESNOCANCEL 0x00000003L
#define MB_ICONERROR 0x00000010L
#define MB_ICONHAND 0x00000010L
#define MB_ICONSTOP 0x00000010L
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONWARNING 0x00000030L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONINFORMATION 0x00000040L
#define MB_TOPMOST 0x00040000L
#define MB_SETFOREGROUND 0x00010000L
#define MB_SYSTEMMODAL 0x00001000L
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_NOCHANGEDIR 0x00000008
#define OFN_EXPLORER 0x00080000
#define OFN_HIDEREADONLY 0x00000004
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_DONTADDTORECENT 0x02000000
#define OFN_ALLOWMULTISELECT 0x00000200
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_DIB 8
#define CF_UNICODETEXT 13
#define CF_DIBV5 17
#define SRCCOPY (DWORD)0x00CC0020
#define CAPTUREBLT (DWORD)0x40000000
#define PW_CLIENTONLY 0x00000001
#define PW_RENDERFULLCONTENT 0x00000002
#define BI_RGB 0L
#define DIB_RGB_COLORS 0
#define AC_SRC_OVER 0x00
#define AC_SRC_ALPHA 0x01
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define HORZRES 8
#define VERTRES 10
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define WHITE_BRUSH 0
#define USER_DEFAULT_SCREEN_DPI 96
#define KLF_ACTIVATE 0x00000001
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#define DWMWA_CLOAKED 14
#define CSIDL_PROFILE 0x0028
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_FLAG_CREATE 0x8000
#define KF_FLAG_DEFAULT 0
#define KF_FLAG_CREATE 0x00008000
#define SHGFP_TYPE_CURRENT 0
#define SE_ERR_NOASSOC 31
#define RT_RCDATA MAKEINTRESOURCEW(10)
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define USER_TIMER_MINIMUM 0x0000000A
#define VS_FF_DEBUG 0x00000001L
#define WAIT_IO_COMPLETION 0x000000C0L

// -------------------------------------------------------------------------------------------
// String helpers (MSVC CRT)
// -------------------------------------------------------------------------------------------
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _strdup strdup
#define _wcsdup wcsdup
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _snwprintf swprintf
#define _itoa_s(v, b, n, r) snprintf((b), (n), "%d", (int)(v))

template <size_t N>
inline int sprintf_s(char (&buffer)[N], const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int r = vsnprintf(buffer, N, format, args);
    va_end(args);
    return r;
}
inline int sprintf_s(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int r = vsnprintf(buffer, size, format, args);
    va_end(args);
    return r;
}
template <size_t N>
inline int _snprintf_s(char (&buffer)[N], size_t, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int r = vsnprintf(buffer, N, format, args);
    va_end(args);
    return r;
}
inline int _snprintf_s(char* buffer, size_t size, size_t, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int r = vsnprintf(buffer, size, format, args);
    va_end(args);
    return r;
}
// Note: MSVC's wide printf treats %s as a wide string; the shared sources only use numeric
// conversions with these.
template <size_t N>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int r = vswprintf(buffer, N, format, args);
    va_end(args);
    return r;
}
inline int swprintf_s(wchar_t* buffer, size_t size, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int r = vswprintf(buffer, size, format, args);
    va_end(args);
    return r;
}
inline int vsprintf_s(char* buffer, size_t size, const char* format, va_list args) { return vsnprintf(buffer, size, format, args); }
template <size_t N>
inline int vsprintf_s(char (&buffer)[N], const char* format, va_list args) { return vsnprintf(buffer, N, format, args); }
inline int strcpy_s(char* dst, size_t size, const char* src) {
    if (!dst || size == 0) return EINVAL;
    snprintf(dst, size, "%s", src ? src : "");
    return 0;
}
template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }
inline int strncpy_s(char* dst, size_t size, const char* src, size_t count) {
    if (!dst || size == 0) return EINVAL;
    size_t n = src ? strnlen(src, count == _TRUNCATE ? size - 1 : count) : 0;
    if (n >= size) n = size - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}
template <size_t N>
inline int strncpy_s(char (&dst)[N], const char* src, size_t count) { return strncpy_s(dst, N, src, count); }
inline int strcat_s(char* dst, size_t size, const char* src) {
    size_t len = strnlen(dst, size);
    if (len >= size) return EINVAL;
    snprintf(dst + len, size - len, "%s", src ? src : "");
    return 0;
}
template <size_t N>
inline int strcat_s(char (&dst)[N], const char* src) { return strcat_s(dst, N, src); }
inline int wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src) {
    if (!dst || size == 0) return EINVAL;
    size_t n = src ? wcsnlen(src, size - 1) : 0;
    if (n) wmemcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}
template <size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscpy_s(dst, N, src); }
inline int wcsncpy_s(wchar_t* dst, size_t size, const wchar_t* src, size_t count) {
    if (!dst || size == 0) return EINVAL;
    size_t n = src ? wcsnlen(src, count == _TRUNCATE ? size - 1 : count) : 0;
    if (n >= size) n = size - 1;
    if (n) wmemcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}
template <size_t N>
inline int wcsncpy_s(wchar_t (&dst)[N], const wchar_t* src, size_t count) { return wcsncpy_s(dst, N, src, count); }
inline int wcscat_s(wchar_t* dst, size_t size, const wchar_t* src) {
    size_t len = wcsnlen(dst, size);
    if (len >= size) return EINVAL;
    wcscpy_s(dst + len, size - len, src);
    return 0;
}
template <size_t N>
inline int wcscat_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscat_s(dst, N, src); }
inline int memcpy_s(void* dst, size_t dstSize, const void* src, size_t count) {
    if (count > dstSize) return ERANGE;
    memcpy(dst, src, count);
    return 0;
}
inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : EINVAL; }
inline int gmtime_s(struct tm* out, const time_t* t) { return gmtime_r(t, out) ? 0 : EINVAL; }
inline int _localtime64_s(struct tm* out, const time_t* t) { return localtime_s(out, t); }
int fopen_s(FILE** out, const char* path, const char* mode);
int _wfopen_s(FILE** out, const wchar_t* path, const wchar_t* mode);
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
inline int _wtoi(const wchar_t* s) { return (int)wcstol(s, nullptr, 10); }
inline unsigned short _byteswap_ushort(unsigned short v) { return __builtin_bswap16(v); }
inline unsigned long _byteswap_ulong(unsigned long v) { return __builtin_bswap32((uint32_t)v); }
inline unsigned long long _byteswap_uint64(unsigned long long v) { return __builtin_bswap64(v); }
// __debugbreak, _ReturnAddress and _AddressOfReturnAddress are clang builtins under -fms-extensions.
inline void YieldProcessor() {
#if defined(__aarch64__)
    __asm__ __volatile__("yield");
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}
#if !defined(__x86_64__)
// On x86_64 the real SSE2 intrinsic is used (see include/intrin.h).
#define _mm_pause() YieldProcessor()
#endif

// SEH is not available; translators become no-ops and __try/__except degrade to ordinary blocks.
struct _EXCEPTION_POINTERS;
typedef _EXCEPTION_POINTERS EXCEPTION_POINTERS;
typedef EXCEPTION_POINTERS* PEXCEPTION_POINTERS;
typedef void (*_se_translator_function)(unsigned int, EXCEPTION_POINTERS*);
inline _se_translator_function _set_se_translator(_se_translator_function) { return nullptr; }

// -------------------------------------------------------------------------------------------
// Emulated Win32 API (implemented in win32_compat.cpp / mac_platform.mm)
// -------------------------------------------------------------------------------------------
// Errors
DWORD GetLastError();
void SetLastError(DWORD error);
DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD messageId, DWORD languageId, LPSTR buffer, DWORD size, va_list* args);
HLOCAL LocalFree(HLOCAL mem);

// Time
void Sleep(DWORD milliseconds);
DWORD SleepEx(DWORD milliseconds, BOOL alertable);
DWORD GetTickCount();
ULONGLONG GetTickCount64();
BOOL QueryPerformanceCounter(LARGE_INTEGER* counter);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
void GetSystemTimeAsFileTime(LPFILETIME out);
void GetSystemTime(LPSYSTEMTIME out);
void GetLocalTime(LPSYSTEMTIME out);
BOOL FileTimeToLocalFileTime(const FILETIME* in, LPFILETIME out);
BOOL FileTimeToSystemTime(const FILETIME* in, LPSYSTEMTIME out);
BOOL SystemTimeToFileTime(const SYSTEMTIME* in, LPFILETIME out);
LONG CompareFileTime(const FILETIME* a, const FILETIME* b);
DWORD GetMessageTime();
UINT timeBeginPeriod(UINT period);
UINT timeEndPeriod(UINT period);

// Processes / threads / modules
DWORD GetCurrentProcessId();
DWORD GetCurrentThreadId();
HANDLE GetCurrentProcess();
HANDLE GetCurrentThread();
BOOL SetThreadPriority(HANDLE thread, int priority);
int GetThreadPriority(HANDLE thread);
BOOL SwitchToThread();
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
BOOL GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exitTime, LPFILETIME kernel, LPFILETIME user);
BOOL QueryFullProcessImageNameA(HANDLE process, DWORD flags, LPSTR name, PDWORD size);
BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR name, PDWORD size);
DWORD GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size);
DWORD GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
HMODULE GetModuleHandleW(LPCWSTR name);
HMODULE GetModuleHandleA(LPCSTR name);
#define GetModuleHandle GetModuleHandleW
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE* out);
BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* out);
#define GetModuleHandleEx GetModuleHandleExW
HMODULE LoadLibraryW(LPCWSTR path);
HMODULE LoadLibraryA(LPCSTR path);
HMODULE LoadLibraryExW(LPCWSTR path, HANDLE file, DWORD flags);
HMODULE LoadLibraryExA(LPCSTR path, HANDLE file, DWORD flags);
#define LoadLibrary LoadLibraryW
BOOL FreeLibrary(HMODULE module);
FARPROC GetProcAddress(HMODULE module, LPCSTR name);
BOOL DisableThreadLibraryCalls(HMODULE module);
SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length);
LPWSTR GetCommandLineW();
LPSTR GetCommandLineA();
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size);
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
BOOL SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value);
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
DWORD ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD size);
void OutputDebugStringA(LPCSTR text);
void OutputDebugStringW(LPCWSTR text);
#define OutputDebugString OutputDebugStringW
USHORT CaptureStackBackTrace(ULONG framesToSkip, ULONG framesToCapture, PVOID* backTrace, PULONG hash);
#define RtlCaptureStackBackTrace CaptureStackBackTrace

// Synchronisation objects (events/timers/mutexes); handles are closed with CloseHandle
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attrs, BOOL manualReset, BOOL initialState, LPCWSTR name);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attrs, BOOL manualReset, BOOL initialState, LPCSTR name);
#define CreateEvent CreateEventW
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES attrs, BOOL manualReset, LPCWSTR name);
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES attrs, LPCWSTR name, DWORD flags, DWORD access);
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* dueTime, LONG period, void* completion, LPVOID arg, BOOL resume);
BOOL CancelWaitableTimer(HANDLE timer);
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES attrs, BOOL initialOwner, LPCWSTR name);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attrs, BOOL initialOwner, LPCSTR name);
BOOL ReleaseMutex(HANDLE mutex);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES attrs, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags, LPDWORD threadId);
BOOL GetExitCodeThread(HANDLE thread, LPDWORD exitCode);
BOOL CloseHandle(HANDLE handle);
BOOL DuplicateHandle(HANDLE srcProcess, HANDLE src, HANDLE dstProcess, LPHANDLE dst, DWORD access, BOOL inherit, DWORD options);

// Files
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attrs, DWORD disposition, DWORD flags, HANDLE templ);
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attrs, DWORD disposition, DWORD flags, HANDLE templ);
#define CreateFile CreateFileW
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD toRead, LPDWORD read, LPOVERLAPPED overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD toWrite, LPDWORD written, LPOVERLAPPED overlapped);
BOOL FlushFileBuffers(HANDLE file);
BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER size);
DWORD GetFileSize(HANDLE file, LPDWORD high);
DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD method);
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPointer, DWORD method);
BOOL SetEndOfFile(HANDLE file);
BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
BOOL SetFileTime(HANDLE file, const FILETIME* creation, const FILETIME* access, const FILETIME* write);
DWORD GetFileAttributesW(LPCWSTR path);
DWORD GetFileAttributesA(LPCSTR path);
#define GetFileAttributes GetFileAttributesW
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info);
BOOL SetFileAttributesW(LPCWSTR path, DWORD attributes);
BOOL DeleteFileW(LPCWSTR path);
BOOL DeleteFileA(LPCSTR path);
#define DeleteFile DeleteFileW
BOOL MoveFileW(LPCWSTR from, LPCWSTR to);
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags);
BOOL ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID, LPVOID);
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists);
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES attrs);
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attrs);
BOOL RemoveDirectoryW(LPCWSTR path);
DWORD GetCurrentDirectoryW(DWORD size, LPWSTR buffer);
DWORD GetCurrentDirectoryA(DWORD size, LPSTR buffer);
BOOL SetCurrentDirectoryW(LPCWSTR path);
DWORD GetTempPathW(DWORD size, LPWSTR buffer);
DWORD GetFullPathNameW(LPCWSTR path, DWORD size, LPWSTR buffer, LPWSTR* filePart);
UINT GetWindowsDirectoryW(LPWSTR buffer, UINT size);
BOOL PathIsRelativeW(LPCWSTR path);
BOOL PathFileExistsW(LPCWSTR path);
BOOL PathFileExistsA(LPCSTR path);
LPWSTR PathFindFileNameW(LPCWSTR path);
LPWSTR PathFindExtensionW(LPCWSTR path);
BOOL PathRemoveFileSpecW(LPWSTR path);
HRESULT SHGetFolderPathW(HWND owner, int folder, HANDLE token, DWORD flags, LPWSTR path);
int SHCreateDirectoryExW(HWND owner, LPCWSTR path, const SECURITY_ATTRIBUTES* attrs);
HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR* path);
void CoTaskMemFree(LPVOID mem);
extern const KNOWNFOLDERID FOLDERID_Profile;
extern const KNOWNFOLDERID FOLDERID_RoamingAppData;
extern const KNOWNFOLDERID FOLDERID_LocalAppData;
extern const KNOWNFOLDERID FOLDERID_Desktop;
extern const KNOWNFOLDERID FOLDERID_Downloads;
extern const KNOWNFOLDERID FOLDERID_Pictures;
extern const KNOWNFOLDERID FOLDERID_Documents;
HANDLE CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES attrs, DWORD protect, DWORD maxHigh, DWORD maxLow, LPCWSTR name);
HANDLE OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name);
LPVOID MapViewOfFile(HANDLE mapping, DWORD access, DWORD offsetHigh, DWORD offsetLow, SIZE_T bytes);
BOOL UnmapViewOfFile(LPCVOID address);
#define FILE_MAP_ALL_ACCESS 0xF001F
#define FILE_MAP_READ 0x0004
#define FILE_MAP_WRITE 0x0002

// Text conversion
int MultiByteToWideChar(UINT codePage, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen);
int WideCharToMultiByte(UINT codePage, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR defaultChar, LPBOOL usedDefault);

// Resources (embedded with #embed, see cmake/macos/ToolscreenMacOS.cmake)
HRSRC FindResourceW(HMODULE module, LPCWSTR name, LPCWSTR type);
#define FindResource FindResourceW
HGLOBAL LoadResource(HMODULE module, HRSRC resource);
DWORD SizeofResource(HMODULE module, HRSRC resource);
LPVOID LockResource(HGLOBAL data);

// Global memory / clipboard
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
LPVOID GlobalLock(HGLOBAL mem);
BOOL GlobalUnlock(HGLOBAL mem);
HGLOBAL GlobalFree(HGLOBAL mem);
SIZE_T GlobalSize(HGLOBAL mem);
BOOL OpenClipboard(HWND owner);
BOOL CloseClipboard();
BOOL EmptyClipboard();
HANDLE SetClipboardData(UINT format, HANDLE mem);
HANDLE GetClipboardData(UINT format);
BOOL IsClipboardFormatAvailable(UINT format);

// Keyboard
SHORT GetAsyncKeyState(int vk);
SHORT GetKeyState(int vk);
BOOL GetKeyboardState(PBYTE state);
UINT MapVirtualKeyW(UINT code, UINT mapType);
UINT MapVirtualKeyA(UINT code, UINT mapType);
#define MapVirtualKey MapVirtualKeyW
UINT MapVirtualKeyExW(UINT code, UINT mapType, HKL layout);
int GetKeyNameTextA(LONG lParam, LPSTR buffer, int size);
int GetKeyNameTextW(LONG lParam, LPWSTR buffer, int size);
HKL GetKeyboardLayout(DWORD threadId);
SHORT VkKeyScanA(CHAR ch);
SHORT VkKeyScanW(WCHAR ch);
SHORT VkKeyScanExW(WCHAR ch, HKL layout);
SHORT VkKeyScanExA(CHAR ch, HKL layout);
int ToUnicode(UINT vk, UINT scan, const BYTE* keyState, LPWSTR buffer, int size, UINT flags);
int ToUnicodeEx(UINT vk, UINT scan, const BYTE* keyState, LPWSTR buffer, int size, UINT flags, HKL layout);
UINT SendInput(UINT count, LPINPUT inputs, int size);
HHOOK SetWindowsHookExW(int idHook, HOOKPROC proc, HINSTANCE module, DWORD threadId);
#define SetWindowsHookEx SetWindowsHookExW
BOOL UnhookWindowsHookEx(HHOOK hook);
LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam);
LPARAM GetMessageExtraInfo();
UINT GetRawInputData(HRAWINPUT rawInput, UINT command, LPVOID data, PUINT size, UINT headerSize);
BOOL RegisterRawInputDevices(const RAWINPUTDEVICE* devices, UINT count, UINT size);
UINT GetDoubleClickTime();

// Mouse / cursor
BOOL GetCursorPos(LPPOINT point);
BOOL SetCursorPos(int x, int y);
BOOL ClipCursor(const RECT* rect);
BOOL GetClipCursor(LPRECT rect);
HCURSOR SetCursor(HCURSOR cursor);
HCURSOR GetCursor();
int ShowCursor(BOOL show);
BOOL GetCursorInfo(PCURSORINFO info);
HCURSOR LoadCursorW(HINSTANCE instance, LPCWSTR name);
#define LoadCursor LoadCursorW
HANDLE LoadImageW(HINSTANCE instance, LPCWSTR name, UINT type, int cx, int cy, UINT flags);
HANDLE CopyImage(HANDLE image, UINT type, int cx, int cy, UINT flags);
BOOL GetIconInfo(HICON icon, PICONINFO info);
BOOL GetIconInfoExW(HICON icon, PICONINFOEXW info);
BOOL DestroyCursor(HCURSOR cursor);
BOOL DestroyIcon(HICON icon);
HWND SetCapture(HWND hwnd);
BOOL ReleaseCapture();
HWND GetCapture();
BOOL TrackMouseEvent(LPTRACKMOUSEEVENT track);

// Windows (the only real "window" is Minecraft's; HWND wraps its NSWindow)
LRESULT CallWindowProcW(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define CallWindowProc CallWindowProcW
LRESULT DefWindowProcW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define DefWindowProc DefWindowProcW
BOOL PostMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define PostMessage PostMessageW
LRESULT SendMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#define SendMessage SendMessageW
BOOL PostThreadMessageW(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL GetMessageW(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax);
BOOL PeekMessageW(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT remove);
BOOL TranslateMessage(const MSG* msg);
LRESULT DispatchMessageW(const MSG* msg);
void PostQuitMessage(int exitCode);
UINT RegisterWindowMessageA(LPCSTR name);
UINT RegisterWindowMessageW(LPCWSTR name);
LONG_PTR SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value);
LONG_PTR GetWindowLongPtrW(HWND hwnd, int index);
#define SetWindowLongPtr SetWindowLongPtrW
#define GetWindowLongPtr GetWindowLongPtrW
LONG GetWindowLongW(HWND hwnd, int index);
LONG SetWindowLongW(HWND hwnd, int index, LONG value);
#define GetWindowLong GetWindowLongW
#define SetWindowLong SetWindowLongW
ULONG_PTR GetClassLongPtrW(HWND hwnd, int index);
BOOL IsWindow(HWND hwnd);
BOOL IsWindowVisible(HWND hwnd);
BOOL IsIconic(HWND hwnd);
BOOL IsZoomed(HWND hwnd);
BOOL IsWindowEnabled(HWND hwnd);
HWND GetForegroundWindow();
BOOL SetForegroundWindow(HWND hwnd);
HWND GetActiveWindow();
HWND SetFocus(HWND hwnd);
HWND GetFocus();
HWND GetDesktopWindow();
HWND GetAncestor(HWND hwnd, UINT flags);
HWND GetParent(HWND hwnd);
HWND GetWindow(HWND hwnd, UINT cmd);
HWND FindWindowW(LPCWSTR className, LPCWSTR windowName);
BOOL EnumWindows(WNDENUMPROC proc, LPARAM lParam);
DWORD GetWindowThreadProcessId(HWND hwnd, LPDWORD processId);
int GetWindowTextW(HWND hwnd, LPWSTR buffer, int size);
int GetWindowTextA(HWND hwnd, LPSTR buffer, int size);
int GetWindowTextLengthW(HWND hwnd);
int GetClassNameW(HWND hwnd, LPWSTR buffer, int size);
int GetClassNameA(HWND hwnd, LPSTR buffer, int size);
HANDLE GetPropW(HWND hwnd, LPCWSTR name);
BOOL SetPropW(HWND hwnd, LPCWSTR name, HANDLE data);
HANDLE RemovePropW(HWND hwnd, LPCWSTR name);
BOOL GetClientRect(HWND hwnd, LPRECT rect);
BOOL GetWindowRect(HWND hwnd, LPRECT rect);
BOOL ClientToScreen(HWND hwnd, LPPOINT point);
BOOL ScreenToClient(HWND hwnd, LPPOINT point);
int MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count);
BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle);
BOOL SetWindowPos(HWND hwnd, HWND insertAfter, int x, int y, int cx, int cy, UINT flags);
BOOL MoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint);
BOOL ShowWindow(HWND hwnd, int cmd);
BOOL GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT* placement);
BOOL SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT* placement);
BOOL InvalidateRect(HWND hwnd, const RECT* rect, BOOL erase);
BOOL UpdateWindow(HWND hwnd);
BOOL RedrawWindow(HWND hwnd, const RECT* rect, HRGN region, UINT flags);
BOOL DestroyWindow(HWND hwnd);
HWND WindowFromDC(HDC dc);
HWND WindowFromPoint(POINT point);
ATOM RegisterClassExW(const WNDCLASSEXW* wc);
BOOL UnregisterClassW(LPCWSTR className, HINSTANCE instance);
HWND CreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
UINT_PTR SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc);
BOOL KillTimer(HWND hwnd, UINT_PTR id);
int MessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type);
int MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type);
#define MessageBox MessageBoxW
BOOL GetOpenFileNameW(LPOPENFILENAMEW ofn);
BOOL GetSaveFileNameW(LPOPENFILENAMEW ofn);
#define GetOpenFileName GetOpenFileNameW
HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show);
HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);
#define ShellExecute ShellExecuteW
BOOL MessageBeep(UINT type);

// Monitors / metrics / system parameters
HMONITOR MonitorFromWindow(HWND hwnd, DWORD flags);
HMONITOR MonitorFromRect(LPCRECT rect, DWORD flags);
HMONITOR MonitorFromPoint(POINT point, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR monitor, LPMONITORINFO info);
#define GetMonitorInfo GetMonitorInfoW
int GetSystemMetrics(int index);
int GetSystemMetricsForDpi(int index, UINT dpi);
UINT GetDpiForWindow(HWND hwnd);
UINT GetDpiForSystem();
BOOL SystemParametersInfoW(UINT action, UINT param, PVOID data, UINT winIni);
#define SystemParametersInfo SystemParametersInfoW
BOOL SetProcessDPIAware();

// GDI (minimal; real drawing goes through OpenGL)
HDC GetDC(HWND hwnd);
int ReleaseDC(HWND hwnd, HDC dc);
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateCompatibleBitmap(HDC dc, int cx, int cy);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO* info, UINT usage, void** bits, HANDLE section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj);
BOOL DeleteObject(HGDIOBJ obj);
HGDIOBJ GetStockObject(int obj);
int GetObjectW(HANDLE obj, int size, LPVOID out);
LONG GetBitmapBits(HBITMAP bitmap, LONG size, LPVOID bits);
int GetDIBits(HDC dc, HBITMAP bitmap, UINT start, UINT lines, LPVOID bits, BITMAPINFO* info, UINT usage);
BOOL BitBlt(HDC dst, int x, int y, int cx, int cy, HDC src, int x1, int y1, DWORD rop);
BOOL StretchBlt(HDC dst, int x, int y, int cx, int cy, HDC src, int x1, int y1, int cx1, int cy1, DWORD rop);
BOOL PrintWindow(HWND hwnd, HDC dc, UINT flags);
int FillRect(HDC dc, const RECT* rect, HBRUSH brush);
BOOL DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH brush, UINT flags);
int GetDeviceCaps(HDC dc, int index);
#define DI_NORMAL 0x0003
#define DI_MASK 0x0001
#define DI_IMAGE 0x0002
HRESULT DwmGetWindowAttribute(HWND hwnd, DWORD attribute, PVOID value, DWORD size);

// Registry (always reports "not found")
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_QUERY_VALUE 0x0001
#define REG_SZ 1
#define REG_DWORD 4
typedef LONG LSTATUS;
LSTATUS RegOpenKeyExW(HKEY key, LPCWSTR subKey, DWORD options, DWORD access, PHKEY result);
LSTATUS RegOpenKeyExA(HKEY key, LPCSTR subKey, DWORD options, DWORD access, PHKEY result);
LSTATUS RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LSTATUS RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LSTATUS RegEnumValueW(HKEY key, DWORD index, LPWSTR name, LPDWORD nameSize, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD dataSize);
LSTATUS RegCloseKey(HKEY key);

// File version info (not available; callers treat failure as "unknown")
DWORD GetFileVersionInfoSizeW(LPCWSTR path, LPDWORD handle);
BOOL GetFileVersionInfoW(LPCWSTR path, DWORD handle, DWORD size, LPVOID data);
BOOL VerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID* buffer, PUINT size);


// -------------------------------------------------------------------------------------------
// Structured exception handling shims
// -------------------------------------------------------------------------------------------
// __try/__except blocks run their body normally and never the handler. Hardware faults are not
// translated on macOS: the JVM owns SIGSEGV/SIGBUS for its own purposes and must not be pre-empted.
#define __try if (true)
#define __except(filter) else if (false)
#define __finally
#define __leave
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005L
#define EXCEPTION_DATATYPE_MISALIGNMENT 0x80000002L
#define EXCEPTION_BREAKPOINT 0x80000003L
#define EXCEPTION_SINGLE_STEP 0x80000004L
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED 0xC000008CL
#define EXCEPTION_FLT_DENORMAL_OPERAND 0xC000008DL
#define EXCEPTION_FLT_DIVIDE_BY_ZERO 0xC000008EL
#define EXCEPTION_FLT_INEXACT_RESULT 0xC000008FL
#define EXCEPTION_FLT_INVALID_OPERATION 0xC0000090L
#define EXCEPTION_FLT_OVERFLOW 0xC0000091L
#define EXCEPTION_FLT_STACK_CHECK 0xC0000092L
#define EXCEPTION_FLT_UNDERFLOW 0xC0000093L
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094L
#define EXCEPTION_INT_OVERFLOW 0xC0000095L
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096L
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006L
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001DL
#define EXCEPTION_NONCONTINUABLE_EXCEPTION 0xC0000025L
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDL
#define EXCEPTION_INVALID_DISPOSITION 0xC0000026L
#define EXCEPTION_GUARD_PAGE 0x80000001L
#define EXCEPTION_INVALID_HANDLE 0xC0000008L
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _CONTEXT { DWORD64 Pc; DWORD64 Sp; DWORD64 Fp; } CONTEXT, *PCONTEXT;
struct _EXCEPTION_POINTERS { PEXCEPTION_RECORD ExceptionRecord; PCONTEXT ContextRecord; };
typedef LONG(WINAPI* PTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS*);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef LONG(WINAPI* PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS*);
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);
PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler);
ULONG RemoveVectoredExceptionHandler(PVOID handle);
BOOL TerminateProcess(HANDLE process, UINT exitCode);
[[noreturn]] void ExitProcess(UINT exitCode);

#define NTSYSAPI
#define NTAPI
extern "C" PVOID RtlPcToFileHeader(PVOID pcValue, PVOID* baseOfImage);

// -------------------------------------------------------------------------------------------
// DbgHelp (symbolisation through dladdr; no line information)
// -------------------------------------------------------------------------------------------
#define MAX_SYM_NAME 2000
#define SYMOPT_UNDNAME 0x00000002
#define SYMOPT_DEFERRED_LOADS 0x00000004
#define SYMOPT_LOAD_LINES 0x00000010
#define SYMOPT_FAIL_CRITICAL_ERRORS 0x00000200
typedef struct _SYMBOL_INFO {
    ULONG SizeOfStruct; ULONG TypeIndex; ULONGLONG Reserved[2]; ULONG Index; ULONG Size; ULONGLONG ModBase;
    ULONG Flags; ULONGLONG Value; ULONGLONG Address; ULONG Register; ULONG Scope; ULONG Tag; ULONG NameLen; ULONG MaxNameLen;
    CHAR Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;
typedef struct _IMAGEHLP_LINE64 { DWORD SizeOfStruct; PVOID Key; DWORD LineNumber; CHAR* FileName; DWORD64 Address; } IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;
BOOL SymInitialize(HANDLE process, PCSTR searchPath, BOOL invade);
BOOL SymCleanup(HANDLE process);
DWORD SymGetOptions();
DWORD SymSetOptions(DWORD options);
BOOL SymFromAddr(HANDLE process, DWORD64 address, DWORD64* displacement, PSYMBOL_INFO symbol);
BOOL SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line);

// -------------------------------------------------------------------------------------------
// WGL equivalents (CGL underneath)
// -------------------------------------------------------------------------------------------
HGLRC wglGetCurrentContext();
HDC wglGetCurrentDC();
PROC wglGetProcAddress(LPCSTR name);
BOOL wglMakeCurrent(HDC dc, HGLRC context);
BOOL SwapBuffers(HDC dc);

// -------------------------------------------------------------------------------------------
// Assorted constants/functions referenced by shared code paths
// -------------------------------------------------------------------------------------------
#define ERROR_RESOURCE_DATA_NOT_FOUND 1812L
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813L
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814L
#define RDW_INVALIDATE 0x0001
#define RDW_INTERNALPAINT 0x0002
#define RDW_ERASE 0x0004
#define RDW_ALLCHILDREN 0x0080
#define RDW_UPDATENOW 0x0100
#define RDW_FRAME 0x0400
#define SMTO_NORMAL 0x0000
#define SMTO_BLOCK 0x0001
#define SMTO_ABORTIFHUNG 0x0002
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define R2_COPYPEN 13
#define LANG_ENGLISH 0x09
#define SUBLANG_ENGLISH_US 0x01
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_APPEND_DATA 0x0004
#define FNERR_SUBCLASSFAILURE 0x3001
#define FNERR_INVALIDFILENAME 0x3002
#define FNERR_BUFFERTOOSMALL 0x3003
#define CDERR_GENERALCODES 0x0000
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER { WORD bfType; DWORD bfSize; WORD bfReserved1; WORD bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER;
#pragma pack(pop)
#define GetObject GetObjectW
LRESULT SendMessageTimeoutW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT flags, UINT timeout, PDWORD_PTR result);
#define SendMessageTimeout SendMessageTimeoutW
int SetROP2(HDC dc, int mode);
HBRUSH CreateSolidBrush(COLORREF color);
DWORD CommDlgExtendedError();
int getenv_s(size_t* requiredSize, char* buffer, size_t size, const char* name);
int _wgetenv_s(size_t* requiredSize, wchar_t* buffer, size_t size, const wchar_t* name);
int _dupenv_s(char** buffer, size_t* size, const char* name);

// -------------------------------------------------------------------------------------------
// macOS platform services used by the compat layer and the mac-specific glue
// -------------------------------------------------------------------------------------------
namespace macplat {
// The GLFW window the game renders into, published by the hook layer.
HWND GameWindowHandle();
// Converts between UTF-8 and wchar_t (UTF-32) strings.
std::wstring Utf8ToWide(const char* s, size_t len = (size_t)-1);
std::string WideToUtf8(const wchar_t* s, size_t len = (size_t)-1);
// Maps a backslash-separated Windows-style path to a POSIX path.
std::string NativePathFromWide(const wchar_t* path);
std::string NativePathFromNarrow(const char* path);
} // namespace macplat

#endif // !_WIN32
