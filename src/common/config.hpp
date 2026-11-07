#pragma once

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace mintglide {

struct Config {
    bool tap_to_click = true;
    bool secondary_click = true;
    std::string secondary_click_mode = "two_finger";
    bool natural_scrolling = true;
    bool screen_zoom = true;
    bool three_finger_drag = true;
    bool smart_zoom = true;
    double drag_velocity_threshold = 0.80;
    double drag_threshold = 0.012;
    double scroll_sensitivity = 1.0;
    double drag_sensitivity = 1.0;
    int tap_duration_ms = 250;

    static std::filesystem::path config_directory() {
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') {
            throw std::runtime_error("HOME is not set; cannot locate Mintglide configuration");
        }
        return std::filesystem::path(home) / ".config" / "mintglide";
    }

    static std::filesystem::path config_file() {
        return config_directory() / "config.json";
    }

    static std::filesystem::path pid_file() {
        return config_directory() / "daemon.pid";
    }

    static Config load() {
        Config config;
        const auto path = config_file();
        const auto legacy_path =
            std::filesystem::path(config_directory().parent_path()) /
            "macgestures" / "config.json";
        std::ifstream input(path);
        if (!input) {
            input.clear();
            input.open(legacy_path);
        }
        if (!input) {
            if (!std::filesystem::exists(path) &&
                !std::filesystem::exists(legacy_path)) {
                return config;
            }
            throw std::runtime_error("Cannot open Mintglide configuration file");
        }

        nlohmann::json json;
        try {
            input >> json;
            if (!json.is_object()) {
                throw std::runtime_error("configuration root must be a JSON object");
            }

            read_if_present(json, "tap_to_click", config.tap_to_click);
            read_if_present(json, "secondary_click", config.secondary_click);
            read_if_present(json, "secondary_click_mode", config.secondary_click_mode);
            read_if_present(json, "natural_scrolling", config.natural_scrolling);
            read_if_present(json, "screen_zoom", config.screen_zoom);
            read_if_present(json, "three_finger_drag", config.three_finger_drag);
            read_if_present(json, "smart_zoom", config.smart_zoom);
            read_if_present(json, "drag_velocity_threshold", config.drag_velocity_threshold);
            read_if_present(json, "drag_threshold", config.drag_threshold);
            read_if_present(json, "scroll_sensitivity", config.scroll_sensitivity);
            read_if_present(json, "drag_sensitivity", config.drag_sensitivity);
            read_if_present(json, "tap_duration_ms", config.tap_duration_ms);
        } catch (const nlohmann::json::exception& error) {
            throw std::runtime_error("Invalid configuration in " + path.string() + ": " + error.what());
        }

        validate(config);
        return config;
    }

    void save() const {
        validate(*this);
        const auto directory = config_directory();
        std::filesystem::create_directories(directory);
        std::filesystem::permissions(
            directory, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace
        );

        const auto path = config_file();
        const auto temporary = path.string() + ".tmp." + std::to_string(::getpid());
        const nlohmann::json json = {
            {"tap_to_click", tap_to_click},
            {"secondary_click", secondary_click},
            {"secondary_click_mode", secondary_click_mode},
            {"natural_scrolling", natural_scrolling},
            {"screen_zoom", screen_zoom},
            {"three_finger_drag", three_finger_drag},
            {"smart_zoom", smart_zoom},
            {"drag_velocity_threshold", drag_velocity_threshold},
            {"drag_threshold", drag_threshold},
            {"scroll_sensitivity", scroll_sensitivity},
            {"drag_sensitivity", drag_sensitivity},
            {"tap_duration_ms", tap_duration_ms}
        };

        try {
            {
                std::ofstream output(temporary, std::ios::trunc);
                if (!output) {
                    throw std::runtime_error("Cannot write configuration file: " + temporary);
                }
                output << json.dump(2) << '\n';
                output.flush();
                if (!output) {
                    throw std::runtime_error("Failed while writing configuration file: " + temporary);
                }
            }
            std::filesystem::permissions(
                temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace
            );
            std::filesystem::rename(temporary, path);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

private:
    template <typename T>
    static void read_if_present(const nlohmann::json& json, const char* key, T& value) {
        const auto it = json.find(key);
        if (it != json.end()) {
            value = it->get<T>();
        }
    }

    static void validate(const Config& config) {
        if (config.drag_velocity_threshold < 0.05 ||
            config.drag_velocity_threshold > 5.0 ||
            config.drag_threshold < 0.0005 || config.drag_threshold > 0.1 ||
            config.scroll_sensitivity < 0.1 || config.scroll_sensitivity > 5.0 ||
            config.drag_sensitivity < 0.1 || config.drag_sensitivity > 5.0 ||
            config.tap_duration_ms < 50 || config.tap_duration_ms > 600) {
            throw std::runtime_error("Configuration contains a value outside its supported range");
        }
        if (config.secondary_click_mode != "two_finger" &&
            config.secondary_click_mode != "corner") {
            throw std::runtime_error("secondary_click_mode must be 'two_finger' or 'corner'");
        }
    }
};

} // namespace mintglide
