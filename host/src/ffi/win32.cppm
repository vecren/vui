module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <imm.h>

inline const auto alias_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
inline constexpr auto alias_GET_X_LPARAM(LPARAM value) -> int { return GET_X_LPARAM(value); }
inline constexpr auto alias_GET_Y_LPARAM(LPARAM value) -> int { return GET_Y_LPARAM(value); }
inline constexpr auto alias_GET_WHEEL_DELTA_WPARAM(WPARAM value) -> int { return GET_WHEEL_DELTA_WPARAM(value); }
inline constexpr auto alias_GET_XBUTTON_WPARAM(WPARAM value) -> int { return GET_XBUTTON_WPARAM(value); }
inline constexpr auto alias_CFS_EXCLUDE = CFS_EXCLUDE;
inline constexpr auto alias_CFS_POINT = CFS_POINT;
inline constexpr auto alias_CPS_CANCEL = CPS_CANCEL;
inline constexpr auto alias_CW_USEDEFAULT = CW_USEDEFAULT;
inline constexpr auto alias_ERROR_CLASS_ALREADY_EXISTS = ERROR_CLASS_ALREADY_EXISTS;
inline constexpr auto alias_GCS_COMPSTR = GCS_COMPSTR;
inline constexpr auto alias_GCS_CURSORPOS = GCS_CURSORPOS;
inline constexpr auto alias_GCS_RESULTSTR = GCS_RESULTSTR;
inline constexpr auto alias_GWLP_USERDATA = GWLP_USERDATA;
inline constexpr auto alias_INFINITE = INFINITE;
inline constexpr auto alias_ISC_SHOWUICOMPOSITIONWINDOW = ISC_SHOWUICOMPOSITIONWINDOW;
inline constexpr auto alias_MONITOR_DEFAULTTONEAREST = MONITOR_DEFAULTTONEAREST;
inline constexpr auto alias_NI_COMPOSITIONSTR = NI_COMPOSITIONSTR;
inline constexpr auto alias_PM_NOREMOVE = PM_NOREMOVE;
inline constexpr auto alias_SIZE_MINIMIZED = SIZE_MINIMIZED;
inline constexpr auto alias_SWP_FRAMECHANGED = SWP_FRAMECHANGED;
inline constexpr auto alias_SWP_NOACTIVATE = SWP_NOACTIVATE;
inline constexpr auto alias_SWP_NOMOVE = SWP_NOMOVE;
inline constexpr auto alias_SWP_NOSIZE = SWP_NOSIZE;
inline constexpr auto alias_SWP_NOZORDER = SWP_NOZORDER;
inline constexpr auto alias_SW_HIDE = SW_HIDE;
inline constexpr auto alias_SW_MINIMIZE = SW_MINIMIZE;
inline constexpr auto alias_SW_RESTORE = SW_RESTORE;
inline constexpr auto alias_SW_SHOWNORMAL = SW_SHOWNORMAL;
inline constexpr auto alias_TME_LEAVE = TME_LEAVE;
inline constexpr auto alias_UNICODE_NOCHAR = UNICODE_NOCHAR;
inline constexpr auto alias_VK_BACK = VK_BACK;
inline constexpr auto alias_VK_CONTROL = VK_CONTROL;
inline constexpr auto alias_VK_DELETE = VK_DELETE;
inline constexpr auto alias_VK_DOWN = VK_DOWN;
inline constexpr auto alias_VK_END = VK_END;
inline constexpr auto alias_VK_ESCAPE = VK_ESCAPE;
inline constexpr auto alias_VK_HOME = VK_HOME;
inline constexpr auto alias_VK_LEFT = VK_LEFT;
inline constexpr auto alias_VK_LWIN = VK_LWIN;
inline constexpr auto alias_VK_MENU = VK_MENU;
inline constexpr auto alias_VK_NEXT = VK_NEXT;
inline constexpr auto alias_VK_PRIOR = VK_PRIOR;
inline constexpr auto alias_VK_RETURN = VK_RETURN;
inline constexpr auto alias_VK_RIGHT = VK_RIGHT;
inline constexpr auto alias_VK_RWIN = VK_RWIN;
inline constexpr auto alias_VK_SHIFT = VK_SHIFT;
inline constexpr auto alias_VK_SPACE = VK_SPACE;
inline constexpr auto alias_VK_TAB = VK_TAB;
inline constexpr auto alias_VK_UP = VK_UP;
inline constexpr auto alias_WAIT_FAILED = WAIT_FAILED;
inline constexpr auto alias_WAIT_OBJECT_0 = WAIT_OBJECT_0;
inline constexpr auto alias_WAIT_TIMEOUT = WAIT_TIMEOUT;
inline constexpr auto alias_WHEEL_DELTA = WHEEL_DELTA;
inline constexpr auto alias_WM_CAPTURECHANGED = WM_CAPTURECHANGED;
inline constexpr auto alias_WM_CHAR = WM_CHAR;
inline constexpr auto alias_WM_CLOSE = WM_CLOSE;
inline constexpr auto alias_WM_DPICHANGED = WM_DPICHANGED;
inline constexpr auto alias_WM_ERASEBKGND = WM_ERASEBKGND;
inline constexpr auto alias_WM_GETMINMAXINFO = WM_GETMINMAXINFO;
inline constexpr auto alias_WM_IME_CHAR = WM_IME_CHAR;
inline constexpr auto alias_WM_IME_COMPOSITION = WM_IME_COMPOSITION;
inline constexpr auto alias_WM_IME_ENDCOMPOSITION = WM_IME_ENDCOMPOSITION;
inline constexpr auto alias_WM_IME_SETCONTEXT = WM_IME_SETCONTEXT;
inline constexpr auto alias_WM_IME_STARTCOMPOSITION = WM_IME_STARTCOMPOSITION;
inline constexpr auto alias_WM_KEYDOWN = WM_KEYDOWN;
inline constexpr auto alias_WM_KEYUP = WM_KEYUP;
inline constexpr auto alias_WM_KILLFOCUS = WM_KILLFOCUS;
inline constexpr auto alias_WM_LBUTTONDOWN = WM_LBUTTONDOWN;
inline constexpr auto alias_WM_LBUTTONUP = WM_LBUTTONUP;
inline constexpr auto alias_WM_MBUTTONDOWN = WM_MBUTTONDOWN;
inline constexpr auto alias_WM_MBUTTONUP = WM_MBUTTONUP;
inline constexpr auto alias_WM_MOUSEHWHEEL = WM_MOUSEHWHEEL;
inline constexpr auto alias_WM_MOUSELEAVE = WM_MOUSELEAVE;
inline constexpr auto alias_WM_MOUSEMOVE = WM_MOUSEMOVE;
inline constexpr auto alias_WM_MOUSEWHEEL = WM_MOUSEWHEEL;
inline constexpr auto alias_WM_MOVE = WM_MOVE;
inline constexpr auto alias_WM_NCCREATE = WM_NCCREATE;
inline constexpr auto alias_WM_PAINT = WM_PAINT;
inline constexpr auto alias_WM_QUIT = WM_QUIT;
inline constexpr auto alias_WM_RBUTTONDOWN = WM_RBUTTONDOWN;
inline constexpr auto alias_WM_RBUTTONUP = WM_RBUTTONUP;
inline constexpr auto alias_WM_SETFOCUS = WM_SETFOCUS;
inline constexpr auto alias_WM_SIZE = WM_SIZE;
inline constexpr auto alias_WM_SYSCHAR = WM_SYSCHAR;
inline constexpr auto alias_WM_SYSKEYDOWN = WM_SYSKEYDOWN;
inline constexpr auto alias_WM_SYSKEYUP = WM_SYSKEYUP;
inline constexpr auto alias_WM_UNICHAR = WM_UNICHAR;
inline constexpr auto alias_WM_XBUTTONDOWN = WM_XBUTTONDOWN;
inline constexpr auto alias_WM_XBUTTONUP = WM_XBUTTONUP;
inline constexpr auto alias_WS_OVERLAPPEDWINDOW = WS_OVERLAPPEDWINDOW;
inline constexpr auto alias_XBUTTON1 = XBUTTON1;
inline constexpr auto alias_XBUTTON2 = XBUTTON2;

