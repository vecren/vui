module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <imm.h>

namespace vui_win32_constants {
inline constexpr auto window_style = WS_OVERLAPPEDWINDOW;
inline constexpr auto default_position = CW_USEDEFAULT;
inline constexpr auto user_data = GWLP_USERDATA;
inline constexpr auto no_remove = PM_NOREMOVE;
inline constexpr auto show_normal = SW_SHOWNORMAL;
inline constexpr auto show_minimized = SW_MINIMIZE;
inline constexpr auto show_restore = SW_RESTORE;
inline constexpr auto show_hide = SW_HIDE;
inline constexpr auto set_no_zorder = SWP_NOZORDER;
inline constexpr auto set_no_activate = SWP_NOACTIVATE;
inline constexpr auto set_no_move = SWP_NOMOVE;
inline constexpr auto set_no_size = SWP_NOSIZE;
inline constexpr auto set_frame_changed = SWP_FRAMECHANGED;
inline constexpr auto nearest_monitor = MONITOR_DEFAULTTONEAREST;
inline constexpr auto pointer_leave = TME_LEAVE;
inline constexpr auto wait_failed = WAIT_FAILED;
inline constexpr auto wait_timeout = WAIT_TIMEOUT;
inline constexpr auto wait_object = WAIT_OBJECT_0;
inline constexpr auto infinite = INFINITE;
inline constexpr auto class_exists = ERROR_CLASS_ALREADY_EXISTS;
inline constexpr auto msg_create = WM_NCCREATE;
inline constexpr auto msg_close = WM_CLOSE;
inline constexpr auto msg_size = WM_SIZE;
inline constexpr auto msg_paint = WM_PAINT;
inline constexpr auto msg_erase = WM_ERASEBKGND;
inline constexpr auto msg_minmax = WM_GETMINMAXINFO;
inline constexpr auto msg_move = WM_MOVE;
inline constexpr auto msg_dpi = WM_DPICHANGED;
inline constexpr auto msg_focus = WM_SETFOCUS;
inline constexpr auto msg_blur = WM_KILLFOCUS;
inline constexpr auto msg_mouse_move = WM_MOUSEMOVE;
inline constexpr auto msg_mouse_leave = WM_MOUSELEAVE;
inline constexpr auto msg_left_down = WM_LBUTTONDOWN;
inline constexpr auto msg_left_up = WM_LBUTTONUP;
inline constexpr auto msg_right_down = WM_RBUTTONDOWN;
inline constexpr auto msg_right_up = WM_RBUTTONUP;
inline constexpr auto msg_middle_down = WM_MBUTTONDOWN;
inline constexpr auto msg_middle_up = WM_MBUTTONUP;
inline constexpr auto msg_extra_down = WM_XBUTTONDOWN;
inline constexpr auto msg_extra_up = WM_XBUTTONUP;
inline constexpr auto msg_wheel = WM_MOUSEWHEEL;
inline constexpr auto msg_hwheel = WM_MOUSEHWHEEL;
inline constexpr auto msg_capture_changed = WM_CAPTURECHANGED;
inline constexpr auto msg_key_down = WM_KEYDOWN;
inline constexpr auto msg_key_up = WM_KEYUP;
inline constexpr auto msg_sys_key_down = WM_SYSKEYDOWN;
inline constexpr auto msg_sys_key_up = WM_SYSKEYUP;
inline constexpr auto msg_char = WM_CHAR;
inline constexpr auto msg_unichar = WM_UNICHAR;
inline constexpr auto unicode_nochar = UNICODE_NOCHAR;
inline constexpr auto msg_ime_start = WM_IME_STARTCOMPOSITION;
inline constexpr auto msg_ime_composition = WM_IME_COMPOSITION;
inline constexpr auto msg_ime_end = WM_IME_ENDCOMPOSITION;
inline constexpr auto msg_ime_context = WM_IME_SETCONTEXT;
inline constexpr auto msg_ime_char = WM_IME_CHAR;
inline constexpr auto ime_result = GCS_RESULTSTR;
inline constexpr auto ime_preedit = GCS_COMPSTR;
inline constexpr auto ime_cursor = GCS_CURSORPOS;
inline constexpr auto ime_preedit_updates = GCS_COMPSTR | GCS_CURSORPOS | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_DELTASTART;
inline constexpr auto ime_all_updates = GCS_COMPREADSTR | GCS_COMPREADATTR | GCS_COMPREADCLAUSE | GCS_COMPSTR |
    GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART | GCS_RESULTREADSTR |
    GCS_RESULTREADCLAUSE | GCS_RESULTSTR | GCS_RESULTCLAUSE;
inline constexpr auto ime_hide_composition = ISC_SHOWUICOMPOSITIONWINDOW;
inline constexpr auto ime_cancel = NI_COMPOSITIONSTR;
inline constexpr auto ime_cancel_composition = CPS_CANCEL;
inline constexpr auto ime_candidate_exclude = CFS_EXCLUDE;
inline constexpr auto ime_composition_point = CFS_POINT;
inline constexpr auto msg_sys_char = WM_SYSCHAR;
inline constexpr auto msg_quit = WM_QUIT;
inline constexpr auto msg_command = WM_APP + 1;
inline constexpr auto size_minimized = SIZE_MINIMIZED;
inline constexpr auto mouse_back = XBUTTON1;
inline constexpr auto mouse_forward = XBUTTON2;
inline constexpr auto wheel_unit = WHEEL_DELTA;
inline constexpr auto key_shift = VK_SHIFT;
inline constexpr auto key_control = VK_CONTROL;
inline constexpr auto key_alt = VK_MENU;
inline constexpr auto key_left_meta = VK_LWIN;
inline constexpr auto key_right_meta = VK_RWIN;
inline constexpr auto key_tab = VK_TAB;
inline constexpr auto key_enter = VK_RETURN;
inline constexpr auto key_space = VK_SPACE;
inline constexpr auto key_escape = VK_ESCAPE;
inline constexpr auto key_left = VK_LEFT;
inline constexpr auto key_right = VK_RIGHT;
inline constexpr auto key_up = VK_UP;
inline constexpr auto key_down = VK_DOWN;
inline constexpr auto key_home = VK_HOME;
inline constexpr auto key_end = VK_END;
inline constexpr auto key_page_up = VK_PRIOR;
inline constexpr auto key_page_down = VK_NEXT;
inline constexpr auto key_backspace = VK_BACK;
inline constexpr auto key_delete = VK_DELETE;
inline auto per_monitor_v2() -> DPI_AWARENESS_CONTEXT { return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2; }
inline auto set_data(HWND window, LONG_PTR value) -> LONG_PTR { return SetWindowLongPtrW(window, GWLP_USERDATA, value); }
inline auto get_data(HWND window) -> LONG_PTR { return GetWindowLongPtrW(window, GWLP_USERDATA); }
inline auto get_style(HWND window) -> LONG_PTR { return GetWindowLongPtrW(window, GWL_STYLE); }
inline auto set_style(HWND window, LONG_PTR style) -> bool {
    SetLastError(0);
    auto previous = SetWindowLongPtrW(window, GWL_STYLE, style);
    return previous != 0 || GetLastError() == 0;
}
inline auto x_coordinate(LPARAM value) -> int { return GET_X_LPARAM(value); }
inline auto y_coordinate(LPARAM value) -> int { return GET_Y_LPARAM(value); }
inline auto wheel_delta(WPARAM value) -> int { return GET_WHEEL_DELTA_WPARAM(value); }
inline auto extra_button(WPARAM value) -> int { return GET_XBUTTON_WPARAM(value); }
inline auto arrow_cursor() -> HCURSOR { return LoadCursorW(nullptr, IDC_ARROW); }
}

