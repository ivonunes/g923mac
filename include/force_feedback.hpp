#pragma once

#include "telemetry.hpp"
#include "force_feedback_config.hpp"
#include <cstdint>

// Force feedback calculation parameters
struct ForceParameters {
    std::uint8_t autocenter_force = 0;
    std::uint8_t autocenter_slope = 0;
    std::uint8_t damper_force_positive = 0;
    std::uint8_t damper_force_negative = 0;
    std::uint8_t constant_force = 0;
    bool use_constant_force = false;
    std::uint8_t spring_k1 = 0;
    std::uint8_t spring_k2 = 0;
    std::uint8_t spring_clip = 0;
    bool use_custom_spring = false;
    
    void reset() { *this = ForceParameters{}; }
};

class ForceCalculator {
public:
    explicit ForceCalculator(const FfbConfig& config = FfbConfig{});
    
    ForceParameters calculate(const TelemetryData& telemetry, TerrainState& terrain_state);
    
    void update_config(const FfbConfig& config) { config_ = config; }
    const FfbConfig& config() const { return config_; }
    
private:
    FfbConfig config_;
    
    // Force calculation methods
    void calculate_base_forces(const TelemetryData& telemetry, ForceParameters& params);
    void calculate_self_aligning_torque(const TelemetryData& telemetry, ForceParameters& params);
    void calculate_power_steering_effects(const TelemetryData& telemetry, ForceParameters& params);
    void calculate_terrain_effects(const TelemetryData& telemetry, TerrainState& terrain_state, ForceParameters& params);
    void calculate_vehicle_dynamics_effects(const TelemetryData& telemetry, ForceParameters& params);
    void calculate_steering_kickback(const TelemetryData& telemetry, ForceParameters& params);
    void apply_parking_brake_effects(const TelemetryData& telemetry, ForceParameters& params);
    
    // Terrain analysis
    void update_terrain_state(const TelemetryData& telemetry, TerrainState& terrain_state);
    bool detect_terrain_impact(const TelemetryData& telemetry, const TerrainState& terrain_state);
    
    // Utility functions
    float calculate_speed_factor(float speed_kmh, float threshold_low, float threshold_high);
    float calculate_power_steering_multiplier(const TelemetryData& telemetry);
    float calculate_centering_multiplier(const TelemetryData& telemetry);
    std::uint8_t clamp_to_byte(float value);
};

class LedController {
public:
    explicit LedController(const FfbConfig& config = FfbConfig{});
    
    std::uint8_t calculate_pattern(const TelemetryData& telemetry);
    
    void update_config(const FfbConfig& config) { config_ = config; }
    const FfbConfig& config() const { return config_; }
    
private:
    FfbConfig config_;
    bool flash_state_ = false;
    
    std::uint8_t calculate_rpm_pattern(float rpm, float speed_kmh);
    std::uint8_t calculate_brake_pattern(float brake);
    std::uint8_t get_parking_brake_pattern();
};
