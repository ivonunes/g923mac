#include "plugin_manager.hpp"
#include "constants.hpp"
#include <scssdk_telemetry.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>
#include <amtrucks/scssdk_ats.h>
#include <amtrucks/scssdk_telemetry_ats.h>
#include <cstring>

std::unique_ptr<PluginManager> g_plugin_manager = nullptr;
    
PluginManager::PluginManager()
    : is_initialized_(false), is_paused_(true), game_log_(nullptr), last_timestamp_(-1),
        force_update_counter_(0), led_update_counter_(0) {
}

PluginManager::~PluginManager() {
    shutdown();
}

bool PluginManager::initialize(scs_log_t game_log) {
    if (is_initialized_) {
        return true;
    }
    
    game_log_ = game_log;
    log_info("Version " + std::string(VERSION) + " starting initialization...");
    
    device_manager_ = std::make_unique<DeviceManager>();
    if (!device_manager_->is_initialized()) {
        log_error("Failed to initialize device manager");
        return false;
    }
    
    force_calculator_ = std::make_unique<ForceCalculator>();
    led_controller_ = std::make_unique<LedController>();
    
    if (!discover_and_initialize_wheels()) {
        log_error("Failed to initialize wheels");
        return false;
    }
    
    telemetry_data_.reset();
    terrain_state_.reset();
    last_timestamp_ = static_cast<scs_timestamp_t>(-1);
    is_paused_ = true;
    
    is_initialized_ = true;
    log_info("Plugin initialization successful");
    return true;
}

void PluginManager::shutdown() {
    if (!is_initialized_) {
        return;
    }
    
    log_info("Shutting down plugin");
    
    // Reset all wheels before cleanup
    reset_all_wheels();
    
    // Give wheels time to process final commands
    usleep(200 * 1000);  // 200ms delay
    
    // Clean up wheels (this will destroy WheelController objects)
    cleanup_wheels();
    
    wheels_.clear();
    
    // Clean up managers in reverse order of creation
    force_calculator_.reset();
    led_controller_.reset();
    device_manager_.reset();  // This will close HID manager
    
    game_log_ = nullptr;
    is_initialized_ = false;
    
    log_info("Plugin shutdown complete");
}

void PluginManager::on_frame_start(const scs_telemetry_frame_start_t* info) {
    if (!info) return;
    
    if (last_timestamp_ == static_cast<scs_timestamp_t>(-1)) {
        last_timestamp_ = info->paused_simulation_time;
    }
    
    if (info->flags & SCS_TELEMETRY_FRAME_START_FLAG_timer_restart) {
        last_timestamp_ = 0;
    }
    
    telemetry_data_.timestamp += (info->paused_simulation_time - last_timestamp_);
    last_timestamp_ = info->paused_simulation_time;
    
    telemetry_data_.raw_rendering_timestamp = info->render_time;
    telemetry_data_.raw_simulation_timestamp = info->simulation_time;
    telemetry_data_.raw_paused_simulation_timestamp = info->paused_simulation_time;
}

void PluginManager::on_frame_end() {
    if (is_paused_) {
        reset_all_wheels();
        return;
    }
    
    --force_update_counter_;
    if (force_update_counter_ <= 0) {
        if (!update_force_feedback()) {
            log_error("Force feedback update failed");
        }
        force_update_counter_ = FORCE_UPDATE_RATE;
    }
    
    --led_update_counter_;
    if (led_update_counter_ <= 0) {
        if (!update_leds()) {
            log_warning("LED update failed");
        }
        led_update_counter_ = LED_UPDATE_RATE;
    }
    
    terrain_state_.last_vertical_acceleration = telemetry_data_.linear_acceleration_y;
}

void PluginManager::on_pause(bool paused) {
    is_paused_ = paused;
    
    if (paused) {
        reset_all_wheels();
        log_info("Telemetry paused, stopped forces");
    } else {
        log_info("Telemetry resumed");
    }
}

void PluginManager::update_telemetry_value(const std::string& channel, const scs_value_t* value) {
    if (!value) return;
    
    switch (value->type) {
        case SCS_VALUE_TYPE_fvector:
            process_vector_telemetry(channel, value);
            break;
        case SCS_VALUE_TYPE_float:
            process_float_telemetry(channel, value);
            break;
        case SCS_VALUE_TYPE_bool:
            process_bool_telemetry(channel, value);
            break;
        case SCS_VALUE_TYPE_s32:
        case SCS_VALUE_TYPE_u32:
            process_integer_telemetry(channel, value);
            break;
        case SCS_VALUE_TYPE_euler:
            process_euler_telemetry(channel, value);
            break;
        default:
            break;
    }
}

bool PluginManager::discover_and_initialize_wheels() {
    auto discovered_wheels = device_manager_->find_known_wheels();
    
    if (discovered_wheels.empty()) {
        log_error("No compatible wheels found");
        return false;
    }
    
    wheels_.clear();
    
    for (const auto& device : discovered_wheels) {
        auto wheel = std::make_unique<WheelController>(device);
        
        if (!wheel->initialize()) {
            log_warning("Failed to initialize wheel device " + utils::format_device_id(device.device_id));
            continue;
        }
        
        if (!wheel->calibrate()) {
            log_warning("Failed to calibrate wheel device " + utils::format_device_id(device.device_id));
            continue;
        }
        
        wheels_.push_back(std::move(wheel));
        log_info("Successfully initialized wheel device " + utils::format_device_id(device.device_id));
    }
    
    if (wheels_.empty()) {
        log_error("No wheels were successfully initialized");
        return false;
    }
    
    log_info("Initialized " + std::to_string(wheels_.size()) + " wheel(s)");
    return true;
}

