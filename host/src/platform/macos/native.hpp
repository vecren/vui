#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ffi {
struct MacosWindow;

enum class MacosEventKind {
    Move,
    Down,
    Up,
    Enter,
    Leave,
    Wheel,
    KeyDown,
    KeyUp,
    FocusGained,
    FocusLost,
    Close,
    Redraw
};
enum class MacosScrollPhase { Unknown, Begin, Update, End };
struct MacosEvent {
    MacosEventKind kind{};
    double x{}, y{}, delta_x{}, delta_y{}, timestamp{-1};
    uint16_t keycode{};
    int button{};
    unsigned modifiers{};
    bool repeat{}, precise{};
    MacosScrollPhase phase{};
    int momentum{-1};
};
struct MacosGeometry {
    double width{}, height{}, x{}, y{}, scale{1};
    uint32_t pixel_width{}, pixel_height{};
    bool minimized{}, suspended{}, fullscreen{};
};
struct MacosBytes {
    const char *data{""};
    size_t size{};
};
struct MacosTextTransaction {
    uint64_t session{};
    MacosBytes commit, preedit;
    bool has_commit{}, has_preedit{}, has_cursor{};
    uint32_t anchor{}, cursor{};
    bool cursor_visible{true}, cursor_range{};
    uint32_t delete_before{}, delete_after{};
    bool has_expected{};
    uint64_t expected_text{};
    uint32_t expected_anchor{}, expected_cursor{};
};
struct MacosCallbacks {
    void (*event)(void *, const MacosEvent &){};
    void (*geometry)(void *, const MacosGeometry &){};
    void (*text)(void *, const MacosTextTransaction &){};
};
struct MacosTextState {
    bool enabled{}, surrounding_present{}, reset{};
    uint64_t session{}, text{};
    MacosBytes surrounding;
    uint32_t anchor{}, cursor{};
    double x{}, y{}, width{}, height{};
};
struct MacosDisplay {
    uint64_t id{};
    double x{}, y{}, width{}, height{}, usable_x{}, usable_y{}, usable_width{}, usable_height{}, refresh_hz{};
    uint32_t pixel_width{}, pixel_height{};
};

bool macos_on_main_thread();
bool macos_post_empty_event();
void macos_drain_retired();
MacosWindow *macos_open(const char *title, uint32_t width, uint32_t height, void *context, MacosCallbacks callbacks);
// The owner stays alive until its native objects have been released on the main thread.
void macos_destroy(MacosWindow *window, void *owner, void (*destroy_owner)(void *));
void macos_deactivate(MacosWindow *window);
bool macos_pump(MacosWindow *window, uint32_t milliseconds);
bool macos_synchronize(MacosWindow *window);
const void *macos_metal_layer(const MacosWindow *window);
bool macos_resize(MacosWindow *window, uint32_t width, uint32_t height);
bool macos_set_minimum_size(MacosWindow *window, uint32_t width, uint32_t height);
bool macos_set_position(MacosWindow *window, double x, double y);
bool macos_set_fullscreen(MacosWindow *window, bool enabled);
bool macos_minimize(MacosWindow *window);
bool macos_restore(MacosWindow *window);
bool macos_displays(MacosWindow *window, void *context, void (*emit)(void *, const MacosDisplay &));
bool macos_set_text_input(MacosWindow *window, const MacosTextState &state);
bool macos_write_clipboard(MacosWindow *window, MacosBytes text);
} // namespace ffi
