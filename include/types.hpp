#pragma once

#include <array>
#include <vector>
#include <memory>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDManager.h>
#include "constants.hpp"

using device_id_t = std::uint32_t;
using hid_device_t = __IOHIDDevice;
using hid_manager_t = __IOHIDManager;

template<typename T>
using vector = std::vector<T>;

template<typename T>
using unique_ptr = std::unique_ptr<T>;

template<typename T>
using shared_ptr = std::shared_ptr<T>;

struct HidDevice {
    device_id_t vendor_id = 0;
    device_id_t product_id = 0;
    device_id_t device_id = 0;
    hid_device_t* hid_device = nullptr;
    
    HidDevice() = default;
    HidDevice(device_id_t vid, device_id_t pid, device_id_t did, hid_device_t* device)
        : vendor_id(vid), product_id(pid), device_id(did), hid_device(device) {}
    
    bool is_valid() const noexcept { return hid_device != nullptr; }
    bool is_g923() const noexcept { return device_id == G923_DEVICE_ID; }
};

struct Command {
    std::uint8_t data[COMMAND_MAX_LENGTH] = {0};
    
    Command() = default;
    explicit Command(std::initializer_list<std::uint8_t> init);
    
    std::uint8_t& operator[](std::size_t index) noexcept { return data[index]; }
    const std::uint8_t& operator[](std::size_t index) const noexcept { return data[index]; }
    
    const std::uint8_t* raw() const noexcept { return data; }
    std::size_t size() const noexcept { return COMMAND_MAX_LENGTH; }
};
    
static const std::array<device_id_t, 1> KNOWN_WHEEL_IDS = {G923_DEVICE_ID};