bool PluginManager::update_force_feedback() {
    if (wheels_.empty() || !force_calculator_) {
        return false;
    }
    
    ForceParameters params = force_calculator_->calculate(telemetry_data_, terrain_state_);
    bool all_success = true;
    
    for (auto& wheel : wheels_) {
        if (!wheel->is_initialized()) continue;
        
        if (params.use_constant_force) {
            if (!wheel->set_constant_force(params.constant_force)) {
                log_error("Failed to set constant force");
                all_success = false;
            }
            continue;
        } else {
            wheel->stop_forces();
        }
        
        if (params.use_custom_spring) {
            if (!wheel->set_custom_spring(0, 0, params.spring_k1, params.spring_k2,
                                        0, 0, params.spring_clip)) {
                log_error("Failed to set custom spring");
                all_success = false;
            }
        }
        
        if (params.damper_force_positive > 0 || params.damper_force_negative > 0) {
            if (!wheel->set_damper(params.damper_force_positive, params.damper_force_negative, 0, 0)) {
                log_error("Failed to set damper force");
                all_success = false;
            }
        }
        
        if (params.autocenter_force > 0) {
            if (!wheel->enable_autocenter() ||
                !wheel->set_autocenter_spring(params.autocenter_slope, params.autocenter_slope,
                                                params.autocenter_force)) {
                log_error("Failed to set autocenter spring force");
                all_success = false;
            }
        } else {
            if (!wheel->disable_autocenter()) {
                log_error("Failed to disable autocenter spring");
                all_success = false;
            }
        }
    }
    
    return all_success;
}

bool PluginManager::update_leds() {
    if (wheels_.empty() || !led_controller_) {
        return false;
    }
    
    std::uint8_t pattern = led_controller_->calculate_pattern(telemetry_data_);
    bool all_success = true;
    
    for (auto& wheel : wheels_) {
        if (!wheel->is_initialized()) continue;
        
        if (!wheel->set_led_pattern(pattern)) {
            log_warning("LED update failed for wheel");
            all_success = false;
        }
    }
    
    return all_success;
}

void PluginManager::reset_all_wheels() {
    for (auto& wheel : wheels_) {
        if (!wheel->is_initialized()) continue;
        
        wheel->stop_forces();
        wheel->disable_autocenter();
        wheel->set_led_pattern(LED_PATTERN_OFF);
    }
}

void PluginManager::cleanup_wheels() {
    wheels_.clear();
}

void PluginManager::process_vector_telemetry(const std::string& channel, const scs_value_t* value) {
    if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_velocity) {
        telemetry_data_.linear_velocity_x = value->value_fvector.x;
        telemetry_data_.linear_velocity_y = value->value_fvector.y;
        telemetry_data_.linear_velocity_z = value->value_fvector.z;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_velocity) {
        telemetry_data_.angular_velocity_x = value->value_fvector.x;
        telemetry_data_.angular_velocity_y = value->value_fvector.y;
        telemetry_data_.angular_velocity_z = value->value_fvector.z;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_acceleration) {
        telemetry_data_.linear_acceleration_x = value->value_fvector.x;
        telemetry_data_.linear_acceleration_y = value->value_fvector.y;
        telemetry_data_.linear_acceleration_z = value->value_fvector.z;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_acceleration) {
        telemetry_data_.angular_acceleration_x = value->value_fvector.x;
        telemetry_data_.angular_acceleration_y = value->value_fvector.y;
        telemetry_data_.angular_acceleration_z = value->value_fvector.z;
    }
}

void PluginManager::process_float_telemetry(const std::string& channel, const scs_value_t* value) {
    float val = value->value_float.value;
    
    if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_speed) {
        telemetry_data_.speed = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm) {
        telemetry_data_.rpm = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_input_steering) {
        telemetry_data_.input_steering = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering) {
        telemetry_data_.steering = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle) {
        telemetry_data_.throttle = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake) {
        telemetry_data_.brake = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch) {
        telemetry_data_.clutch = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_brake_air_pressure) {
        telemetry_data_.brake_air_pressure = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control) {
        telemetry_data_.cruise_control = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_fuel) {
        telemetry_data_.fuel_amount = val;
    }
}

void PluginManager::process_bool_telemetry(const std::string& channel, const scs_value_t* value) {
    bool val = (value->value_bool.value != 0);
    
    if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_parking_brake) {
        telemetry_data_.parking_brake = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_motor_brake) {
        telemetry_data_.motor_brake = val;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled) {
        telemetry_data_.engine_enabled = val;
    }
}

void PluginManager::process_integer_telemetry(const std::string& channel, const scs_value_t* value) {
    if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear) {
        telemetry_data_.gear = value->value_s32.value;
    }
    else if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_retarder_level) {
        telemetry_data_.retarder_level = value->value_u32.value;
    }
}

void PluginManager::process_euler_telemetry(const std::string& channel, const scs_value_t* value) {
    if (channel == SCS_TELEMETRY_TRUCK_CHANNEL_world_placement) {
        telemetry_data_.orientation_available = true;
        telemetry_data_.heading = value->value_euler.heading * 360.0f;
        telemetry_data_.pitch = value->value_euler.pitch * 360.0f;
        telemetry_data_.roll = value->value_euler.roll * 360.0f;
    }
}

void PluginManager::log_info(const std::string& message) {
    if (game_log_) {
        game_log_(SCS_LOG_TYPE_message, ("g923mac::info : " + message).c_str());
    }
}

void PluginManager::log_warning(const std::string& message) {
    if (game_log_) {
        game_log_(SCS_LOG_TYPE_warning, ("g923mac::warning : " + message).c_str());
    }
}

void PluginManager::log_error(const std::string& message) {
    if (game_log_) {
        game_log_(SCS_LOG_TYPE_error, ("g923mac::error : " + message).c_str());
    }
}
