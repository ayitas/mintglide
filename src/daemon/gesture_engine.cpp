#include "gesture_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

namespace mintglide {
namespace {

constexpr double kCornerX = 0.86;
constexpr double kCornerY = 0.88;
constexpr double kMotionNoiseThreshold = 0.00002;

} // namespace

GestureEngine::GestureEngine(ActionDispatcher& dispatcher, Config config,
                             int x_min, int x_max, int y_min, int y_max, int slots)
    : dispatcher_(dispatcher),
      config_(std::move(config)),
      contacts_(static_cast<std::size_t>(std::max(slots, 1))),
      x_min_(x_min),
      x_max_(x_max),
      y_min_(y_min),
      y_max_(y_max) {}

void GestureEngine::set_config(Config config) {
    if (!config.smart_zoom) {
        last_two_finger_tap_ = {};
    }
    config_ = std::move(config);
}

double GestureEngine::normalized_x(int value) const {
    return x_max_ > x_min_
        ? std::clamp(static_cast<double>(value - x_min_) / (x_max_ - x_min_), 0.0, 1.0)
        : 0.0;
}

double GestureEngine::normalized_y(int value) const {
    return y_max_ > y_min_
        ? std::clamp(static_cast<double>(value - y_min_) / (y_max_ - y_min_), 0.0, 1.0)
        : 0.0;
}

int GestureEngine::active_contacts() const {
    return static_cast<int>(std::count_if(
        contacts_.begin(), contacts_.end(),
        [](const Contact& contact) { return contact.tracking_id >= 0; }));
}

void GestureEngine::process(const input_event& event) {
    if (event.type == EV_ABS) {
        switch (event.code) {
        case ABS_MT_SLOT:
            current_slot_ = std::clamp(event.value, 0,
                                       static_cast<int>(contacts_.size()) - 1);
            break;
        case ABS_MT_TRACKING_ID:
            if (current_slot_ >= 0 &&
                current_slot_ < static_cast<int>(contacts_.size())) {
                Contact& contact = contacts_[static_cast<std::size_t>(current_slot_)];
                contact.tracking_id = event.value;
                contact.has_previous_position = false;
            }
            break;
        case ABS_MT_POSITION_X:
            contacts_[static_cast<std::size_t>(current_slot_)].x =
                normalized_x(event.value);
            break;
        case ABS_MT_POSITION_Y:
            contacts_[static_cast<std::size_t>(current_slot_)].y =
                normalized_y(event.value);
            break;
        default:
            break;
        }
    } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
        update_frame();
    }
}

