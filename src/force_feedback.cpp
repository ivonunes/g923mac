#include "force_feedback.hpp"
#include "constants.hpp"
#include "utilities.hpp"
#include <algorithm>
#include <cmath>

ForceCalculator::ForceCalculator(const FfbConfig& config) : config_(config) {
}

ForceParameters ForceCalculator::calculate(const TelemetryData& telemetry, TerrainState& terrain_state) {
    ForceParameters params;
    
    // Update terrain analysis first
    update_terrain_state(telemetry, terrain_state);
    
    // Calculate base forces (centering and damping)
    calculate_base_forces(telemetry, params);
    
    // Apply advanced effects
    calculate_self_aligning_torque(telemetry, params);
    calculate_power_steering_effects(telemetry, params);
    calculate_terrain_effects(telemetry, terrain_state, params);
    calculate_vehicle_dynamics_effects(telemetry, params);
    calculate_steering_kickback(telemetry, params);
    
    return params;
}

void ForceCalculator::calculate_base_forces(const TelemetryData& telemetry, ForceParameters& params) {
    float speed_kmh = telemetry.speed_kmh();
    float abs_speed = std::abs(telemetry.speed);
    
    float centering_multiplier = calculate_centering_multiplier(telemetry);
    float power_steering_multiplier = calculate_power_steering_multiplier(telemetry);
    
    if (abs_speed < config_.speed_stationary_threshold) {
        // Stationary behavior
        params.autocenter_force = clamp_to_byte(config_.center_stationary_force * centering_multiplier);
        params.autocenter_slope = clamp_to_byte(config_.center_stationary_slope);
        params.damper_force_positive = clamp_to_byte(config_.damper_stationary_pos * power_steering_multiplier);
        params.damper_force_negative = clamp_to_byte(config_.damper_stationary_neg * power_steering_multiplier);
    }
    else if (speed_kmh < config_.speed_low_threshold) {
        // Low speed behavior
        float speed_factor = speed_kmh * config_.center_low_speed_factor;
        params.autocenter_force = clamp_to_byte((config_.center_low_speed_base + speed_factor) * centering_multiplier);
        params.autocenter_slope = 2;
        params.damper_force_positive = clamp_to_byte(config_.damper_low_speed * power_steering_multiplier);
        params.damper_force_negative = clamp_to_byte(config_.damper_low_speed * power_steering_multiplier);
    }
    else {
        // High speed behavior
        params.autocenter_force = clamp_to_byte(config_.center_highway_base * centering_multiplier);
        
        // Set slope based on speed
        if (speed_kmh < config_.speed_medium_threshold) params.autocenter_slope = 2;
        else if (speed_kmh < config_.speed_high_threshold) params.autocenter_slope = 3;
        else if (speed_kmh < config_.speed_very_high_threshold) params.autocenter_slope = 4;
        else params.autocenter_slope = 5;
        
        float base_damper = std::min(config_.damper_max, 
                                    (1.0f + speed_kmh / config_.damper_speed_factor) * power_steering_multiplier);
        params.damper_force_positive = clamp_to_byte(base_damper);
        params.damper_force_negative = clamp_to_byte(base_damper);
    }
}

void ForceCalculator::calculate_self_aligning_torque(const TelemetryData& telemetry, ForceParameters& params) {
    float abs_speed = std::abs(telemetry.speed);
    float speed_kmh = telemetry.speed_kmh();
    
    if (abs_speed <= config_.speed_stationary_threshold) {
        return;  // No SAT when stationary
    }
    
    float self_align_torque = abs_speed * config_.sat_base_torque_factor * std::abs(telemetry.steering);
    
    // Reduce SAT at high speeds
    if (speed_kmh > config_.sat_speed_reduction_start) {
        float speed_factor = 1.0f - ((speed_kmh - config_.sat_speed_reduction_start) / config_.sat_speed_reduction_range);
        self_align_torque *= std::max(config_.sat_min_factor, speed_factor);
    }
    
    // Reduce SAT based on lateral G forces
    float lateral_g = telemetry.lateral_g();
    float lateral_factor = 1.0f - std::min(config_.sat_max_lateral_reduction, 
                                            std::abs(lateral_g) * config_.sat_lateral_g_factor);
    self_align_torque *= lateral_factor;
    
    // Apply SAT to centering force
    float enhanced_center = std::min(config_.center_max_force, 
                                    params.autocenter_force + self_align_torque * config_.center_highway_factor);
    params.autocenter_force = clamp_to_byte(enhanced_center);
}

