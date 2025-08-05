#pragma once

#include "types.hpp"
#include "device.hpp"
#include "utilities.hpp"
#include <memory>

class WheelController {
public:
    explicit WheelController(const HidDevice& device);
    ~WheelController();
    
    WheelController(const WheelController&) = delete;
    WheelController& operator=(const WheelController&) = delete;
    WheelController(WheelController&&) = default;
    WheelController& operator=(WheelController&&) = default;
    
    bool initialize();
    bool calibrate();
    
    bool enable_autocenter();
    bool disable_autocenter();
    bool set_autocenter_spring(std::uint8_t k1, std::uint8_t k2, std::uint8_t clip);
    bool set_custom_spring(std::uint8_t d1, std::uint8_t d2, std::uint8_t k1, std::uint8_t k2,
                            std::uint8_t s1, std::uint8_t s2, std::uint8_t clip);
    bool set_constant_force(std::uint8_t force_level);
    bool set_damper(std::uint8_t k1, std::uint8_t k2, std::uint8_t s1, std::uint8_t s2);
    bool set_trapezoid(std::uint8_t l1, std::uint8_t l2, std::uint8_t t1, std::uint8_t t2,
                        std::uint8_t t3, std::uint8_t s);
    bool stop_forces();
    
    bool set_led_pattern(std::uint8_t pattern);
    
    bool is_initialized() const noexcept { return is_initialized_; }
    bool is_calibrated() const noexcept { return is_calibrated_; }
    const HidDevice& device() const noexcept { return device_; }
    
private:
    HidDevice device_;
    std::unique_ptr<HidDeviceInterface> device_interface_;
    bool is_initialized_;
    bool is_calibrated_;
    
    bool send_command(const Command& command);
    Command create_command_for_device(std::uint8_t cmd_id, 
                                        const std::vector<std::uint8_t>& params = {});
    
    bool validate_device() const;
    bool perform_calibration_sequence();
};