void GestureEngine::update_frame() {
    for (auto& contact : contacts_) {
        contact.frame_dx = 0.0;
        contact.frame_dy = 0.0;
        if (contact.tracking_id < 0) {
            contact.has_previous_position = false;
            continue;
        }
        if (contact.has_previous_position) {
            contact.frame_dx = contact.x - contact.previous_x;
            contact.frame_dy = contact.y - contact.previous_y;
        } else {
            contact.has_previous_position = true;
        }
        contact.previous_x = contact.x;
        contact.previous_y = contact.y;
    }

    const int count = active_contacts();
    const auto now = Clock::now();
    if (count == 0) {
        if (gesture_active_) {
            finish_gesture(0);
        } else if (single_touch_active_) {
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - single_start_);
            const double movement = std::hypot(latest_x_ - single_start_x_,
                                               latest_y_ - single_start_y_);
            if (duration.count() <= config_.tap_duration_ms &&
                movement < config_.drag_threshold * 4.0) {
                if (config_.secondary_click && config_.secondary_click_mode == "corner" &&
                    latest_x_ >= kCornerX && latest_y_ >= kCornerY) {
                    dispatcher_.dispatch(Action::SecondaryClick);
                } else if (config_.tap_to_click) {
                    dispatcher_.dispatch(Action::PrimaryClick);
                }
            }
            single_touch_active_ = false;
        }
        suppress_until_all_up_ = false;
        previous_frame_ = {};
        return;
    }

    if (count > 3) {
        suppress_until_all_up_ = true;
        return;
    }

    double center_x = 0.0;
    double center_y = 0.0;
    for (const auto& contact : contacts_) {
        if (contact.tracking_id >= 0) {
            center_x += contact.x;
            center_y += contact.y;
        }
    }
    center_x /= count;
    center_y /= count;

    if (gesture_active_ && dragging_ && count != gesture_fingers_) {
        if (count < gesture_fingers_) {
            std::cerr << "gesture: keeping selection drag held with "
                      << count << " contact(s) still down\n";
        }
        gesture_fingers_ = count;
        previous_x_ = center_x;
        previous_y_ = center_y;
        previous_frame_ = now;
    } else if (gesture_active_ && count < gesture_fingers_) {
        if (count > 0 && !pending_tap_release_ && !suppress_gesture_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - gesture_start_);
            const double distance = std::hypot(latest_x_ - start_x_,
                                               latest_y_ - start_y_);
            if (elapsed.count() <= config_.tap_duration_ms &&
                distance < config_.drag_threshold * 4.0) {
                pending_tap_release_ = true;
                return;
            }
        }
        if (pending_tap_release_) {
            return;
        }
        if (dragging_) {
            std::cerr << "gesture: three-finger drag ended with "
                      << count << " contact(s) still down\n";
        }
        finish_gesture(count);
        suppress_until_all_up_ = count > 0;
        return;
    }

    if (count == 1) {
        const auto first = std::find_if(
            contacts_.begin(), contacts_.end(),
            [](const Contact& contact) { return contact.tracking_id >= 0; });
        if (!single_touch_active_ && !gesture_active_ && !suppress_until_all_up_) {
            single_touch_active_ = true;
            single_start_ = now;
            single_start_x_ = center_x;
            single_start_y_ = center_y;
        }
        if (first != contacts_.end()) {
            latest_x_ = first->x;
            latest_y_ = first->y;
        }
        previous_frame_ = now;
        return;
    }

    single_touch_active_ = false;
    if (suppress_until_all_up_) {
        return;
    }

    if (!gesture_active_ || count != gesture_fingers_) {
        begin_gesture(count, center_x, center_y, now);
    }

    if (gesture_active_) {
        const double dx = center_x - previous_x_;
        const double dy = center_y - previous_y_;
        latest_x_ = center_x;
        latest_y_ = center_y;

        double radius = 0.0;
        if (count == 3 && config_.three_finger_drag) {
            for (const auto& contact : contacts_) {
                if (contact.tracking_id >= 0) {
                    radius += std::hypot(contact.x - center_x, contact.y - center_y);
                }
            }
            radius /= count;
        }

        const double distance = std::hypot(center_x - start_x_, center_y - start_y_);
        const auto elapsed = std::chrono::duration<double>(now - gesture_start_).count();
        const double speed = elapsed > 0.0 ? distance / elapsed : 0.0;
        const bool contacts_move_together =
            std::abs(radius - start_radius_) <= std::max(0.01, start_radius_ * 0.25);
        if (count == 3 && config_.three_finger_drag && !drag_decided_ &&
            distance >= config_.drag_threshold) {
            if (!drag_candidate_logged_) {
                std::cerr << "gesture: three-finger drag candidate; distance="
                          << distance << ", speed=" << speed
                          << ", contacts_move_together="
                          << (contacts_move_together ? "yes" : "no") << '\n';
                drag_candidate_logged_ = true;
            }
            if (contacts_move_together) {
                drag_decided_ = true;
                if (speed < config_.drag_velocity_threshold) {
                    dispatcher_.dispatch(Action::DragStart);
                    dragging_ = true;
                    suppress_gesture_ = true;
                    std::cerr << "gesture: three-finger drag started\n";
                } else {
                    std::cerr << "gesture: drag not started; movement exceeded velocity threshold\n";
                }
            }
        } else if (dragging_ && count == 3) {
            int moving_slot = -1;
            double largest_motion = 0.0;
            double largest_dx = 0.0;
            double largest_dy = 0.0;
            for (std::size_t slot = 0; slot < contacts_.size(); ++slot) {
                const Contact& contact = contacts_[slot];
                if (contact.tracking_id < 0) {
                    continue;
                }
                const double motion = std::hypot(contact.frame_dx, contact.frame_dy);
                if (motion > largest_motion) {
                    largest_motion = motion;
                    largest_dx = contact.frame_dx;
                    largest_dy = contact.frame_dy;
                    moving_slot = static_cast<int>(slot);
                }
            }

            const double stationary_threshold = std::max(
                kMotionNoiseThreshold, config_.drag_threshold * 0.05);
            const bool vertical_candidate =
                std::abs(largest_dy) >= std::abs(largest_dx) * 1.5;
            bool other_contacts_stationary =
                moving_slot >= 0 && largest_motion >= stationary_threshold &&
                vertical_candidate;
            if (other_contacts_stationary) {
                for (std::size_t slot = 0; slot < contacts_.size(); ++slot) {
                    const Contact& contact = contacts_[slot];
                    if (contact.tracking_id >= 0 &&
                        static_cast<int>(slot) != moving_slot &&
                        std::hypot(contact.frame_dx, contact.frame_dy) >
                            std::max(stationary_threshold, largest_motion * 0.35)) {
                        other_contacts_stationary = false;
                        break;
                    }
                }
            }

            if (other_contacts_stationary && vertical_candidate) {
                if (scroll_finger_slot_ != moving_slot) {
                    std::cerr << "gesture: one-finger vertical scroll detected during drag\n";
                }
                scroll_finger_slot_ = moving_slot;
                accumulated_scroll_delta_ +=
                    contacts_[static_cast<std::size_t>(moving_slot)].frame_dy;
                const double scroll_threshold = 0.006 / config_.scroll_sensitivity;
                while (std::abs(accumulated_scroll_delta_) >= scroll_threshold) {
                    const bool up = accumulated_scroll_delta_ < 0.0;
                    if (config_.screen_zoom && dispatcher_.control_pressed()) {
                        dispatcher_.dispatch(up ? Action::ZoomUp : Action::ZoomDown);
                    } else {
                        dispatcher_.dispatch(up ? Action::ScrollUp : Action::ScrollDown);
                    }
                    std::cerr << "gesture: drag-scroll "
                              << (up ? "up" : "down") << '\n';
                    accumulated_scroll_delta_ +=
                        accumulated_scroll_delta_ > 0.0
                            ? -scroll_threshold : scroll_threshold;
                }
            } else if (scroll_finger_slot_ >= 0 &&
                       largest_motion < stationary_threshold) {
                accumulated_scroll_delta_ = 0.0;
            } else {
                scroll_finger_slot_ = -1;
                accumulated_scroll_delta_ = 0.0;
                dispatcher_.dispatch(Action::DragMove, dx, dy, config_.drag_sensitivity);
            }
        }

        if (count == 2) {
            if (previous_frame_ != Clock::time_point{}) {
                const double scroll_delta = center_y - previous_y_;
                const double threshold = 0.006 / config_.scroll_sensitivity;
                if (std::abs(scroll_delta) >= threshold) {
                    const bool up = config_.natural_scrolling
                        ? scroll_delta > 0.0 : scroll_delta < 0.0;
                    if (config_.screen_zoom && dispatcher_.control_pressed()) {
                        dispatcher_.dispatch(up ? Action::ZoomUp : Action::ZoomDown);
                    } else {
                        dispatcher_.dispatch(up ? Action::ScrollUp : Action::ScrollDown);
                    }
                    previous_y_ = center_y;
                }
            }
        }
        previous_x_ = center_x;
        previous_y_ = center_y;
        previous_frame_ = now;
    }
}

