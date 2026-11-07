#pragma once

#include <libudev.h>
#include <libevdev/libevdev.h>

#include <memory>
#include <string>

namespace mintglide {

class InputDevice {
public:
    InputDevice() = default;
    ~InputDevice();
    InputDevice(const InputDevice&) = delete;
    InputDevice& operator=(const InputDevice&) = delete;

    static std::unique_ptr<InputDevice> find_apple_trackpad(udev* context);
    bool open(const std::string& path);
    void close();
    int fd() const noexcept { return fd_; }
    libevdev* device() const noexcept { return device_; }
    int slot_count() const;

private:
    int fd_ = -1;
    libevdev* device_ = nullptr;
};

} // namespace mintglide
