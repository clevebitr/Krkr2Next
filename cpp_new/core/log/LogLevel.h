#pragma once

#include <cstdint>
#include <string_view>

namespace krkr::log {
    enum class Level : std::int32_t {
        Debug = 0,
        Info = 1,
        Warn = 2,
        Error = 3,
        Off = 4,
    };

    constexpr std::string_view LevelName(Level level) {
        switch(level) {
            case Level::Debug:
                return "debug";
            case Level::Info:
                return "info";
            case Level::Warn:
                return "warn";
            case Level::Error:
                return "error";
            case Level::Off:
                return "off";
        }
        return "unknown";
    }

    constexpr bool ParseLevel(std::string_view text, Level &out) {
        if(text == "debug") {
            out = Level::Debug;
            return true;
        }
        if(text == "info") {
            out = Level::Info;
            return true;
        }
        if(text == "warn") {
            out = Level::Warn;
            return true;
        }
        if(text == "error") {
            out = Level::Error;
            return true;
        }
        if(text == "off") {
            out = Level::Off;
            return true;
        }
        return false;
    }
} // namespace krkr::log