void GestureEngine::begin_gesture(int fingers, double center_x, double center_y,
                                  Clock::time_point now) {
    gesture_active_ = true;
    gesture_fingers_ = fingers;
    start_x_ = previous_x_ = latest_x_ = center_x;
    start_y_ = previous_y_ = latest_y_ = center_y;
    start_radius_ = 0.0;
    if (fingers == 3 && config_.three_finger_drag) {
        for (const auto& contact : contacts_) {
            if (contact.tracking_id >= 0) {
                start_radius_ += std::hypot(contact.x - center_x, contact.y - center_y);
            }
        }
        start_radius_ /= fingers;
    }
    gesture_start_ = now;
    previous_frame_ = now;
    drag_decided_ = false;
    suppress_gesture_ = false;
    pending_tap_release_ = false;
    drag_candidate_logged_ = false;
    if (fingers == 3) {
        std::cerr << "gesture: three-finger contact recognized\n";
    }
}

void GestureEngine::finish_gesture(int remaining_contacts) {
    if (!gesture_active_) {
        return;
    }

    if (dragging_) {
        dispatcher_.dispatch(Action::DragEnd);
        dragging_ = false;
        std::cerr << "gesture: three-finger drag released; all contacts lifted\n";
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - gesture_start_);
    const double distance = std::hypot(latest_x_ - start_x_, latest_y_ - start_y_);
    if (!suppress_gesture_ && remaining_contacts == 0 &&
        elapsed.count() <= config_.tap_duration_ms &&
        distance < config_.drag_threshold * 4.0) {
        if (gesture_fingers_ == 2) {
            const auto since_previous = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - last_two_finger_tap_);
            const bool is_double_tap =
                last_two_finger_tap_ != Clock::time_point{} &&
                since_previous.count() <= config_.tap_duration_ms * 2;
            if (is_double_tap && config_.smart_zoom) {
                dispatcher_.dispatch(Action::SmartZoom);
                last_two_finger_tap_ = {};
            } else if (config_.secondary_click &&
                       config_.secondary_click_mode == "two_finger") {
                dispatcher_.dispatch(Action::SecondaryClick);
            }
            if (config_.smart_zoom && !is_double_tap) {
                last_two_finger_tap_ = Clock::now();
            }
        }
    }

    gesture_active_ = false;
    gesture_fingers_ = 0;
    suppress_gesture_ = false;
    pending_tap_release_ = false;
    drag_decided_ = false;
    previous_frame_ = {};
    if (remaining_contacts == 0) {
        suppress_until_all_up_ = false;
        pending_tap_release_ = false;
    }
}

void GestureEngine::reset() {
    dispatcher_.release_drag();
    for (auto& contact : contacts_) {
        contact.tracking_id = -1;
    }
    gesture_active_ = false;
    dragging_ = false;
    drag_decided_ = false;
    drag_candidate_logged_ = false;
    scroll_finger_slot_ = -1;
    accumulated_scroll_delta_ = 0.0;
    single_touch_active_ = false;
    suppress_until_all_up_ = false;
    gesture_fingers_ = 0;
    previous_frame_ = {};
}

} // namespace mintglide
