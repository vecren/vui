module;
#include <rstd/macro.hpp>

module vui.host:platform;
#if RSTD_OS_WINDOWS
import :platform.win32;
#elif RSTD_OS_MACOS
import :platform.macos;
#else
import :platform.wayland;
import :platform.x11;
#endif
