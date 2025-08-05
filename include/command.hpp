#pragma once

#include "types.hpp"
#include "constants.hpp"
#include <IOKit/IOReturn.h>
#include <vector>
#include <ctime>

namespace g923_commands {
    static constexpr std::uint8_t DISABLE_AUTOCENTER = 0xF5;
    static constexpr std::uint8_t ENABLE_AUTOCENTER = 0xF4;
    static constexpr std::uint8_t SET_AUTOCENTER_SPRING = 0xFE;
    static constexpr std::uint8_t SET_FORCE_EFFECT = 0xF1;
    static constexpr std::uint8_t STOP_FORCES = 0xF3;
    static constexpr std::uint8_t SET_LED_PATTERN = 0xF8;
    
    static constexpr std::uint8_t EFFECT_CONSTANT = 0x00;
    static constexpr std::uint8_t EFFECT_SPRING = 0x01;
    static constexpr std::uint8_t EFFECT_DAMPER = 0x02;
    static constexpr std::uint8_t EFFECT_TRAPEZOID = 0x06;
    
    static constexpr std::uint8_t LED_COMMAND_TYPE = 0x12;
}

class CommandBuilder {
public:
    static Command create_disable_autocenter();
    static Command create_enable_autocenter();
    static Command create_autocenter_spring(std::uint8_t k1, std::uint8_t k2, std::uint8_t clip);
    static Command create_constant_force(std::uint8_t force_level);
    static Command create_custom_spring(std::uint8_t d1, std::uint8_t d2, std::uint8_t k1, std::uint8_t k2,
                                        std::uint8_t s1, std::uint8_t s2, std::uint8_t clip);
    static Command create_damper(std::uint8_t k1, std::uint8_t k2, std::uint8_t s1, std::uint8_t s2);
    static Command create_trapezoid(std::uint8_t l1, std::uint8_t l2, std::uint8_t t1, std::uint8_t t2,
                                    std::uint8_t t3, std::uint8_t s);
    static Command create_stop_forces();
    static Command create_led_pattern(std::uint8_t pattern);
    
private:
    static Command create_force_effect_command(std::uint8_t effect_type, 
                                                const std::vector<std::uint8_t>& params);
};

class CommandSender {
public:
    static IOReturn send_command(const HidDevice& device, const Command& command);
    static IOReturn send_commands(const HidDevice& device, const std::vector<Command>& commands);
    
private:
    static IOReturn send_hid_report(hid_device_t* hid_device, const std::uint8_t* data, std::size_t length);
};
