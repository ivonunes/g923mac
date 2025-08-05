#include "telemetry.hpp"
#include <algorithm>

void TerrainState::update(float delta_time) {
    if (impact_timer > 0.0f) {
        impact_timer -= delta_time;
        impact_timer = std::max(0.0f, impact_timer);
    }
    
    if (impact_cooldown > 0.0f) {
        impact_cooldown -= delta_time;
        impact_cooldown = std::max(0.0f, impact_cooldown);
    }
}
