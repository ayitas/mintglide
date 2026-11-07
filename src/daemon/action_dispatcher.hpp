#pragma once

#include <X11/Xlib.h>

namespace mintglide {

enum class Action {
    SmartZoom,
    ZoomUp,
    ZoomDown,
    PrimaryClick,
    SecondaryClick,
    ScrollUp,
    ScrollDown,
    DragStart,
    DragMove,
    DragEnd
};

class ActionDispatcher {
public:
    ActionDispatcher();
    ~ActionDispatcher();
    ActionDispatcher(const ActionDispatcher&) = delete;
    ActionDispatcher& operator=(const ActionDispatcher&) = delete;

    bool available() const noexcept { return display_ != nullptr; }
    void dispatch(Action action, double dx = 0.0, double dy = 0.0,
                  double sensitivity = 1.0);
    bool control_pressed() const;
    void release_drag();

private:
    void key_press(KeySym symbol);
    void key_release(KeySym symbol);
    void click(unsigned int button);
    void scroll(unsigned int button);

    Display* display_ = nullptr;
    bool dragging_ = false;
    bool cursor_valid_ = false;
    int cursor_x_ = 0;
    int cursor_y_ = 0;
};

} // namespace mintglide