void ForceCalculator::calculate_power_steering_effects(const TelemetryData& telemetry, ForceParameters& params) {
    if (telemetry.motor_brake || telemetry.retarder_level > 0) {
        float brake_factor = config_.damper_brake_factor + 
                            (telemetry.retarder_level * config_.damper_retarder_factor);
        
        if (telemetry.motor_brake) {
            brake_factor += config_.damper_engine_brake_factor;
        }
        
        params.damper_force_positive = clamp_to_byte(
            std::min(config_.damper_max_total, params.damper_force_positive * brake_factor));
        params.damper_force_negative = clamp_to_byte(
            std::min(config_.damper_max_total, params.damper_force_negative * brake_factor));
    }
}

void ForceCalculator::calculate_terrain_effects(const TelemetryData& telemetry, TerrainState& terrain_state, 
                                                ForceParameters& params) {
    float abs_speed = std::abs(telemetry.speed);
    float current_roughness = std::abs(telemetry.vertical_g());
    
    terrain_state.smoothed_roughness = terrain_state.smoothed_roughness * config_.terrain_smoothing_factor +
                                        current_roughness * (1.0f - config_.terrain_smoothing_factor);
    
    if (detect_terrain_impact(telemetry, terrain_state)) {
        terrain_state.impact_timer = config_.terrain_impact_duration;
        terrain_state.impact_cooldown = 0.6f;  // 600ms cooldown
    }
    
    float terrain_force_multiplier = 1.0f;
    float terrain_damping_add = 0.0f;
    bool use_terrain_spring = false;
    std::uint8_t terrain_spring_intensity = 0;
    
    if (terrain_state.impact_timer > 0.0f) {
        float impact_intensity = terrain_state.impact_timer / config_.terrain_impact_duration;
        terrain_force_multiplier += impact_intensity * 1.5f;
        terrain_damping_add += impact_intensity * 3.0f;
        
        use_terrain_spring = true;
        terrain_spring_intensity = clamp_to_byte(impact_intensity * 25.0f);
    }
    // Minor bumps and surface variations
    else if (current_roughness > config_.terrain_minor_threshold && abs_speed > 2.0f) {
        float speed_factor = std::min(1.0f, abs_speed / 10.0f);
        terrain_force_multiplier += current_roughness * 1.5f * speed_factor;
        terrain_damping_add += current_roughness * 1.2f * speed_factor;
        
        use_terrain_spring = true;
        terrain_spring_intensity = clamp_to_byte(2.0f + current_roughness * 8.0f * speed_factor);
    }
    // Continuous rough terrain
    else if (terrain_state.smoothed_roughness > config_.terrain_detection_threshold && abs_speed > 1.0f) {
        float speed_factor = std::min(1.0f, abs_speed / 8.0f);
        bool major_terrain = terrain_state.smoothed_roughness > config_.terrain_major_threshold;
        
        if (major_terrain) {
            terrain_force_multiplier = 1.0f + config_.terrain_offroad_multiplier * 0.2f * speed_factor;
            terrain_damping_add = terrain_state.smoothed_roughness * 1.2f * speed_factor;
        } else {
            terrain_force_multiplier = 1.0f + terrain_state.smoothed_roughness * 1.0f * speed_factor;
            terrain_damping_add = terrain_state.smoothed_roughness * 0.8f * speed_factor;
        }
        
        use_terrain_spring = true;
        terrain_spring_intensity = clamp_to_byte(1.0f + terrain_state.smoothed_roughness * 4.0f * speed_factor);
    }
    
    // Apply terrain effects
    if (terrain_force_multiplier > 1.0f || terrain_damping_add > 0.0f) {
        params.autocenter_force = clamp_to_byte(
            std::min(80.0f, params.autocenter_force * terrain_force_multiplier));
        
        params.damper_force_positive = clamp_to_byte(
            std::min(8.0f, params.damper_force_positive + terrain_damping_add));
        params.damper_force_negative = clamp_to_byte(
            std::min(8.0f, params.damper_force_negative + terrain_damping_add));
    }
    
    if (use_terrain_spring) {
        params.use_custom_spring = true;
        params.spring_k1 = terrain_spring_intensity;
        params.spring_k2 = terrain_spring_intensity;
        params.spring_clip = clamp_to_byte(20 + terrain_spring_intensity * 8);
    }
}

