#include "action_dispatcher.hpp"
#include "gesture_engine.hpp"
#include "input_device.hpp"
#include "../common/config.hpp"

#include <libudev.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

volatile sig_atomic_t reload_requested = 0;
volatile sig_atomic_t stop_requested = 0;

void handle_reload(int) {
    reload_requested = 1;
}

void handle_stop(int) {
    stop_requested = 1;
}

class PidFile {
public:
    PidFile() {
        const auto path = mintglide::Config::pid_file();
        std::filesystem::create_directories(path.parent_path());
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            throw std::runtime_error("Cannot write daemon PID file: " + path.string());
        }
        if (flock(fd_, LOCK_EX | LOCK_NB) < 0) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("mintglide-daemon is already running");
        }
        if (fchmod(fd_, 0600) < 0) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("Cannot secure daemon PID file: " + path.string());
        }
        if (ftruncate(fd_, 0) < 0 ||
            dprintf(fd_, "%ld\n", static_cast<long>(::getpid())) < 0) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("Failed to write daemon PID file: " + path.string());
        }
        path_ = path;
    }

    ~PidFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

private:
    std::filesystem::path path_;
    int fd_ = -1;
};

bool configure_axis(libevdev* device, unsigned int code, int& minimum, int& maximum) {
    const input_absinfo* info = libevdev_get_abs_info(device, code);
    if (info == nullptr || info->maximum <= info->minimum) {
        return false;
    }
    minimum = info->minimum;
    maximum = info->maximum;
    return true;
}

} // namespace

int main() {
    struct sigaction reload_action {};
    reload_action.sa_handler = handle_reload;
    sigemptyset(&reload_action.sa_mask);
    if (sigaction(SIGUSR1, &reload_action, nullptr) < 0) {
        std::cerr << "Cannot install SIGUSR1 handler: " << std::strerror(errno) << '\n';
        return 1;
    }

    struct sigaction stop_action {};
    stop_action.sa_handler = handle_stop;
    sigemptyset(&stop_action.sa_mask);
    if (sigaction(SIGINT, &stop_action, nullptr) < 0 ||
        sigaction(SIGTERM, &stop_action, nullptr) < 0) {
        std::cerr << "Cannot install shutdown signal handlers: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    struct sigaction child_action {};
    child_action.sa_handler = SIG_IGN;
    sigemptyset(&child_action.sa_mask);
    if (sigaction(SIGCHLD, &child_action, nullptr) < 0) {
        std::cerr << "Cannot configure child process reaping: "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    try {
        PidFile pid_file;
        mintglide::Config config = mintglide::Config::load();
        mintglide::ActionDispatcher dispatcher;
        if (!dispatcher.available()) {
            throw std::runtime_error("Cannot connect to the X11 display; start this daemon in your desktop session");
        }

        udev* context = udev_new();
        if (context == nullptr) {
            throw std::runtime_error("udev_new failed");
        }
        udev_monitor* monitor = udev_monitor_new_from_netlink(context, "udev");
        if (monitor == nullptr ||
            udev_monitor_filter_add_match_subsystem_devtype(monitor, "input", nullptr) < 0 ||
            udev_monitor_enable_receiving(monitor) < 0) {
            if (monitor != nullptr) {
                udev_monitor_unref(monitor);
            }
            udev_unref(context);
            throw std::runtime_error("Cannot create udev input-device monitor");
        }

        std::unique_ptr<mintglide::InputDevice> input;
        std::unique_ptr<mintglide::GestureEngine> engine;
        auto connect_device = [&]() {
            input = mintglide::InputDevice::find_apple_trackpad(context);
            engine.reset();
            if (!input) {
                std::cerr << "Apple multitouch trackpad not found; waiting for a device\n";
                return;
            }
            int x_min = 0;
            int x_max = 0;
            int y_min = 0;
            int y_max = 0;
            if (!configure_axis(input->device(), ABS_MT_POSITION_X, x_min, x_max) ||
                !configure_axis(input->device(), ABS_MT_POSITION_Y, y_min, y_max)) {
                std::cerr << "Trackpad does not expose usable multitouch position ranges\n";
                input.reset();
                return;
            }
            const int slots = input->slot_count();
            engine = std::make_unique<mintglide::GestureEngine>(
                dispatcher, config, x_min, x_max, y_min, y_max, slots);
        };

        connect_device();
        std::cerr << "mintglide-daemon started (SIGUSR1 reload, SIGINT/SIGTERM stop)\n";

        while (!stop_requested) {
            if (reload_requested) {
                reload_requested = 0;
                try {
                    config = mintglide::Config::load();
                    if (engine) {
                        engine->set_config(config);
                    }
                    std::cerr << "Configuration reloaded\n";
                } catch (const std::exception& error) {
                    std::cerr << "Configuration reload failed: " << error.what() << '\n';
                }
            }

            pollfd descriptors[2] {};
            nfds_t count = 1;
            descriptors[0].fd = udev_monitor_get_fd(monitor);
            descriptors[0].events = POLLIN;
            if (input) {
                descriptors[1].fd = input->fd();
                descriptors[1].events = POLLIN;
                count = 2;
            }

            const int ready = poll(descriptors, count, -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("poll failed: ") + std::strerror(errno));
            }

            if (descriptors[0].revents & POLLIN) {
                udev_device* event_device = udev_monitor_receive_device(monitor);
                if (event_device != nullptr) {
                    const char* action = udev_device_get_action(event_device);
                    if (action != nullptr &&
                        (std::strcmp(action, "add") == 0 || std::strcmp(action, "remove") == 0)) {
                        connect_device();
                    }
                    udev_device_unref(event_device);
                }
            }

            if (input && descriptors[1].revents & (POLLIN | POLLERR | POLLHUP)) {
                bool disconnected = false;
                input_event event {};
                int status = 0;
                for (;;) {
                    status = libevdev_next_event(
                        input->device(), LIBEVDEV_READ_FLAG_NORMAL, &event);
                    if (status == 0) {
                        engine->process(event);
                        continue;
                    }
                    if (status == LIBEVDEV_READ_STATUS_SYNC) {
                        engine->reset();
                        while ((status = libevdev_next_event(
                                    input->device(), LIBEVDEV_READ_FLAG_SYNC, &event)) == 0) {
                            engine->process(event);
                        }
                        if (status == -EAGAIN) {
                            status = 0;
                        }
                    }
                    break;
                }
                if (status != -EAGAIN && status != -EINTR && status != 0) {
                    std::cerr << "Trackpad disconnected or read failed: "
                              << std::strerror(-status) << '\n';
                    disconnected = true;
                }
                if (disconnected) {
                    engine->reset();
                    input.reset();
                    engine.reset();
                }
            }
        }

        if (engine) {
            engine->reset();
        }
        udev_monitor_unref(monitor);
        udev_unref(context);
    } catch (const std::exception& error) {
        std::cerr << "mintglide-daemon: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
