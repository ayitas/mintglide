#include "action_dispatcher.hpp"

#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <initializer_list>

namespace mintglide {

ActionDispatcher::ActionDispatcher() : display_(XOpenDisplay(nullptr)) {
    if (display_ != nullptr) {
        int event_base = 0;
        int error_base = 0;
        int major = 0;
        int minor = 0;
        if (!XTestQueryExtension(display_, &event_base, &error_base, &major, &minor)) {
            std::cerr << "The X server does not provide the XTest extension\n";
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }
}

ActionDispatcher::~ActionDispatcher() {
    release_drag();
    if (display_ != nullptr) {
        XCloseDisplay(display_);
    }
}

void ActionDispatcher::key_press(KeySym symbol) {
    const KeyCode code = XKeysymToKeycode(display_, symbol);
    if (code != 0) {
        XTestFakeKeyEvent(display_, code, True, CurrentTime);
    }
}

void ActionDispatcher::key_release(KeySym symbol) {
    const KeyCode code = XKeysymToKeycode(display_, symbol);
    if (code != 0) {
        XTestFakeKeyEvent(display_, code, False, CurrentTime);
    }
}

void ActionDispatcher::click(unsigned int button) {
    XTestFakeButtonEvent(display_, button, True, CurrentTime);
    XTestFakeButtonEvent(display_, button, False, CurrentTime);
    XFlush(display_);
}

void ActionDispatcher::scroll(unsigned int button) {
    XTestFakeButtonEvent(display_, button, True, CurrentTime);
    XTestFakeButtonEvent(display_, button, False, CurrentTime);
    XFlush(display_);
}

bool ActionDispatcher::control_pressed() const {
    if (display_ == nullptr) {
        return false;
    }
    char keys[32] {};
    XQueryKeymap(display_, keys);
    for (const KeySym symbol : {XK_Control_L, XK_Control_R}) {
        const KeyCode code = XKeysymToKeycode(display_, symbol);
        if (code != 0 &&
            (keys[code / 8] & static_cast<char>(1U << (code % 8))) != 0) {
            return true;
        }
    }
    return false;
}

void ActionDispatcher::release_drag() {
    if (display_ != nullptr && dragging_) {
        XTestFakeButtonEvent(display_, 1, False, CurrentTime);
        XFlush(display_);
        dragging_ = false;
    }
    cursor_valid_ = false;
}

void ActionDispatcher::dispatch(Action action, double dx, double dy,
                                double sensitivity) {
    if (display_ == nullptr) {
        return;
    }

    switch (action) {
    case Action::SmartZoom:
    case Action::ZoomUp:
        key_press(XK_Control_L);
        scroll(4);
        key_release(XK_Control_L);
        XFlush(display_);
        break;
    case Action::ZoomDown:
        key_press(XK_Control_L);
        scroll(5);
        key_release(XK_Control_L);
        XFlush(display_);
        break;
    case Action::PrimaryClick:
        click(1);
        break;
    case Action::SecondaryClick:
        click(3);
        break;
    case Action::ScrollUp:
        scroll(4);
        break;
    case Action::ScrollDown:
        scroll(5);
        break;
    case Action::DragStart:
        if (!dragging_) {
            XTestFakeButtonEvent(display_, 1, True, CurrentTime);
            dragging_ = true;
            Window root_return = 0;
            Window child_return = 0;
            int window_x = 0;
            int window_y = 0;
            unsigned int mask = 0;
            cursor_valid_ = XQueryPointer(
                display_, DefaultRootWindow(display_), &root_return, &child_return,
                &cursor_x_, &cursor_y_, &window_x, &window_y, &mask) != 0;
        }
        XFlush(display_);
        break;
    case Action::DragMove: {
        if (!dragging_) {
            break;
        }
        if (cursor_valid_) {
            const int screen = DefaultScreen(display_);
            cursor_x_ = std::clamp(
                cursor_x_ + static_cast<int>(dx * DisplayWidth(display_, screen) * sensitivity),
                0, DisplayWidth(display_, screen) - 1);
            cursor_y_ = std::clamp(
                cursor_y_ + static_cast<int>(dy * DisplayHeight(display_, screen) * sensitivity),
                0, DisplayHeight(display_, screen) - 1);
            XTestFakeMotionEvent(display_, screen, cursor_x_, cursor_y_, CurrentTime);
        }
        XFlush(display_);
        break;
    }
    case Action::DragEnd:
        release_drag();
        break;
    }
}

} // namespace mintglide
