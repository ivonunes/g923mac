#pragma once

#include "telemetry.hpp"
#include "wheel.hpp"
#include "force_feedback.hpp"
#include "utilities.hpp"
#include <vector>
#include <memory>
#include <scssdk_telemetry.h>

class PluginManager {
public:
    PluginManager();
    ~PluginManager();
    
    bool initialize(scs_log_t game_log);
    void shutdown();
    
    void on_frame_start(const scs_telemetry_frame_start_t* info);
    void on_frame_end();
    void on_pause(bool paused);
    
    void update_telemetry_value(const std::string& channel, const scs_value_t* value);
    
    bool is_initialized() const noexcept { return is_initialized_; }
    bool is_paused() const noexcept { return is_paused_; }
    std::size_t wheel_count() const noexcept { return wheels_.size(); }
    
private:
    bool is_initialized_;
    bool is_paused_;
    scs_log_t game_log_;
    scs_timestamp_t last_timestamp_;
    
    TelemetryData telemetry_data_;
    TerrainState terrain_state_;
    std::unique_ptr<DeviceManager> device_manager_;
    std::vector<std::unique_ptr<WheelController>> wheels_;
    std::unique_ptr<ForceCalculator> force_calculator_;
    std::unique_ptr<LedController> led_controller_;
    
    int force_update_counter_;
    int led_update_counter_;
    
    bool discover_and_initialize_wheels();
    bool update_force_feedback();
    bool update_leds();
    void reset_all_wheels();
    void cleanup_wheels();
    
    void process_vector_telemetry(const std::string& channel, const scs_value_t* value);
    void process_float_telemetry(const std::string& channel, const scs_value_t* value);
    void process_bool_telemetry(const std::string& channel, const scs_value_t* value);
    void process_integer_telemetry(const std::string& channel, const scs_value_t* value);
    void process_euler_telemetry(const std::string& channel, const scs_value_t* value);
    
    void log_info(const std::string& message);
    void log_warning(const std::string& message);
    void log_error(const std::string& message);
};

extern std::unique_ptr<PluginManager> g_plugin_manager;
