#include "command.hpp"
#include "utilities.hpp"
#include <IOKit/hid/IOHIDDevice.h>

Command CommandBuilder::create_disable_autocenter() {
    return Command{g923_commands::DISABLE_AUTOCENTER};
}

Command CommandBuilder::create_enable_autocenter() {
    return Command{g923_commands::ENABLE_AUTOCENTER};
}

Command CommandBuilder::create_autocenter_spring(std::uint8_t k1, std::uint8_t k2, std::uint8_t clip) {
    return Command{g923_commands::SET_AUTOCENTER_SPRING, 0x00, k1, k2, clip, 0x00};
}

Command CommandBuilder::create_constant_force(std::uint8_t force_level) {
    return create_force_effect_command(g923_commands::EFFECT_CONSTANT, 
                                        {force_level, force_level, force_level, force_level, 0x00});
}

Command CommandBuilder::create_custom_spring(std::uint8_t d1, std::uint8_t d2, std::uint8_t k1, std::uint8_t k2,
                                            std::uint8_t s1, std::uint8_t s2, std::uint8_t clip) {
    return create_force_effect_command(g923_commands::EFFECT_SPRING,
                                        {d1, d2, static_cast<std::uint8_t>((k2 << 4) | k1), 
                                        static_cast<std::uint8_t>((s2 << 4) | s1), clip});
}

Command CommandBuilder::create_damper(std::uint8_t k1, std::uint8_t k2, std::uint8_t s1, std::uint8_t s2) {
    return create_force_effect_command(g923_commands::EFFECT_DAMPER, {k1, s1, k2, s2, 0x00});
}

Command CommandBuilder::create_trapezoid(std::uint8_t l1, std::uint8_t l2, std::uint8_t t1, std::uint8_t t2,
                                        std::uint8_t t3, std::uint8_t s) {
    return create_force_effect_command(g923_commands::EFFECT_TRAPEZOID,
                                        {l1, l2, t1, t2, static_cast<std::uint8_t>((t3 << 4) | s)});
}

Command CommandBuilder::create_stop_forces() {
    return Command{g923_commands::STOP_FORCES, 0x00};
}

Command CommandBuilder::create_led_pattern(std::uint8_t pattern) {
    return Command{g923_commands::SET_LED_PATTERN, g923_commands::LED_COMMAND_TYPE, pattern};
}

Command CommandBuilder::create_force_effect_command(std::uint8_t effect_type, 
                                                    const std::vector<std::uint8_t>& params) {
    Command command;
    command[0] = g923_commands::SET_FORCE_EFFECT;
    command[1] = effect_type;
    
    std::size_t param_index = 2;
    for (std::size_t i = 0; i < params.size() && param_index < COMMAND_MAX_LENGTH; ++i) {
        command[param_index++] = params[i];
    }
    
    return command;
}

IOReturn CommandSender::send_command(const HidDevice& device, const Command& command) {
    if (!device.is_valid()) {
        Logger::error("Cannot send command to invalid device");
        return kIOReturnBadArgument;
    }
    
    return send_hid_report(device.hid_device, command.raw(), command.size());
}

IOReturn CommandSender::send_commands(const HidDevice& device, const std::vector<Command>& commands) {
    if (!device.is_valid()) {
        Logger::error("Cannot send commands to invalid device");
        return kIOReturnBadArgument;
    }
    
    for (const auto& command : commands) {
        IOReturn result = send_command(device, command);
        if (result != kIOReturnSuccess) {
            return result;
        }
    }
    
    return kIOReturnSuccess;
}

IOReturn CommandSender::send_hid_report(hid_device_t* hid_device, const std::uint8_t* data, std::size_t length) {
    return IOHIDDeviceSetReport(hid_device, kIOHIDReportTypeOutput, time(nullptr), data, length);
}