#undef CFS_EXCLUDE
#undef CFS_POINT
#undef CPS_CANCEL
#undef CW_USEDEFAULT
#undef ERROR_CLASS_ALREADY_EXISTS
#undef GCS_COMPSTR
#undef GCS_CURSORPOS
#undef GCS_RESULTSTR
#undef GWLP_USERDATA
#undef INFINITE
#undef ISC_SHOWUICOMPOSITIONWINDOW
#undef MONITOR_DEFAULTTONEAREST
#undef NI_COMPOSITIONSTR
#undef PM_NOREMOVE
#undef SIZE_MINIMIZED
#undef SWP_FRAMECHANGED
#undef SWP_NOACTIVATE
#undef SWP_NOMOVE
#undef SWP_NOSIZE
#undef SWP_NOZORDER
#undef SW_HIDE
#undef SW_MINIMIZE
#undef SW_RESTORE
#undef SW_SHOWNORMAL
#undef TME_LEAVE
#undef UNICODE_NOCHAR
#undef VK_BACK
#undef VK_CONTROL
#undef VK_DELETE
#undef VK_DOWN
#undef VK_END
#undef VK_ESCAPE
#undef VK_HOME
#undef VK_LEFT
#undef VK_LWIN
#undef VK_MENU
#undef VK_NEXT
#undef VK_PRIOR
#undef VK_RETURN
#undef VK_RIGHT
#undef VK_RWIN
#undef VK_SHIFT
#undef VK_SPACE
#undef VK_TAB
#undef VK_UP
#undef WAIT_FAILED
#undef WAIT_OBJECT_0
#undef WAIT_TIMEOUT
#undef WHEEL_DELTA
#undef WM_CAPTURECHANGED
#undef WM_CHAR
#undef WM_CLOSE
#undef WM_DPICHANGED
#undef WM_ERASEBKGND
#undef WM_GETMINMAXINFO
#undef WM_IME_CHAR
#undef WM_IME_COMPOSITION
#undef WM_IME_ENDCOMPOSITION
#undef WM_IME_SETCONTEXT
#undef WM_IME_STARTCOMPOSITION
#undef WM_KEYDOWN
#undef WM_KEYUP
#undef WM_KILLFOCUS
#undef WM_LBUTTONDOWN
#undef WM_LBUTTONUP
#undef WM_MBUTTONDOWN
#undef WM_MBUTTONUP
#undef WM_MOUSEHWHEEL
#undef WM_MOUSELEAVE
#undef WM_MOUSEMOVE
#undef WM_MOUSEWHEEL
#undef WM_MOVE
#undef WM_NCCREATE
#undef WM_PAINT
#undef WM_QUIT
#undef WM_RBUTTONDOWN
#undef WM_RBUTTONUP
#undef WM_SETFOCUS
#undef WM_SIZE
#undef WM_SYSCHAR
#undef WM_SYSKEYDOWN
#undef WM_SYSKEYUP
#undef WM_UNICHAR
#undef WM_XBUTTONDOWN
#undef WM_XBUTTONUP
#undef WS_OVERLAPPEDWINDOW
#undef XBUTTON1
#undef XBUTTON2

