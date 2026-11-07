#include "input_device.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace mintglide {
namespace {

bool has_text(const char* text, const char* needle) {
    if (text == nullptr) {
        return false;
    }
    std::string haystack(text);
    std::string search(needle);
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::transform(search.begin(), search.end(), search.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return haystack.find(search) != std::string::npos;
}

bool is_apple_trackpad(udev_device* device) {
    const char* vendor = udev_device_get_property_value(device, "ID_VENDOR_ID");
    const char* touchpad = udev_device_get_property_value(device, "ID_INPUT_TOUCHPAD");
    const char* name = udev_device_get_property_value(device, "ID_MODEL_FROM_DATABASE");
    if (name == nullptr) {
        name = udev_device_get_sysattr_value(device, "name");
    }
    if (vendor != nullptr && std::strcmp(vendor, "05ac") == 0 &&
        touchpad != nullptr && std::strcmp(touchpad, "1") == 0) {
        return true;
    }
    return has_text(name, "bcm5974") || has_text(name, "apple internal trackpad");
}

} // namespace

InputDevice::~InputDevice() {
    close();
}

void InputDevice::close() {
    if (device_ != nullptr) {
        libevdev_free(device_);
        device_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool InputDevice::open(const std::string& path) {
    close();
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        std::cerr << "Cannot open " << path << ": " << std::strerror(errno) << '\n';
        return false;
    }
    const int result = libevdev_new_from_fd(fd_, &device_);
    if (result < 0) {
        std::cerr << "Cannot initialize evdev device " << path << ": "
                  << std::strerror(-result) << '\n';
        close();
        return false;
    }
    std::cerr << "Listening on " << path << " (" << libevdev_get_name(device_) << ")\n";
    return true;
}

int InputDevice::slot_count() const {
    if (device_ == nullptr ||
        !libevdev_has_event_code(device_, EV_ABS, ABS_MT_SLOT)) {
        return 0;
    }
    const input_absinfo* info = libevdev_get_abs_info(device_, ABS_MT_SLOT);
    return info == nullptr ? 0 : info->maximum - info->minimum + 1;
}

std::unique_ptr<InputDevice> InputDevice::find_apple_trackpad(udev* context) {
    udev_enumerate* enumerate = udev_enumerate_new(context);
    if (enumerate == nullptr) {
        throw std::runtime_error("udev_enumerate_new failed");
    }
    udev_enumerate_add_match_subsystem(enumerate, "input");
    if (udev_enumerate_scan_devices(enumerate) < 0) {
        udev_enumerate_unref(enumerate);
        throw std::runtime_error("Failed to enumerate input devices");
    }

    std::unique_ptr<InputDevice> result;
    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, devices) {
        const char* syspath = udev_list_entry_get_name(entry);
        udev_device* device = udev_device_new_from_syspath(context, syspath);
        if (device == nullptr) {
            continue;
        }
        const char* devnode = udev_device_get_devnode(device);
        if (devnode != nullptr && is_apple_trackpad(device)) {
            auto candidate = std::make_unique<InputDevice>();
            if (candidate->open(devnode) &&
                libevdev_has_event_code(candidate->device(), EV_ABS, ABS_MT_SLOT) &&
                libevdev_has_event_code(candidate->device(), EV_ABS, ABS_MT_POSITION_X) &&
                libevdev_has_event_code(candidate->device(), EV_ABS, ABS_MT_POSITION_Y) &&
                libevdev_has_event_code(candidate->device(), EV_ABS, ABS_MT_TRACKING_ID)) {
                result = std::move(candidate);
                udev_device_unref(device);
                break;
            }
        }
        udev_device_unref(device);
    }
    udev_enumerate_unref(enumerate);
    return result;
}

} // namespace mintglide