void ForceCalculator::calculate_vehicle_dynamics_effects(const TelemetryData& telemetry, ForceParameters& params) {
    float abs_speed = std::abs(telemetry.speed);
    float yaw_rate = telemetry.angular_velocity_z;
    
    if (std::abs(yaw_rate) > config_.yaw_rate_threshold && abs_speed > 5.0f) {
        float yaw_factor = std::min(config_.yaw_max_factor, std::abs(yaw_rate) * config_.yaw_rate_factor);
        
        // Check for oversteer vs understeer
        bool is_oversteer = (yaw_rate > 0 && telemetry.steering > 0) || (yaw_rate < 0 && telemetry.steering < 0);
        
        if (is_oversteer) {
            // Oversteer: reduce centering, add damping
            params.autocenter_force = clamp_to_byte(
                params.autocenter_force * (1.0f - yaw_factor * config_.oversteer_reduction));
            params.damper_force_positive = clamp_to_byte(
                params.damper_force_positive + yaw_factor * config_.oversteer_damping_add);
            params.damper_force_negative = clamp_to_byte(
                params.damper_force_negative + yaw_factor * config_.oversteer_damping_add);
        } else {
            // Understeer: increase centering force
            params.autocenter_force = clamp_to_byte(
                std::min(80.0f, params.autocenter_force * (1.0f + yaw_factor * config_.understeer_factor)));
        }
    }
}

void ForceCalculator::calculate_steering_kickback(const TelemetryData& telemetry, ForceParameters& params) {
    float abs_speed = std::abs(telemetry.speed);
    float steering_rate = std::abs(telemetry.angular_acceleration_z);
    
    if (steering_rate > config_.kickback_threshold && 
        abs_speed > config_.kickback_speed_threshold && 
        !params.use_constant_force) {
        
        params.use_constant_force = true;
        params.constant_force = clamp_to_byte(
            std::min(config_.kickback_max_force, steering_rate * config_.kickback_factor));
    }
}

void ForceCalculator::update_terrain_state(const TelemetryData& telemetry, TerrainState& terrain_state) {
    terrain_state.update(1.0f / 60.0f);  // Assume 60 FPS
    terrain_state.last_vertical_acceleration = telemetry.linear_acceleration_y;
}

bool ForceCalculator::detect_terrain_impact(const TelemetryData& telemetry, const TerrainState& terrain_state) {
    float abs_speed = std::abs(telemetry.speed);
    float vertical_acceleration = telemetry.linear_acceleration_y;
    float abs_vertical_accel = std::abs(vertical_acceleration);
    float accel_change = std::abs(vertical_acceleration - terrain_state.last_vertical_acceleration);
    
    // Dynamic threshold based on driving conditions
    float impact_threshold = config_.terrain_minor_threshold * 1.5f;
    
    if (abs_speed > 25.0f) impact_threshold *= 1.5f;
    if (std::abs(telemetry.angular_velocity_y) > 0.1f) impact_threshold *= 1.2f;
    if (std::abs(telemetry.linear_acceleration_z) > 1.0f) impact_threshold *= 1.3f;

    return (accel_change > impact_threshold) &&
            ((abs_vertical_accel > (impact_threshold * 1.5f)) ||
            (accel_change > (impact_threshold * 1.2f))) &&
            (accel_change > 0.05f) &&  // Minimum change threshold
            (abs_speed > 1.0f) &&      // Must be moving
            (terrain_state.impact_cooldown <= 0.0f);  // Not in cooldown
}

