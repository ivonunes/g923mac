#include "plugin_manager.hpp"
#include "constants.hpp"
#include <scssdk_telemetry.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>
#include <amtrucks/scssdk_ats.h>
#include <amtrucks/scssdk_telemetry_ats.h>
#include <cstring>
#include <cassert>

SCSAPI_VOID telemetry_frame_start(scs_event_t const event, void const* const event_info,
                                 scs_context_t const context) {
    (void)event;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    const scs_telemetry_frame_start_t* info = static_cast<const scs_telemetry_frame_start_t*>(event_info);
    g_plugin_manager->on_frame_start(info);
}

SCSAPI_VOID telemetry_frame_end(scs_event_t const event, void const* const event_info,
                               scs_context_t const context) {
    (void)event;
    (void)event_info;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    g_plugin_manager->on_frame_end();
}

SCSAPI_VOID telemetry_pause(scs_event_t const event, void const* const event_info,
                           scs_context_t const context) {
    (void)event_info;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    bool paused = (event == SCS_TELEMETRY_EVENT_paused);
    g_plugin_manager->on_pause(paused);
}

SCSAPI_VOID telemetry_store_float(scs_string_t const name, scs_u32_t const index,
                                 scs_value_t const* const value, scs_context_t const context) {
    (void)index;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    std::string channel_name(name);
    g_plugin_manager->update_telemetry_value(channel_name, value);
}

SCSAPI_VOID telemetry_store_bool(scs_string_t const name, scs_u32_t const index,
                                scs_value_t const* const value, scs_context_t const context) {
    (void)index;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    std::string channel_name(name);
    g_plugin_manager->update_telemetry_value(channel_name, value);
}

SCSAPI_VOID telemetry_store_s32(scs_string_t const name, scs_u32_t const index,
                               scs_value_t const* const value, scs_context_t const context) {
    (void)index;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    std::string channel_name(name);
    g_plugin_manager->update_telemetry_value(channel_name, value);
}

SCSAPI_VOID telemetry_store_u32(scs_string_t const name, scs_u32_t const index,
                               scs_value_t const* const value, scs_context_t const context) {
    (void)index;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    std::string channel_name(name);
    g_plugin_manager->update_telemetry_value(channel_name, value);
}

SCSAPI_VOID telemetry_store_fvector(scs_string_t const name, scs_u32_t const index,
                                   scs_value_t const* const value, scs_context_t const context) {
    (void)index;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    std::string channel_name(name);
    g_plugin_manager->update_telemetry_value(channel_name, value);
}

SCSAPI_VOID telemetry_store_euler(scs_string_t const name, scs_u32_t const index,
                                 scs_value_t const* const value, scs_context_t const context) {
    (void)index;
    (void)context;
    
    if (!g_plugin_manager) return;
    
    std::string channel_name(name);
    g_plugin_manager->update_telemetry_value(channel_name, value);
}

SCSAPI_RESULT scs_telemetry_init(scs_u32_t const version, scs_telemetry_init_params_t const* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_01) {
        return SCS_RESULT_unsupported;
    }
    
    const scs_telemetry_init_params_v101_t* const version_params = 
        static_cast<const scs_telemetry_init_params_v101_t*>(params);
    
    scs_log_t game_log = version_params->common.log;
    
    // Game version checks
    if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) == 0) {
        scs_u32_t const MIN_VERSION = SCS_TELEMETRY_EUT2_GAME_VERSION_1_00;
        if (version_params->common.game_version < MIN_VERSION) {
            game_log(SCS_LOG_TYPE_warning, "g923mac::warning : ETS2 version too old, some features might not work");
        }
        
        scs_u32_t const CURRENT_VERSION = SCS_TELEMETRY_EUT2_GAME_VERSION_CURRENT;
        if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(CURRENT_VERSION)) {
            game_log(SCS_LOG_TYPE_warning, "g923mac::warning : ETS2 version too new, some features might not work");
        }
    }
    else if (strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) == 0) {
        scs_u32_t const MIN_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_1_00;
        if (version_params->common.game_version < MIN_VERSION) {
            game_log(SCS_LOG_TYPE_warning, "g923mac::warning : ATS version too old, some features might not work");
        }
        
        scs_u32_t const CURRENT_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_CURRENT;
        if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(CURRENT_VERSION)) {
            game_log(SCS_LOG_TYPE_warning, "g923mac::warning : ATS version too new, some features might not work");
        }
    }
    else {
        game_log(SCS_LOG_TYPE_warning, "g923mac::warning : Unknown game, some features might not work");
    }
    
    g_plugin_manager = std::make_unique<PluginManager>();
    
    if (!g_plugin_manager->initialize(game_log)) {
        game_log(SCS_LOG_TYPE_error, "g923mac::error : Plugin initialization failed");
        g_plugin_manager.reset();
        return SCS_RESULT_generic_error;
    }
    
    game_log(SCS_LOG_TYPE_message, "g923mac::info : Registering event callbacks...");
    
    bool events_registered = 
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_start, telemetry_frame_start, nullptr) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, telemetry_frame_end, nullptr) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, nullptr) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, nullptr) == SCS_RESULT_ok);
    
    if (!events_registered) {
        game_log(SCS_LOG_TYPE_error, "g923mac::error : Failed to register event callbacks");
        g_plugin_manager.reset();
        return SCS_RESULT_generic_error;
    }
    
    game_log(SCS_LOG_TYPE_message, "g923mac::info : Registering telemetry channels...");
    
    // Register for telemetry channels
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_euler,
        SCS_TELEMETRY_CHANNEL_FLAG_no_value, telemetry_store_euler, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL, SCS_VALUE_TYPE_float, 
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_U32_NIL, SCS_VALUE_TYPE_s32,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_s32, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_velocity, SCS_U32_NIL, SCS_VALUE_TYPE_fvector,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_fvector, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_velocity, SCS_U32_NIL, SCS_VALUE_TYPE_fvector,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_fvector, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_acceleration, SCS_U32_NIL, SCS_VALUE_TYPE_fvector,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_fvector, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_local_angular_acceleration, SCS_U32_NIL, SCS_VALUE_TYPE_fvector,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_fvector, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_parking_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_bool, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_motor_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_bool, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_retarder_level, SCS_U32_NIL, SCS_VALUE_TYPE_u32,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_u32, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_brake_air_pressure, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_fuel, SCS_U32_NIL, SCS_VALUE_TYPE_float,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, nullptr);
    
    version_params->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
        SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_bool, nullptr);
    
    game_log(SCS_LOG_TYPE_message, "g923mac::info : Plugin initialization complete");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown() {
    if (g_plugin_manager) {
        g_plugin_manager->shutdown();
        g_plugin_manager.reset();
    }
}

void __attribute__((destructor)) plugin_cleanup() {
    if (g_plugin_manager) {
        g_plugin_manager->shutdown();
        g_plugin_manager.reset();
    }
}
