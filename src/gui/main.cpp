#include "../common/config.hpp"

#include <gtkmm.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace {

std::string logo_path() {
    const std::filesystem::path installed_path = MINTGLIDE_LOGO_INSTALL_PATH;
    if (std::filesystem::exists(installed_path)) {
        return installed_path.string();
    }
    return MINTGLIDE_LOGO_SOURCE_PATH;
}

class SettingsWindow : public Gtk::Window {
public:
    SettingsWindow() : root_(Gtk::ORIENTATION_VERTICAL, 8),
                       header_(Gtk::ORIENTATION_HORIZONTAL, 10),
                       heading_("Mintglide"), notebook_(),
                       save_button_("Save settings") {
        set_title("Mintglide");
        set_default_size(520, 540);
        set_border_width(12);
        auto logo = Gdk::Pixbuf::create_from_file(logo_path(), 48, 48, true);
        set_icon(logo);
        logo_.set(logo);
        heading_.set_halign(Gtk::ALIGN_START);
        header_.pack_start(logo_, Gtk::PACK_SHRINK);
        header_.pack_start(heading_, Gtk::PACK_SHRINK);
        config_ = mintglide::Config::load();
        create_behavior_tab();
        create_gesture_tab();
        create_sensitivity_tab();
        root_.pack_start(header_, Gtk::PACK_SHRINK);
        root_.pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);
        root_.pack_start(status_, Gtk::PACK_SHRINK);
        root_.pack_end(save_button_, Gtk::PACK_SHRINK);
        add(root_);
        save_button_.signal_clicked().connect(sigc::mem_fun(*this, &SettingsWindow::save));
        show_all_children();
    }