#undef GET_X_LPARAM

#undef GET_Y_LPARAM

#undef GET_WHEEL_DELTA_WPARAM

#undef GET_XBUTTON_WPARAM

#undef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

module vui.host:ffi.win32;
namespace ffi {
using ::BOOL; using ::DWORD; using ::HANDLE; using ::HINSTANCE; using ::HMODULE;
using ::HWND; using ::LONG_PTR; using ::LPARAM; using ::LRESULT; using ::MSG;
using ::POINT; using ::RECT; using ::TRACKMOUSEEVENT; using ::UINT; using ::WPARAM;
using ::WNDCLASSEXW; using ::CREATESTRUCTW;
using ::MINMAXINFO; using ::MONITORINFO; using ::WINDOWPLACEMENT;
using ::HIMC; using ::COMPOSITIONFORM; using ::CANDIDATEFORM;
using ::CreateEventW; using ::SetEvent; using ::ResetEvent; using ::CloseHandle;
using ::CreateThread; using ::WaitForSingleObject; using ::WaitForMultipleObjects;
using ::GetCurrentThreadId; using ::GetLastError; using ::GetModuleHandleW;
using ::RegisterClassExW; using ::CreateWindowExW; using ::DestroyWindow;
using ::DefWindowProcW; using ::PeekMessageW; using ::GetMessageW;
using ::TranslateMessage; using ::DispatchMessageW; using ::PostThreadMessageW;
using ::PostMessageW; using ::ShowWindow; using ::GetClientRect; using ::GetWindowRect;
using ::GetDpiForWindow; using ::SetThreadDpiAwarenessContext; using ::AdjustWindowRectExForDpi;
using ::SetWindowPos; using ::IsIconic; using ::GetMessageTime; using ::GetKeyState;
using ::ScreenToClient; using ::TrackMouseEvent; using ::SetCapture; using ::GetCapture;
using ::ReleaseCapture; using ::ValidateRect; using ::GetDpiForSystem;
using ::GetWindowPlacement; using ::SetWindowPlacement; using ::MonitorFromWindow; using ::GetMonitorInfoW;
using ::ImmGetContext; using ::ImmReleaseContext; using ::ImmGetCompositionStringW;
using ::ImmSetCompositionWindow; using ::ImmSetCandidateWindow; using ::ImmNotifyIME;

inline constexpr auto CFS_EXCLUDE = alias_CFS_EXCLUDE;
inline constexpr auto CFS_POINT = alias_CFS_POINT;
inline constexpr auto CPS_CANCEL = alias_CPS_CANCEL;
inline constexpr auto CW_USEDEFAULT = alias_CW_USEDEFAULT;
inline constexpr auto ERROR_CLASS_ALREADY_EXISTS = alias_ERROR_CLASS_ALREADY_EXISTS;
inline constexpr auto GCS_COMPSTR = alias_GCS_COMPSTR;
inline constexpr auto GCS_CURSORPOS = alias_GCS_CURSORPOS;
inline constexpr auto GCS_RESULTSTR = alias_GCS_RESULTSTR;
inline constexpr auto GWLP_USERDATA = alias_GWLP_USERDATA;
inline constexpr auto INFINITE = alias_INFINITE;
inline constexpr auto ISC_SHOWUICOMPOSITIONWINDOW = alias_ISC_SHOWUICOMPOSITIONWINDOW;
inline constexpr auto MONITOR_DEFAULTTONEAREST = alias_MONITOR_DEFAULTTONEAREST;
inline constexpr auto NI_COMPOSITIONSTR = alias_NI_COMPOSITIONSTR;
inline constexpr auto PM_NOREMOVE = alias_PM_NOREMOVE;
inline constexpr auto SIZE_MINIMIZED = alias_SIZE_MINIMIZED;
inline constexpr auto SWP_FRAMECHANGED = alias_SWP_FRAMECHANGED;
inline constexpr auto SWP_NOACTIVATE = alias_SWP_NOACTIVATE;
inline constexpr auto SWP_NOMOVE = alias_SWP_NOMOVE;
inline constexpr auto SWP_NOSIZE = alias_SWP_NOSIZE;
inline constexpr auto SWP_NOZORDER = alias_SWP_NOZORDER;
inline constexpr auto SW_HIDE = alias_SW_HIDE;
inline constexpr auto SW_MINIMIZE = alias_SW_MINIMIZE;
inline constexpr auto SW_RESTORE = alias_SW_RESTORE;
inline constexpr auto SW_SHOWNORMAL = alias_SW_SHOWNORMAL;
inline constexpr auto TME_LEAVE = alias_TME_LEAVE;
inline constexpr auto UNICODE_NOCHAR = alias_UNICODE_NOCHAR;
inline constexpr auto VK_BACK = alias_VK_BACK;
inline constexpr auto VK_CONTROL = alias_VK_CONTROL;
inline constexpr auto VK_DELETE = alias_VK_DELETE;
inline constexpr auto VK_DOWN = alias_VK_DOWN;
inline constexpr auto VK_END = alias_VK_END;
inline constexpr auto VK_ESCAPE = alias_VK_ESCAPE;
inline constexpr auto VK_HOME = alias_VK_HOME;
inline constexpr auto VK_LEFT = alias_VK_LEFT;
inline constexpr auto VK_LWIN = alias_VK_LWIN;
inline constexpr auto VK_MENU = alias_VK_MENU;
inline constexpr auto VK_NEXT = alias_VK_NEXT;
inline constexpr auto VK_PRIOR = alias_VK_PRIOR;
inline constexpr auto VK_RETURN = alias_VK_RETURN;
inline constexpr auto VK_RIGHT = alias_VK_RIGHT;
inline constexpr auto VK_RWIN = alias_VK_RWIN;
inline constexpr auto VK_SHIFT = alias_VK_SHIFT;
inline constexpr auto VK_SPACE = alias_VK_SPACE;
inline constexpr auto VK_TAB = alias_VK_TAB;
inline constexpr auto VK_UP = alias_VK_UP;
inline constexpr auto WAIT_FAILED = alias_WAIT_FAILED;
inline constexpr auto WAIT_OBJECT_0 = alias_WAIT_OBJECT_0;
inline constexpr auto WAIT_TIMEOUT = alias_WAIT_TIMEOUT;
inline constexpr auto WHEEL_DELTA = alias_WHEEL_DELTA;
inline constexpr auto WM_CAPTURECHANGED = alias_WM_CAPTURECHANGED;
inline constexpr auto WM_CHAR = alias_WM_CHAR;
inline constexpr auto WM_CLOSE = alias_WM_CLOSE;
inline constexpr auto WM_DPICHANGED = alias_WM_DPICHANGED;
inline constexpr auto WM_ERASEBKGND = alias_WM_ERASEBKGND;
inline constexpr auto WM_GETMINMAXINFO = alias_WM_GETMINMAXINFO;
inline constexpr auto WM_IME_CHAR = alias_WM_IME_CHAR;
inline constexpr auto WM_IME_COMPOSITION = alias_WM_IME_COMPOSITION;
inline constexpr auto WM_IME_ENDCOMPOSITION = alias_WM_IME_ENDCOMPOSITION;
inline constexpr auto WM_IME_SETCONTEXT = alias_WM_IME_SETCONTEXT;
inline constexpr auto WM_IME_STARTCOMPOSITION = alias_WM_IME_STARTCOMPOSITION;
inline constexpr auto WM_KEYDOWN = alias_WM_KEYDOWN;
inline constexpr auto WM_KEYUP = alias_WM_KEYUP;
inline constexpr auto WM_KILLFOCUS = alias_WM_KILLFOCUS;
inline constexpr auto WM_LBUTTONDOWN = alias_WM_LBUTTONDOWN;
inline constexpr auto WM_LBUTTONUP = alias_WM_LBUTTONUP;
inline constexpr auto WM_MBUTTONDOWN = alias_WM_MBUTTONDOWN;
inline constexpr auto WM_MBUTTONUP = alias_WM_MBUTTONUP;
inline constexpr auto WM_MOUSEHWHEEL = alias_WM_MOUSEHWHEEL;
inline constexpr auto WM_MOUSELEAVE = alias_WM_MOUSELEAVE;
inline constexpr auto WM_MOUSEMOVE = alias_WM_MOUSEMOVE;
inline constexpr auto WM_MOUSEWHEEL = alias_WM_MOUSEWHEEL;
inline constexpr auto WM_MOVE = alias_WM_MOVE;
inline constexpr auto WM_NCCREATE = alias_WM_NCCREATE;
inline constexpr auto WM_PAINT = alias_WM_PAINT;
inline constexpr auto WM_QUIT = alias_WM_QUIT;
inline constexpr auto WM_RBUTTONDOWN = alias_WM_RBUTTONDOWN;
inline constexpr auto WM_RBUTTONUP = alias_WM_RBUTTONUP;
inline constexpr auto WM_SETFOCUS = alias_WM_SETFOCUS;
inline constexpr auto WM_SIZE = alias_WM_SIZE;
inline constexpr auto WM_SYSCHAR = alias_WM_SYSCHAR;
inline constexpr auto WM_SYSKEYDOWN = alias_WM_SYSKEYDOWN;
inline constexpr auto WM_SYSKEYUP = alias_WM_SYSKEYUP;
inline constexpr auto WM_UNICHAR = alias_WM_UNICHAR;
inline constexpr auto WM_XBUTTONDOWN = alias_WM_XBUTTONDOWN;
inline constexpr auto WM_XBUTTONUP = alias_WM_XBUTTONUP;
inline constexpr auto WS_OVERLAPPEDWINDOW = alias_WS_OVERLAPPEDWINDOW;
inline constexpr auto XBUTTON1 = alias_XBUTTON1;
inline constexpr auto XBUTTON2 = alias_XBUTTON2;

inline constexpr auto GET_X_LPARAM = alias_GET_X_LPARAM;

inline constexpr auto GET_Y_LPARAM = alias_GET_Y_LPARAM;

inline constexpr auto GET_WHEEL_DELTA_WPARAM = alias_GET_WHEEL_DELTA_WPARAM;

inline constexpr auto GET_XBUTTON_WPARAM = alias_GET_XBUTTON_WPARAM;

inline const auto DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = alias_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;

inline constexpr auto ime_preedit_updates = GCS_COMPSTR | GCS_CURSORPOS | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_DELTASTART;
inline constexpr auto ime_all_updates = GCS_COMPREADSTR | GCS_COMPREADATTR | GCS_COMPREADCLAUSE | GCS_COMPSTR |
    GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART | GCS_RESULTREADSTR |
    GCS_RESULTREADCLAUSE | GCS_RESULTSTR | GCS_RESULTCLAUSE;

inline constexpr auto msg_command = WM_APP + 1;

inline auto set_data(HWND window, LONG_PTR value) -> LONG_PTR { return SetWindowLongPtrW(window, GWLP_USERDATA, value); }
inline auto get_data(HWND window) -> LONG_PTR { return GetWindowLongPtrW(window, GWLP_USERDATA); }
inline auto get_style(HWND window) -> LONG_PTR { return GetWindowLongPtrW(window, GWL_STYLE); }
inline auto set_style(HWND window, LONG_PTR style) -> bool {
    SetLastError(0);
    auto previous = SetWindowLongPtrW(window, GWL_STYLE, style);
    return previous != 0 || GetLastError() == 0;
}
inline auto arrow_cursor() -> HCURSOR { return LoadCursorW(nullptr, IDC_ARROW); }

}
