module;
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

inline constexpr auto alias_BTN_EXTRA = BTN_EXTRA;
inline constexpr auto alias_BTN_LEFT = BTN_LEFT;
inline constexpr auto alias_BTN_MIDDLE = BTN_MIDDLE;
inline constexpr auto alias_BTN_RIGHT = BTN_RIGHT;
inline constexpr auto alias_BTN_SIDE = BTN_SIDE;
inline constexpr auto alias_EAGAIN = EAGAIN;
inline constexpr auto alias_EINTR = EINTR;
inline constexpr auto alias_KEY_A = KEY_A;
inline constexpr auto alias_KEY_APOSTROPHE = KEY_APOSTROPHE;
inline constexpr auto alias_KEY_E = KEY_E;
inline constexpr auto alias_MAP_PRIVATE = MAP_PRIVATE;
inline constexpr auto alias_POLLIN = POLLIN;
inline constexpr auto alias_POLLNVAL = POLLNVAL;
inline constexpr auto alias_POLLOUT = POLLOUT;
inline constexpr auto alias_PROT_READ = PROT_READ;

#undef BTN_EXTRA
#undef BTN_LEFT
#undef BTN_MIDDLE
#undef BTN_RIGHT
#undef BTN_SIDE
#undef EAGAIN
#undef EINTR
#undef KEY_A
#undef KEY_APOSTROPHE
#undef KEY_E
#undef MAP_PRIVATE
#undef POLLIN
#undef POLLNVAL
#undef POLLOUT
#undef PROT_READ

module vui.host:platform.linux.ffi;
namespace ffi {
using ::pollfd;
using ::poll;
using ::eventfd;
using ::read;
using ::write;
using ::close;
using ::mmap;
using ::munmap;

inline constexpr auto BTN_EXTRA = alias_BTN_EXTRA;
inline constexpr auto BTN_LEFT = alias_BTN_LEFT;
inline constexpr auto BTN_MIDDLE = alias_BTN_MIDDLE;
inline constexpr auto BTN_RIGHT = alias_BTN_RIGHT;
inline constexpr auto BTN_SIDE = alias_BTN_SIDE;
inline constexpr auto EAGAIN = alias_EAGAIN;
inline constexpr auto EINTR = alias_EINTR;
inline constexpr auto KEY_A = alias_KEY_A;
inline constexpr auto KEY_APOSTROPHE = alias_KEY_APOSTROPHE;
inline constexpr auto KEY_E = alias_KEY_E;
inline constexpr auto MAP_PRIVATE = alias_MAP_PRIVATE;
inline constexpr auto POLLIN = alias_POLLIN;
inline constexpr auto POLLNVAL = alias_POLLNVAL;
inline constexpr auto POLLOUT = alias_POLLOUT;
inline constexpr auto PROT_READ = alias_PROT_READ;

inline constexpr auto error = POLLERR | POLLHUP | POLLNVAL;

inline constexpr auto disconnected = POLLERR | POLLHUP;

inline constexpr auto event_flags = EFD_CLOEXEC | EFD_NONBLOCK;

inline auto map_failed(void *p) { return p == MAP_FAILED; }
inline auto error_number() { return errno; }
inline auto make_pipe(int *fds) { return pipe2(fds, O_CLOEXEC); }
inline auto nonblocking(int fd) { auto flags = fcntl(fd, F_GETFL); return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0; }
inline auto block_sigpipe() { sigset_t signals; return sigemptyset(&signals) == 0 && sigaddset(&signals, SIGPIPE) == 0 && pthread_sigmask(SIG_BLOCK, &signals, nullptr) == 0; }
}
