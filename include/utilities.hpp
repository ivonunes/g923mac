#pragma once

#include <cstdio>
#include <string>
#include <IOKit/IOReturn.h>
#include <mach/mach_error.h>
#include "constants.hpp"
#include "types.hpp"

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);
    
    static void log(LogLevel level, const std::string& message);
    static void set_enabled(bool enabled) { enabled_ = enabled; }
    
private:
    static bool enabled_;
    static void print_with_color(const std::string& prefix, const std::string& message, int color_code);
};

class ErrorHandler {
public:
    static bool check_io_result(const std::string& operation, IOReturn result);
    static std::string get_error_description(IOReturn result);
    
private:
    static void log_error(const std::string& operation, IOReturn result, const std::string& description);
};

namespace utils {
    std::string format_device_id(device_id_t device_id);
    bool is_valid_device_id(device_id_t device_id);
    float clamp(float value, float min, float max);
    float lerp(float a, float b, float t);
}
