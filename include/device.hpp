#pragma once

#include "types.hpp"
#include "utilities.hpp"
#include <vector>
#include <memory>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();
    
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;
    DeviceManager(DeviceManager&&) = delete;
    DeviceManager& operator=(DeviceManager&&) = delete;
    
    std::vector<HidDevice> list_all_devices();
    std::vector<HidDevice> find_known_wheels();
    
    bool is_initialized() const noexcept { return hid_manager_ != nullptr; }
    
private:
    hid_manager_t* hid_manager_;
    
    bool initialize_hid_manager();
    void cleanup_hid_manager();
    
    static device_id_t get_device_property_number(hid_device_t* device, CFStringRef property);
    static CFStringRef get_device_property_string(hid_device_t* device, CFStringRef property);
    static void copy_devices_to_array(const void* value, void* context);
};

class HidDeviceInterface {
public:
    explicit HidDeviceInterface(const HidDevice& device);
    ~HidDeviceInterface();
    
    HidDeviceInterface(const HidDeviceInterface&) = delete;
    HidDeviceInterface& operator=(const HidDeviceInterface&) = delete;
    HidDeviceInterface(HidDeviceInterface&&) = delete;
    HidDeviceInterface& operator=(HidDeviceInterface&&) = delete;
    
    bool open();
    bool close();
    bool send_command(const Command& command);
    
    bool is_open() const noexcept { return is_open_; }
    const HidDevice& device() const noexcept { return device_; }
    
private:
    HidDevice device_;
    bool is_open_;
    
    bool validate_device() const;
};
