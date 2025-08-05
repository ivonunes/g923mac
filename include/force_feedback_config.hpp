#pragma once

struct FfbConfig {
    // Update rates (lower = more frequent updates)
    int force_update_rate = 8;  // Force feedback update every 8 frames
    int led_update_rate = 32;   // LED update every 32 frames

    // Self-aligning torque parameters
    float sat_base_torque_factor = 0.8f;     // Base self-aligning torque multiplier
    float sat_speed_reduction_start = 80.0f;  // Speed (km/h) where SAT starts reducing
    float sat_speed_reduction_range = 120.0f; // Speed range for SAT reduction
    float sat_min_factor = 0.3f;             // Minimum SAT factor at high speeds
    float sat_lateral_g_factor = 0.8f;       // How much lateral G reduces SAT
    float sat_max_lateral_reduction = 0.7f;  // Maximum SAT reduction from lateral G

    // Centering force parameters
    float center_stationary_force = 0.0f;    // No centering when stationary
    float center_stationary_slope = 0.0f;    // No centering slope when stationary
    float center_low_speed_base = 20.0f;     // Base centering force at low speeds
    float center_low_speed_factor = 0.8f;    // Multiplier for low speed centering
    float center_highway_base = 18.0f;       // Base centering force at highway speeds
    float center_highway_factor = 0.6f;      // Multiplier for highway centering
    float center_max_force = 45.0f;          // Maximum centering force

    // Speed thresholds for different behaviors (km/h)
    float speed_stationary_threshold = 2.0f;
    float speed_low_threshold = 15.0f;
    float speed_medium_threshold = 35.0f;
    float speed_high_threshold = 65.0f;
    float speed_very_high_threshold = 100.0f;

    // Damping parameters
    float damper_stationary_pos = 2.0f;
    float damper_stationary_neg = 2.0f;
    float damper_low_speed = 2.5f;
    float damper_speed_factor = 40.0f;
    float damper_max = 3.0f;
    float damper_brake_factor = 0.8f;
    float damper_retarder_factor = 0.08f;
    float damper_engine_brake_factor = 0.25f;
    float damper_max_total = 6.0f;

    // Vehicle dynamics parameters
    float yaw_rate_threshold = 0.1f;         // Minimum yaw rate to trigger effects
    float yaw_rate_factor = 10.0f;           // Yaw rate to force multiplier
    float yaw_max_factor = 2.0f;             // Maximum yaw rate effect
    float understeer_factor = 0.3f;          // Understeer centering increase
    float oversteer_reduction = 0.2f;        // Oversteer centering reduction
    float oversteer_damping_add = 1.0f;      // Additional damping during oversteer

    // Road surface simulation
    float road_feel_speed_threshold = 20.0f;
    float road_feel_rpm_threshold = 600.0f;
    float road_feel_speed_factor = 80.0f;
    float road_feel_intensity_threshold = 0.5f;
    float road_feel_spring_base = 1.5f;
    float road_feel_spring_factor = 1.5f;
    float road_feel_clip_base = 15.0f;
    float road_feel_clip_factor = 10.0f;

    // Terrain surface effects
    float terrain_offroad_multiplier = 4.0f;
    float terrain_rough_frequency = 8.0f;
    float terrain_smooth_frequency = 15.0f;
    float terrain_detection_threshold = 0.08f;
    float terrain_minor_threshold = 0.02f;
    float terrain_major_threshold = 0.25f;
    float terrain_impact_multiplier = 10.0f;
    float terrain_impact_duration = 0.5f;
    float terrain_smoothing_factor = 0.8f;

    // Steering kickback simulation
    float kickback_threshold = 2.0f;
    float kickback_speed_threshold = 5.0f;
    float kickback_factor = 10.0f;
    float kickback_max_force = 40.0f;

    // Weight transfer effects
    float weight_transfer_threshold = 0.2f;
    float weight_transfer_factor = 0.5f;
    float weight_transfer_max_force = 90.0f;

    // Parking brake effects
    float parking_brake_force = 80.0f;
    float parking_brake_slope = 6.0f;
    float parking_brake_damper = 8.0f;

    // LED configuration
    float led_brake_threshold = 0.5f;
    float led_heavy_brake = 0.9f;
    float led_medium_brake = 0.7f;
    float led_speed_high_threshold = 50.0f;
    float led_speed_low_threshold = 10.0f;
    float led_rpm_base = 1000.0f;
    float led_rpm_highway = 800.0f;
    float led_rpm_city = 1200.0f;
    float led_rpm_step1 = 300.0f;
    float led_rpm_step2 = 600.0f;
    float led_rpm_step3 = 800.0f;
    float led_rpm_step4 = 1000.0f;
};