float ForceCalculator::calculate_speed_factor(float speed_kmh, float threshold_low, float threshold_high) {
    if (speed_kmh <= threshold_low) return 0.0f;
    if (speed_kmh >= threshold_high) return 1.0f;
    return (speed_kmh - threshold_low) / (threshold_high - threshold_low);
}

float ForceCalculator::calculate_power_steering_multiplier(const TelemetryData& telemetry) {
    float speed_kmh = telemetry.speed_kmh();
    
    if (telemetry.engine_enabled && telemetry.rpm > 500.0f) {
        // Engine running - power steering available
        if (speed_kmh < 10.0f) return 0.7f;
        else if (speed_kmh < 30.0f) return 0.8f;
        else return 0.9f;
    } else {
        // Engine off - heavy steering
        if (speed_kmh < 10.0f) return 2.0f;
        else if (speed_kmh < 30.0f) return 1.6f;
        else return 1.3f;
    }
}

float ForceCalculator::calculate_centering_multiplier(const TelemetryData& telemetry) {
    if (telemetry.engine_enabled && telemetry.rpm > 500.0f) {
        return 0.7f;  // Reduced centering with power steering
    } else {
        return 1.0f;  // Full centering without power steering
    }
}

std::uint8_t ForceCalculator::clamp_to_byte(float value) {
    return static_cast<std::uint8_t>(utils::clamp(value, 0.0f, 255.0f));
}

LedController::LedController(const FfbConfig& config) : config_(config), flash_state_(false) {
}

std::uint8_t LedController::calculate_pattern(const TelemetryData& telemetry) {
    // Parking brake has highest priority
    if (telemetry.parking_brake) {
        return get_parking_brake_pattern();
    }
    
    // Brake indication
    if (telemetry.brake > config_.led_brake_threshold) {
        return calculate_brake_pattern(telemetry.brake);
    }
    
    // RPM indication
    return calculate_rpm_pattern(telemetry.rpm, telemetry.speed_kmh());
}

std::uint8_t LedController::calculate_rpm_pattern(float rpm, float speed_kmh) {
    if (rpm == 0) {
        return LED_PATTERN_OFF;
    }
    
    // Adjust RPM thresholds based on speed
    float rpm_threshold_base = config_.led_rpm_base;
    if (speed_kmh > config_.led_speed_high_threshold) {
        rpm_threshold_base = config_.led_rpm_highway;
    } else if (speed_kmh < config_.led_speed_low_threshold) {
        rpm_threshold_base = config_.led_rpm_city;
    }
    
    if (rpm < rpm_threshold_base) {
        return LED_PATTERN_1;
    } else if (rpm < rpm_threshold_base + config_.led_rpm_step1) {
        return LED_PATTERN_2;
    } else if (rpm < rpm_threshold_base + config_.led_rpm_step2) {
        return LED_PATTERN_3;
    } else if (rpm < rpm_threshold_base + config_.led_rpm_step3) {
        return LED_PATTERN_4;
    } else if (rpm < rpm_threshold_base + config_.led_rpm_step4) {
        return LED_PATTERN_5;
    } else {
        // Flash at redline
        flash_state_ = !flash_state_;
        return flash_state_ ? LED_PATTERN_5 : LED_PATTERN_4;
    }
}

std::uint8_t LedController::calculate_brake_pattern(float brake) {
    if (brake > config_.led_heavy_brake) {
        return LED_PATTERN_5;
    } else if (brake > config_.led_medium_brake) {
        return LED_PATTERN_4;
    } else {
        return LED_PATTERN_3;
    }
}

std::uint8_t LedController::get_parking_brake_pattern() {
    // Flash all LEDs when parking brake is engaged
    flash_state_ = !flash_state_;
    return flash_state_ ? LED_PATTERN_5 : LED_PATTERN_OFF;
}
