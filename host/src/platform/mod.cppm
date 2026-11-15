module;
#include <rstd/macro.hpp>

module vui.host:platform;
#if RSTD_OS_WINDOWS
import :platform.win32;
#else
import :platform.wayland;
import :platform.x11;
#endif