export module vui.host:ffi.win32;
export namespace vui::host::ffi::win32 {
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
using ::vui_win32_constants::per_monitor_v2;
using ::vui_win32_constants::set_data;
using ::vui_win32_constants::get_data;
using ::vui_win32_constants::get_style;
using ::vui_win32_constants::set_style;
using ::vui_win32_constants::x_coordinate;
using ::vui_win32_constants::y_coordinate;
using ::vui_win32_constants::wheel_delta;
using ::vui_win32_constants::extra_button;
using ::vui_win32_constants::arrow_cursor;
using ::vui_win32_constants::window_style;
using ::vui_win32_constants::default_position;
using ::vui_win32_constants::user_data;
using ::vui_win32_constants::no_remove;
using ::vui_win32_constants::show_normal;
using ::vui_win32_constants::show_minimized;
using ::vui_win32_constants::show_restore;
using ::vui_win32_constants::show_hide;
using ::vui_win32_constants::set_no_zorder;
using ::vui_win32_constants::set_no_activate;
using ::vui_win32_constants::set_no_move;
using ::vui_win32_constants::set_no_size;
using ::vui_win32_constants::set_frame_changed;
using ::vui_win32_constants::nearest_monitor;
using ::vui_win32_constants::pointer_leave;
using ::vui_win32_constants::wait_failed;
using ::vui_win32_constants::wait_timeout;
using ::vui_win32_constants::wait_object;
using ::vui_win32_constants::infinite;
using ::vui_win32_constants::class_exists;
using ::vui_win32_constants::msg_create;
using ::vui_win32_constants::msg_close;
using ::vui_win32_constants::msg_size;
using ::vui_win32_constants::msg_paint;
using ::vui_win32_constants::msg_erase;
using ::vui_win32_constants::msg_minmax;
using ::vui_win32_constants::msg_move;
using ::vui_win32_constants::msg_dpi;
using ::vui_win32_constants::msg_focus;
using ::vui_win32_constants::msg_blur;
using ::vui_win32_constants::msg_mouse_move;
using ::vui_win32_constants::msg_mouse_leave;
using ::vui_win32_constants::msg_left_down;
using ::vui_win32_constants::msg_left_up;
using ::vui_win32_constants::msg_right_down;
using ::vui_win32_constants::msg_right_up;
using ::vui_win32_constants::msg_middle_down;
using ::vui_win32_constants::msg_middle_up;
using ::vui_win32_constants::msg_extra_down;
using ::vui_win32_constants::msg_extra_up;
using ::vui_win32_constants::msg_wheel;
using ::vui_win32_constants::msg_hwheel;
using ::vui_win32_constants::msg_capture_changed;
using ::vui_win32_constants::msg_key_down;
using ::vui_win32_constants::msg_key_up;
using ::vui_win32_constants::msg_sys_key_down;
using ::vui_win32_constants::msg_sys_key_up;
using ::vui_win32_constants::msg_char;
using ::vui_win32_constants::msg_unichar;
using ::vui_win32_constants::unicode_nochar;
using ::vui_win32_constants::msg_ime_start;
using ::vui_win32_constants::msg_ime_composition;
using ::vui_win32_constants::msg_ime_end;
using ::vui_win32_constants::msg_ime_context;
using ::vui_win32_constants::msg_ime_char;
using ::vui_win32_constants::ime_result;
using ::vui_win32_constants::ime_preedit;
using ::vui_win32_constants::ime_cursor;
using ::vui_win32_constants::ime_preedit_updates;
using ::vui_win32_constants::ime_all_updates;
using ::vui_win32_constants::ime_hide_composition;
using ::vui_win32_constants::ime_cancel;
using ::vui_win32_constants::ime_cancel_composition;
using ::vui_win32_constants::ime_candidate_exclude;
using ::vui_win32_constants::ime_composition_point;
using ::vui_win32_constants::msg_sys_char;
using ::vui_win32_constants::msg_quit;
using ::vui_win32_constants::msg_command;
using ::vui_win32_constants::size_minimized;
using ::vui_win32_constants::mouse_back;
using ::vui_win32_constants::mouse_forward;
using ::vui_win32_constants::wheel_unit;
using ::vui_win32_constants::key_shift;
using ::vui_win32_constants::key_control;
using ::vui_win32_constants::key_alt;
using ::vui_win32_constants::key_left_meta;
using ::vui_win32_constants::key_right_meta;
using ::vui_win32_constants::key_tab;
using ::vui_win32_constants::key_enter;
using ::vui_win32_constants::key_space;
using ::vui_win32_constants::key_escape;
using ::vui_win32_constants::key_left;
using ::vui_win32_constants::key_right;
using ::vui_win32_constants::key_up;
using ::vui_win32_constants::key_down;
using ::vui_win32_constants::key_home;
using ::vui_win32_constants::key_end;
using ::vui_win32_constants::key_page_up;
using ::vui_win32_constants::key_page_down;
using ::vui_win32_constants::key_backspace;
using ::vui_win32_constants::key_delete;
}
