#include "wheel.hpp"
#include "command.hpp"
#include "constants.hpp"
#include "utilities.hpp"
#include <unistd.h>

WheelController::WheelController(const HidDevice& device)
    : device_(device), device_interface_(std::make_unique<HidDeviceInterface>(device)),
        is_initialized_(false), is_calibrated_(false) {
    
    if (!validate_device()) {
        Logger::error("Invalid device provided to WheelController");
        return;
    }
    
    Logger::info("Created WheelController for device " + utils::format_device_id(device_.device_id));
}

WheelController::~WheelController() {
    if (is_initialized_) {
        Logger::info("Cleaning up WheelController for device " + utils::format_device_id(device_.device_id));
        
        // Reset wheel state before closing
        if (device_interface_ && device_interface_->is_open()) {
            stop_forces();
            disable_autocenter();
            set_led_pattern(LED_PATTERN_OFF);
            
            // Give the device time to process final commands
            usleep(100 * 1000);  // 100ms delay
            
            // Close the device
            device_interface_->close();
        }
        
        Logger::info("WheelController destroyed for device " + utils::format_device_id(device_.device_id));
    }
}

bool WheelController::initialize() {
    if (is_initialized_) {
        return true;
    }
    
    if (!validate_device()) {
        return false;
    }
    
    Logger::info("Initializing wheel controller for device " + utils::format_device_id(device_.device_id));
    
    // Open the device and keep it open for the lifetime of the controller
    if (!device_interface_->open()) {
        Logger::error("Failed to open device during initialization");
        return false;
    }
    
    if (!set_led_pattern(LED_PATTERN_OFF)) {
        Logger::warning("Failed to reset LED pattern during initialization");
    }
    
    is_initialized_ = true;
    Logger::info("Wheel controller initialized successfully");
    return true;
}

bool WheelController::calibrate() {
    if (!is_initialized_) {
        Logger::error("Cannot calibrate: wheel not initialized");
        return false;
    }
    
    if (is_calibrated_) {
        return true;
    }
    
    Logger::info("Starting wheel calibration sequence");
    
    if (!perform_calibration_sequence()) {
        Logger::error("Calibration sequence failed");
        return false;
    }
    
    is_calibrated_ = true;
    Logger::info("Wheel calibration completed successfully");
    return true;
}

bool WheelController::perform_calibration_sequence() {
    Logger::debug("Starting LED sweep");
    if (!set_led_pattern(LED_PATTERN_OFF)) return false;
    
    // Forward sweep
    for (int i = 0; i < 32; ++i) {
        usleep(30 * 1000);  // 30ms delay
        if (!set_led_pattern(static_cast<std::uint8_t>(i))) {
            Logger::warning("LED pattern failed during forward sweep");
        }
    }
    
    // Backward sweep
    for (int i = 31; i >= 0; --i) {
        usleep(30 * 1000);  // 30ms delay
        if (!set_led_pattern(static_cast<std::uint8_t>(i))) {
            Logger::warning("LED pattern failed during backward sweep");
        }
    }
    
    Logger::debug("Testing force feedback");
    
    if (!disable_autocenter()) return false;
    if (!set_constant_force(120)) return false;
    
    usleep(500 * 1000);  // 500ms
    
    if (!stop_forces()) return false;
    if (!set_autocenter_spring(2, 2, 48)) return false;
    if (!enable_autocenter()) return false;
    
    usleep(500 * 1000);  // 500ms
    
    return true;
}

bool WheelController::enable_autocenter() {
    Command command = CommandBuilder::create_enable_autocenter();
    return send_command(command);
}

bool WheelController::disable_autocenter() {
    Command command = CommandBuilder::create_disable_autocenter();
    return send_command(command);
}

bool WheelController::set_autocenter_spring(std::uint8_t k1, std::uint8_t k2, std::uint8_t clip) {
    Command command = CommandBuilder::create_autocenter_spring(k1, k2, clip);
    return send_command(command);
}

bool WheelController::set_custom_spring(std::uint8_t d1, std::uint8_t d2, std::uint8_t k1, std::uint8_t k2,
                                        std::uint8_t s1, std::uint8_t s2, std::uint8_t clip) {
    Command command = CommandBuilder::create_custom_spring(d1, d2, k1, k2, s1, s2, clip);
    return send_command(command);
}

bool WheelController::set_constant_force(std::uint8_t force_level) {
    Command command = CommandBuilder::create_constant_force(force_level);
    return send_command(command);
}

bool WheelController::set_damper(std::uint8_t k1, std::uint8_t k2, std::uint8_t s1, std::uint8_t s2) {
    Command command = CommandBuilder::create_damper(k1, k2, s1, s2);
    return send_command(command);
}

bool WheelController::set_trapezoid(std::uint8_t l1, std::uint8_t l2, std::uint8_t t1, std::uint8_t t2,
                                    std::uint8_t t3, std::uint8_t s) {
    Command command = CommandBuilder::create_trapezoid(l1, l2, t1, t2, t3, s);
    return send_command(command);
}

bool WheelController::stop_forces() {
    Command command = CommandBuilder::create_stop_forces();
    return send_command(command);
}

bool WheelController::set_led_pattern(std::uint8_t pattern) {
    Command command = CommandBuilder::create_led_pattern(pattern);
    return send_command(command);
}

bool WheelController::send_command(const Command& command) {
    if (!device_interface_->is_open()) {
        Logger::error("Device not open for command");
        return false;
    }
    
    bool success = device_interface_->send_command(command);
    
    if (success) {
        Logger::debug("Command sent successfully");
    } else {
        Logger::error("Failed to send command");
    }
    
    return success;
}

bool WheelController::validate_device() const {
    if (!device_.is_valid()) {
        Logger::error("Invalid HID device");
        return false;
    }
    
    if (!device_.is_g923()) {
        Logger::error("Device is not a G923 wheel: " + utils::format_device_id(device_.device_id));
        return false;
    }
    
    return true;
}
