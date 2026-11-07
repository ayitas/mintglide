#pragma once

#include "action_dispatcher.hpp"
#include "../common/config.hpp"

#include <linux/input.h>

#include <chrono>
#include <vector>

namespace mintglide {

class GestureEngine {
public:
    GestureEngine(ActionDispatcher& dispatcher, Config config,
                  int x_min, int x_max, int y_min, int y_max, int slots);

    void process(const input_event& event);
    void set_config(Config config);
    void reset();

private:
    struct Contact {
        int tracking_id = -1;
        double x = 0.0;
        double y = 0.0;
        double previous_x = 0.0;
        double previous_y = 0.0;
        double frame_dx = 0.0;
        double frame_dy = 0.0;
        bool has_previous_position = false;
    };

    using Clock = std::chrono::steady_clock;
    void finish_gesture(int remaining_contacts);
    void update_frame();
    int active_contacts() const;
    double normalized_x(int value) const;
    double normalized_y(int value) const;
    void begin_gesture(int fingers, double center_x, double center_y,
                       Clock::time_point now);

    ActionDispatcher& dispatcher_;
    Config config_;
    std::vector<Contact> contacts_;
    int current_slot_ = 0;
    int x_min_;
    int x_max_;
    int y_min_;
    int y_max_;
    int gesture_fingers_ = 0;
    double start_x_ = 0.0;
    double start_y_ = 0.0;
    double previous_x_ = 0.0;
    double previous_y_ = 0.0;
    double start_radius_ = 0.0;
    double latest_x_ = 0.0;
    double latest_y_ = 0.0;
    bool gesture_active_ = false;
    bool dragging_ = false;
    bool drag_decided_ = false;
    bool drag_candidate_logged_ = false;
    int scroll_finger_slot_ = -1;
    double accumulated_scroll_delta_ = 0.0;
    bool suppress_gesture_ = false;
    bool pending_tap_release_ = false;
    bool suppress_until_all_up_ = false;
    bool single_touch_active_ = false;
    double single_start_x_ = 0.0;
    double single_start_y_ = 0.0;
    Clock::time_point single_start_{};
    Clock::time_point gesture_start_{};
    Clock::time_point previous_frame_{};
    Clock::time_point last_two_finger_tap_{};
};

} // namespace mintglide
