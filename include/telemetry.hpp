#pragma once

#include "types.hpp"
#include <cstdint>
#include <scssdk_telemetry.h>

struct TelemetryData {
    scs_timestamp_t timestamp = 0;
    scs_timestamp_t raw_rendering_timestamp = 0;
    scs_timestamp_t raw_simulation_timestamp = 0;
    scs_timestamp_t raw_paused_simulation_timestamp = 0;
    
    bool orientation_available = false;
    float heading = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    
    float steering = 0.0f;
    float input_steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    float clutch = 0.0f;
    
    float speed = 0.0f;  // m/s
    float rpm = 0.0f;
    int gear = 0;
    
    float linear_velocity_x = 0.0f;    // lateral velocity
    float linear_velocity_y = 0.0f;    // vertical velocity
    float linear_velocity_z = 0.0f;    // longitudinal velocity
    float angular_velocity_x = 0.0f;   // roll rate
    float angular_velocity_y = 0.0f;   // pitch rate
    float angular_velocity_z = 0.0f;   // yaw rate
    float linear_acceleration_x = 0.0f;  // lateral acceleration
    float linear_acceleration_y = 0.0f;  // vertical acceleration
    float linear_acceleration_z = 0.0f;  // longitudinal acceleration
    float angular_acceleration_x = 0.0f; // roll acceleration
    float angular_acceleration_y = 0.0f; // pitch acceleration
    float angular_acceleration_z = 0.0f; // yaw acceleration
    
    bool parking_brake = false;
    bool motor_brake = false;
    std::uint32_t retarder_level = 0;
    float brake_air_pressure = 0.0f;
    float cruise_control = 0.0f;
    float fuel_amount = 0.0f;
    bool engine_enabled = false;
    
    float speed_kmh() const { return speed * 3.6f; }
    float lateral_g() const { return linear_acceleration_x / 9.81f; }
    float vertical_g() const { return linear_acceleration_y / 9.81f; }
    float longitudinal_g() const { return linear_acceleration_z / 9.81f; }
    
    void reset() { *this = TelemetryData{}; }
};

struct TerrainState {
    float smoothed_roughness = 0.0f;
    float impact_timer = 0.0f;
    float last_vertical_acceleration = 0.0f;
    float impact_cooldown = 0.0f;
    
    void reset() { *this = TerrainState{}; }
    void update(float delta_time);
};