private:
    Gtk::Switch& add_switch(Gtk::Grid& grid, int row, const Glib::ustring& label,
                            bool active) {
        auto* text = Gtk::manage(new Gtk::Label(label));
        text->set_halign(Gtk::ALIGN_START);
        auto* toggle = Gtk::manage(new Gtk::Switch());
        toggle->set_active(active);
        grid.attach(*text, 0, row, 1, 1);
        grid.attach(*toggle, 1, row, 1, 1);
        switches_.push_back(toggle);
        return *toggle;
    }

    Gtk::Scale& add_scale(Gtk::Grid& grid, int row, const Glib::ustring& label,
                          double minimum, double maximum, double step, double value,
                          unsigned int digits = 2) {
        auto* text = Gtk::manage(new Gtk::Label(label));
        text->set_halign(Gtk::ALIGN_START);
        auto* scale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
        scale->set_range(minimum, maximum);
        scale->set_increments(step, step * 5);
        scale->set_value(value);
        scale->set_digits(digits);
        scale->set_hexpand(true);
        grid.attach(*text, 0, row, 1, 1);
        grid.attach(*scale, 1, row, 1, 1);
        scales_.push_back(scale);
        return *scale;
    }

    void create_behavior_tab() {
        auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
        auto* grid = Gtk::manage(new Gtk::Grid());
        grid->set_row_spacing(10);
        grid->set_column_spacing(18);
        grid->set_border_width(12);
        scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        scroll->add(*grid);
        add_switch(*grid, 0, "Tap to click", config_.tap_to_click);
        add_switch(*grid, 1, "Secondary click", config_.secondary_click);
        add_switch(*grid, 2, "Natural scrolling", config_.natural_scrolling);
        add_switch(*grid, 3, "Screen zoom (Ctrl + two-finger scroll)", config_.screen_zoom);
        auto* mode_label = Gtk::manage(new Gtk::Label("Secondary click gesture"));
        mode_label->set_halign(Gtk::ALIGN_START);
        secondary_mode_.append("two_finger", "Two-finger tap");
        secondary_mode_.append("corner", "Bottom-right corner");
        secondary_mode_.set_active_id(config_.secondary_click_mode);
        grid->attach(*mode_label, 0, 4, 1, 1);
        grid->attach(secondary_mode_, 1, 4, 1, 1);
        notebook_.append_page(*scroll, "Behavior");
    }

    void create_gesture_tab() {
        auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
        auto* grid = Gtk::manage(new Gtk::Grid());
        grid->set_row_spacing(10);
        grid->set_column_spacing(18);
        grid->set_border_width(12);
        scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        scroll->add(*grid);
        add_switch(*grid, 0, "Three-finger drag", config_.three_finger_drag);
        add_switch(*grid, 1, "Smart Zoom (two-finger double tap)", config_.smart_zoom);
        notebook_.append_page(*scroll, "Gestures");
    }

    void create_sensitivity_tab() {
        auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
        auto* grid = Gtk::manage(new Gtk::Grid());
        grid->set_row_spacing(14);
        grid->set_column_spacing(18);
        grid->set_border_width(12);
        scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        scroll->add(*grid);
        add_scale(*grid, 0, "Drag activation velocity threshold", 0.05, 5.0, 0.05,
                  config_.drag_velocity_threshold);
        add_scale(*grid, 1, "Scroll sensitivity", 0.1, 5.0, 0.1,
                  config_.scroll_sensitivity);
        add_scale(*grid, 2, "Drag sensitivity", 0.1, 5.0, 0.1,
                  config_.drag_sensitivity);
        add_scale(*grid, 3, "Tap duration (ms)", 50, 600, 10,
                  config_.tap_duration_ms, 0);
        add_scale(*grid, 4, "Drag activation threshold", 0.0005, 0.1, 0.0005,
                  config_.drag_threshold, 4);
        notebook_.append_page(*scroll, "Sensitivity");
    }

    void save() {
        std::size_t index = 0;
        config_.tap_to_click = switches_[index++]->get_active();
        config_.secondary_click = switches_[index++]->get_active();
        config_.natural_scrolling = switches_[index++]->get_active();
        config_.screen_zoom = switches_[index++]->get_active();
        config_.three_finger_drag = switches_[index++]->get_active();
        config_.smart_zoom = switches_[index++]->get_active();
        config_.secondary_click_mode = secondary_mode_.get_active_id().raw();
        index = 0;
        config_.drag_velocity_threshold = scales_[index++]->get_value();
        config_.scroll_sensitivity = scales_[index++]->get_value();
        config_.drag_sensitivity = scales_[index++]->get_value();
        config_.tap_duration_ms = static_cast<int>(scales_[index++]->get_value());
        config_.drag_threshold = scales_[index++]->get_value();

        try {
            config_.save();
            bool notified = false;
            bool notification_failed = false;
            const auto pid_path = mintglide::Config::pid_file();
            const int pid_fd = ::open(pid_path.c_str(), O_RDONLY | O_CLOEXEC);
            if (pid_fd >= 0) {
                const bool daemon_running = flock(pid_fd, LOCK_EX | LOCK_NB) < 0 &&
                    (errno == EWOULDBLOCK || errno == EAGAIN);
                std::ifstream pid_input(pid_path);
                pid_t pid = 0;
                if (daemon_running && pid_input >> pid && pid > 1) {
                    if (::kill(pid, SIGUSR1) == 0) {
                        notified = true;
                    } else {
                        status_.set_text("Settings saved, but daemon notification failed: " +
                                         std::string(std::strerror(errno)));
                        notification_failed = true;
                    }
                }
                ::close(pid_fd);
            }
            if (!notification_failed) {
                status_.set_text(notified
                    ? "Settings saved and daemon notified."
                    : "Settings saved. Daemon is not running; it will load them on startup.");
            }
        } catch (const std::exception& error) {
            status_.set_text("Save failed: " + std::string(error.what()));
        }
    }

    Gtk::Box root_;
    Gtk::Box header_;
    Gtk::Image logo_;
    Gtk::Label heading_;
    Gtk::Notebook notebook_;
    Gtk::Label status_;
    Gtk::Button save_button_;
    Gtk::ComboBoxText secondary_mode_;
    std::vector<Gtk::Switch*> switches_;
    std::vector<Gtk::Scale*> scales_;
    mintglide::Config config_;
};

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto application = Gtk::Application::create(
            argc, argv, "org.mintglide.Mintglide");
        SettingsWindow window;
        return application->run(window);
    } catch (const std::exception& error) {
        Gtk::MessageDialog dialog(error.what(), false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_CLOSE, true);
        dialog.set_title("Mintglide");
        dialog.run();
        return 1;
    }
}
