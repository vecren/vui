module;
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

namespace vui_linux_constants {
inline constexpr auto input = POLLIN;
inline constexpr auto output = POLLOUT;
inline constexpr auto error = POLLERR | POLLHUP | POLLNVAL;
inline constexpr auto invalid = POLLNVAL;
inline constexpr auto disconnected = POLLERR | POLLHUP;
inline constexpr auto interrupted = EINTR;
inline constexpr auto again = EAGAIN;
inline constexpr auto event_flags = EFD_CLOEXEC | EFD_NONBLOCK;
inline constexpr auto button_left = BTN_LEFT;
inline constexpr auto button_right = BTN_RIGHT;
inline constexpr auto button_middle = BTN_MIDDLE;
inline constexpr auto button_back = BTN_SIDE;
inline constexpr auto button_forward = BTN_EXTRA;
inline constexpr auto key_a = KEY_A;
inline constexpr auto key_e = KEY_E;
inline constexpr auto key_apostrophe = KEY_APOSTROPHE;
inline constexpr auto map_read = PROT_READ;
inline constexpr auto map_private = MAP_PRIVATE;
inline auto map_failed(void *p) { return p == MAP_FAILED; }
inline auto error_number() { return errno; }
} // namespace vui_linux_constants

export module vui.host:ffi.linux;
export namespace vui::host::ffi::linux {
using ::pollfd;
using ::poll;
using ::eventfd;
using ::read;
using ::write;
using ::close;
using ::mmap;
using ::munmap;
using ::vui_linux_constants::input;
using ::vui_linux_constants::output;
using ::vui_linux_constants::error;
using ::vui_linux_constants::invalid;
using ::vui_linux_constants::disconnected;
using ::vui_linux_constants::interrupted;
using ::vui_linux_constants::again;
using ::vui_linux_constants::event_flags;
using ::vui_linux_constants::button_left;
using ::vui_linux_constants::button_right;
using ::vui_linux_constants::button_middle;
using ::vui_linux_constants::button_back;
using ::vui_linux_constants::button_forward;
using ::vui_linux_constants::key_a;
using ::vui_linux_constants::key_e;
using ::vui_linux_constants::key_apostrophe;
using ::vui_linux_constants::map_read;
using ::vui_linux_constants::map_private;
using ::vui_linux_constants::map_failed;
using ::vui_linux_constants::error_number;
}
