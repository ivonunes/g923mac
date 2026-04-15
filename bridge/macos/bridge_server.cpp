#include "bridge_server.hpp"
#include "utilities.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

bool recv_exact(int fd, void* buffer, std::size_t size) {
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t received = 0;

    while (received < size) {
        const ssize_t chunk = recv(fd, out + received, size - received, 0);
        if (chunk == 0) {
            return false;
        }
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        received += static_cast<std::size_t>(chunk);
    }

    return true;
}

bool send_exact(int fd, const void* buffer, std::size_t size) {
    const auto* data = static_cast<const std::uint8_t*>(buffer);
    std::size_t sent = 0;

    while (sent < size) {
        const ssize_t chunk = send(fd, data + sent, size - sent, 0);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(chunk);
    }

    return true;
}

void close_if_open(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int map_constant_magnitude_to_level(std::int16_t signed_magnitude) {
    constexpr int kNominalForceMax = 10000;
    constexpr int kInputDeadzone = 2;
    constexpr float kLowRangeReference = 64.0f;
    constexpr float kLowRangeExponent = 0.60f;
    constexpr float kLowRangeMaxOutput = 90.0f;
    constexpr float kHighRangeGain = 2.0f;

    const int magnitude = std::abs(static_cast<int>(signed_magnitude));
    if (magnitude <= kInputDeadzone) {
        return 0;
    }

    const float low_normalized = std::min(
        1.0f, static_cast<float>(magnitude - kInputDeadzone) / (kLowRangeReference - static_cast<float>(kInputDeadzone)));
    const float low_curve = std::pow(low_normalized, kLowRangeExponent) * kLowRangeMaxOutput;

    const float high_normalized = std::min(1.0f, static_cast<float>(magnitude) / static_cast<float>(kNominalForceMax));
    const float high_curve = std::sqrt(high_normalized) * 127.0f * kHighRangeGain;

    int level = static_cast<int>(std::lround(std::max(low_curve, high_curve)));
    level = std::min(127, std::max(0, level));
    if (signed_magnitude < 0) {
        level = -level;
    }
    return level;
}

int apply_constant_slew_limiter(int target_level, bool have_last_level, int last_level) {
    if (!have_last_level) {
        return target_level;
    }

    constexpr int kMaxStepSameDirection = 24;
    constexpr int kMaxStepSignFlip = 10;
    constexpr int kMicroFlipGate = 8;

    if (target_level != 0 && last_level != 0 &&
        (target_level * last_level) < 0 &&
        std::abs(target_level) <= kMicroFlipGate &&
        std::abs(last_level) <= kMicroFlipGate) {
        return 0;
    }

    int limited = target_level;
    const int max_step =
        ((target_level != 0 && last_level != 0 && (target_level * last_level) < 0) ? kMaxStepSignFlip : kMaxStepSameDirection);

    if (limited > last_level + max_step) {
        limited = last_level + max_step;
    } else if (limited < last_level - max_step) {
        limited = last_level - max_step;
    }

    if (std::abs(limited) <= 1) {
        limited = 0;
    }

    return limited;
}

}  // namespace

BridgeServer::BridgeServer(std::uint16_t port)
    : port_(port), stop_requested_(false), listen_fd_(-1), device_manager_() {
    status_.port = port_;
    status_.wheel_name = "Starting wheel service...";
}

BridgeServer::~BridgeServer() {
    stop();
}

bool BridgeServer::start() {
    if (server_thread_.joinable()) {
        return true;
    }

    stop_requested_.store(false);
    server_thread_ = std::thread(&BridgeServer::server_loop, this);
    return true;
}

void BridgeServer::stop() {
    stop_requested_.store(true);

    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    status_.listening = false;
    status_.client_connected = false;
    status_.client_name.clear();
    stop_wheel_forces_locked();
    disconnect_wheel_locked();
}

bool BridgeServer::reconnect_wheel() {
    return complete_wheel_connect_cycle(true);
}

BridgeServer::Status BridgeServer::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

BridgeServer::WheelConnectResult BridgeServer::ensure_wheel_connected() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!wheels_.empty()) {
            status_.wheel_connected = true;
            return WheelConnectResult::connected;
        }

        if (wheel_operation_in_progress_) {
            return WheelConnectResult::busy;
        }
    }

    if (complete_wheel_connect_cycle(false)) {
        return WheelConnectResult::connected;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return wheel_operation_in_progress_ ? WheelConnectResult::busy : WheelConnectResult::unavailable;
}

bool BridgeServer::complete_wheel_connect_cycle(bool force_reconnect) {
    std::vector<std::unique_ptr<WheelController>> previous_wheels;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!force_reconnect && !wheels_.empty()) {
            status_.wheel_connected = true;
            if (status_.wheel_name.empty()) {
                status_.wheel_name = "Logitech G923";
            }
            return true;
        }

        if (wheel_operation_in_progress_) {
            return false;
        }

        begin_wheel_operation_locked(force_reconnect ? "Reconnecting G923..." : "Connecting to G923...");
        if (force_reconnect) {
            previous_wheels = std::move(wheels_);
        }
    }

    previous_wheels.clear();

    auto wheels = device_manager_.find_known_wheels();
    if (wheels.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        finish_wheel_operation_locked({}, "No G923 detected");
        return false;
    }

    std::vector<std::unique_ptr<WheelController>> initialized_wheels;

    for (const auto& device : wheels) {
        auto controller = std::make_unique<WheelController>(device);
        if (!controller->initialize()) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.wheel_name = "Calibrating G923...";
        }

        if (!controller->calibrate()) {
            continue;
        }
        initialized_wheels.push_back(std::move(controller));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_wheels.empty()) {
        const std::string wheel_name = initialized_wheels.size() > 1
                                           ? "Logitech G923 (" + std::to_string(initialized_wheels.size()) + " interfaces)"
                                           : "Logitech G923";
        finish_wheel_operation_locked(std::move(initialized_wheels), wheel_name);
        return true;
    }

    finish_wheel_operation_locked({}, "Failed to calibrate G923");
    return false;
}

void BridgeServer::begin_wheel_operation_locked(const std::string& status_text) {
    wheel_operation_in_progress_ = true;
    status_.wheel_connected = false;
    status_.wheel_name = status_text;
}

void BridgeServer::finish_wheel_operation_locked(std::vector<std::unique_ptr<WheelController>> wheels,
                                                 const std::string& status_text) {
    wheels_ = std::move(wheels);
    wheel_operation_in_progress_ = false;
    status_.wheel_connected = !wheels_.empty();
    status_.wheel_name = status_text;
    have_last_wheel_state_ = false;
    last_wheel_state_ = g923bridge::WheelStatePayload{};
    last_constant_force_active_ = false;
    have_last_constant_level_ = false;
    last_constant_level_ = 0;
}

void BridgeServer::disconnect_wheel_locked() {
    wheels_.clear();
    wheel_operation_in_progress_ = false;
    status_.wheel_connected = false;
    last_constant_force_active_ = false;
    have_last_constant_level_ = false;
    last_constant_level_ = 0;
    have_last_wheel_state_ = false;
    last_wheel_state_ = g923bridge::WheelStatePayload{};
    if (status_.wheel_name.empty()) {
        status_.wheel_name = "Disconnected";
    }
}

void BridgeServer::stop_wheel_forces_locked() {
    if (wheels_.empty()) {
        return;
    }

    for (auto& wheel : wheels_) {
        if (!wheel || !wheel->is_initialized()) {
            continue;
        }
        wheel->stop_forces();
        wheel->disable_autocenter();
        wheel->set_custom_spring(0, 0, 0, 0, 0, 0, 0);
        wheel->set_damper(0, 0, 0, 0);
    }
    last_constant_force_active_ = false;
    have_last_constant_level_ = false;
    last_constant_level_ = 0;
    have_last_wheel_state_ = false;
    last_wheel_state_ = g923bridge::WheelStatePayload{};
}

void BridgeServer::server_loop() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return;
    }

    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listen_fd_, 1) != 0) {
        close_if_open(listen_fd_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.listening = true;
    }

    complete_wheel_connect_cycle(false);

    while (!stop_requested_.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd_, &read_fds);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

        const int ready = select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }

        int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        timeval client_timeout{};
        client_timeout.tv_sec = 1;
        client_timeout.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &client_timeout, sizeof(client_timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &client_timeout, sizeof(client_timeout));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.client_connected = true;
            status_.client_name = "Connected";
        }

        run_client_session(client_fd);
        close_if_open(client_fd);

        std::lock_guard<std::mutex> lock(mutex_);
        status_.client_connected = false;
        status_.client_name.clear();
        stop_wheel_forces_locked();
    }

    close_if_open(listen_fd_);
}

void BridgeServer::run_client_session(int client_fd) {
    while (!stop_requested_.load()) {
        g923bridge::MessageHeader header{};
        if (!recv_exact(client_fd, &header, sizeof(header))) {
            break;
        }

        if (header.magic != g923bridge::kProtocolMagic ||
            header.version != g923bridge::kProtocolVersion) {
            break;
        }

        if (!handle_message(client_fd, header)) {
            break;
        }
    }
}

bool BridgeServer::handle_message(int client_fd, const g923bridge::MessageHeader& header) {
    switch (static_cast<g923bridge::MessageType>(header.type)) {
        case g923bridge::MessageType::hello: {
            if (header.payload_size != sizeof(g923bridge::HelloPayload)) {
                return false;
            }

            g923bridge::HelloPayload payload{};
            if (!recv_exact(client_fd, &payload, sizeof(payload))) {
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_.client_name = payload.client_name;
            }

            return send_hello_ack(client_fd);
        }

        case g923bridge::MessageType::apply_wheel_state: {
            if (header.payload_size != sizeof(g923bridge::WheelStatePayload)) {
                return false;
            }

            g923bridge::WheelStatePayload payload{};
            if (!recv_exact(client_fd, &payload, sizeof(payload))) {
                return false;
            }

            const auto connect_result = ensure_wheel_connected();
            if (connect_result == WheelConnectResult::unavailable) {
                return false;
            }
            if (connect_result == WheelConnectResult::busy) {
                return true;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            return apply_wheel_state_locked(payload);
        }

        case g923bridge::MessageType::stop_all: {
            if (header.payload_size != 0) {
                std::array<std::uint8_t, 256> discard{};
                std::size_t remaining = header.payload_size;
                while (remaining > 0) {
                    const std::size_t chunk = std::min(remaining, discard.size());
                    if (!recv_exact(client_fd, discard.data(), chunk)) {
                        return false;
                    }
                    remaining -= chunk;
                }
            }

            std::lock_guard<std::mutex> lock(mutex_);
            stop_wheel_forces_locked();
            ++status_.packets_received;
            return true;
        }

        case g923bridge::MessageType::ping: {
            if (header.payload_size > 0) {
                std::array<std::uint8_t, 256> discard{};
                std::size_t remaining = header.payload_size;
                while (remaining > 0) {
                    const std::size_t chunk = std::min(remaining, discard.size());
                    if (!recv_exact(client_fd, discard.data(), chunk)) {
                        return false;
                    }
                    remaining -= chunk;
                }
            }
            return true;
        }

        case g923bridge::MessageType::set_led_pattern: {
            if (header.payload_size != sizeof(g923bridge::LedPatternPayload)) {
                return false;
            }

            g923bridge::LedPatternPayload payload{};
            if (!recv_exact(client_fd, &payload, sizeof(payload))) {
                return false;
            }

            const auto connect_result = ensure_wheel_connected();
            if (connect_result == WheelConnectResult::unavailable) {
                return false;
            }
            if (connect_result == WheelConnectResult::busy) {
                return true;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            return apply_led_pattern_locked(payload.pattern);
        }

        default:
            return false;
    }
}

bool BridgeServer::send_hello_ack(int client_fd) {
    g923bridge::HelloAckPayload payload{};
    payload.accepted = 1;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        payload.wheel_connected = status_.wheel_connected ? 1 : 0;
        payload.server_port = port_;
        std::strncpy(payload.wheel_name, status_.wheel_name.c_str(), sizeof(payload.wheel_name) - 1);
    }

    g923bridge::MessageHeader header{};
    header.type = static_cast<std::uint16_t>(g923bridge::MessageType::hello_ack);
    header.payload_size = sizeof(payload);

    return send_exact(client_fd, &header, sizeof(header)) &&
            send_exact(client_fd, &payload, sizeof(payload));
}

bool BridgeServer::apply_wheel_state_locked(const g923bridge::WheelStatePayload& payload) {
    if (wheel_operation_in_progress_) {
        return true;
    }

    if (wheels_.empty()) {
        status_.wheel_connected = false;
        return false;
    }

    const bool has_any_effect =
        payload.autocenter_enabled || payload.custom_spring_enabled ||
        payload.damper_enabled || payload.constant_force_enabled;
    int desired_constant_level = 0;
    if (payload.constant_force_enabled) {
        desired_constant_level = map_constant_magnitude_to_level(payload.constant_force_magnitude);
        desired_constant_level = apply_constant_slew_limiter(
            desired_constant_level, have_last_constant_level_, last_constant_level_);
    }
    const bool constant_active = desired_constant_level != 0;
    const bool constant_level_changed = constant_active
        ? (!have_last_constant_level_ || desired_constant_level != last_constant_level_)
        : last_constant_force_active_;

    if (!has_any_effect) {
        stop_wheel_forces_locked();
        have_last_wheel_state_ = true;
        last_wheel_state_ = payload;
        ++status_.packets_received;
        return true;
    }

    if (have_last_wheel_state_ &&
        std::memcmp(&payload, &last_wheel_state_, sizeof(payload)) == 0 &&
        !constant_level_changed) {
        ++status_.packets_received;
        return true;
    }

    const bool spring_changed =
        !have_last_wheel_state_ ||
        payload.custom_spring_enabled != last_wheel_state_.custom_spring_enabled ||
        payload.spring_deadband_left != last_wheel_state_.spring_deadband_left ||
        payload.spring_deadband_right != last_wheel_state_.spring_deadband_right ||
        payload.spring_k1 != last_wheel_state_.spring_k1 ||
        payload.spring_k2 != last_wheel_state_.spring_k2 ||
        payload.spring_sat1 != last_wheel_state_.spring_sat1 ||
        payload.spring_sat2 != last_wheel_state_.spring_sat2 ||
        payload.spring_clip != last_wheel_state_.spring_clip;
    const bool damper_changed =
        !have_last_wheel_state_ ||
        payload.damper_enabled != last_wheel_state_.damper_enabled ||
        payload.damper_force_positive != last_wheel_state_.damper_force_positive ||
        payload.damper_force_negative != last_wheel_state_.damper_force_negative ||
        payload.damper_saturation_positive != last_wheel_state_.damper_saturation_positive ||
        payload.damper_saturation_negative != last_wheel_state_.damper_saturation_negative;
    const bool autocenter_changed =
        !have_last_wheel_state_ ||
        payload.autocenter_enabled != last_wheel_state_.autocenter_enabled ||
        payload.autocenter_force != last_wheel_state_.autocenter_force ||
        payload.autocenter_slope != last_wheel_state_.autocenter_slope;
    const bool constant_command_changed = constant_level_changed;
    const bool led_changed =
        !have_last_wheel_state_ ||
        payload.led_pattern_enabled != last_wheel_state_.led_pattern_enabled ||
        payload.led_pattern != last_wheel_state_.led_pattern;

    bool applied_to_any_wheel = false;

    for (auto& wheel : wheels_) {
        if (!wheel || !wheel->is_initialized()) {
            continue;
        }

        bool wheel_ok = true;

        if (wheel_ok && spring_changed) {
            wheel_ok = wheel->set_custom_spring(
                payload.spring_deadband_left,
                payload.spring_deadband_right,
                payload.custom_spring_enabled ? payload.spring_k1 : 0,
                payload.custom_spring_enabled ? payload.spring_k2 : 0,
                payload.custom_spring_enabled ? payload.spring_sat1 : 0,
                payload.custom_spring_enabled ? payload.spring_sat2 : 0,
                payload.custom_spring_enabled ? payload.spring_clip : 0);
        }

        if (wheel_ok && damper_changed) {
            wheel_ok = wheel->set_damper(
                payload.damper_enabled ? payload.damper_force_positive : 0,
                payload.damper_enabled ? payload.damper_force_negative : 0,
                payload.damper_enabled ? payload.damper_saturation_positive : 0,
                payload.damper_enabled ? payload.damper_saturation_negative : 0);
        }

        if (wheel_ok && autocenter_changed) {
            if (payload.autocenter_enabled) {
                wheel_ok = wheel->enable_autocenter() &&
                          wheel->set_autocenter_spring(
                              payload.autocenter_slope, payload.autocenter_slope, payload.autocenter_force);
            } else {
                wheel_ok = wheel->disable_autocenter();
            }
        }

        if (wheel_ok && constant_command_changed) {
            if (constant_active) {
                const auto raw_level = static_cast<std::uint8_t>(
                    std::max(0, std::min(255, 128 + desired_constant_level)));
                wheel_ok = wheel->set_constant_force(raw_level);
            } else if (last_constant_force_active_) {
                wheel_ok = wheel->set_constant_force(128);
            }
        }

        if (wheel_ok && led_changed) {
            wheel->set_led_pattern(payload.led_pattern_enabled ? payload.led_pattern : 0);
        }

        if (wheel_ok) {
            applied_to_any_wheel = true;
        }
    }

    if (!applied_to_any_wheel) {
        status_.wheel_connected = false;
        return false;
    }

    last_constant_force_active_ = constant_active;
    if (constant_active) {
        have_last_constant_level_ = true;
        last_constant_level_ = desired_constant_level;
    } else {
        have_last_constant_level_ = false;
        last_constant_level_ = 0;
    }

    have_last_wheel_state_ = true;
    last_wheel_state_ = payload;
    ++status_.packets_received;
    return true;
}

bool BridgeServer::apply_led_pattern_locked(std::uint8_t pattern) {
    if (wheel_operation_in_progress_) {
        return true;
    }

    if (wheels_.empty()) {
        status_.wheel_connected = false;
        return false;
    }

    bool applied = false;
    for (auto& wheel : wheels_) {
        if (wheel && wheel->is_initialized()) {
            applied = wheel->set_led_pattern(pattern) || applied;
        }
    }

    if (applied) {
        if (!have_last_wheel_state_) {
            last_wheel_state_ = g923bridge::WheelStatePayload{};
        }
        last_wheel_state_.led_pattern_enabled = 1;
        last_wheel_state_.led_pattern = pattern;
        have_last_wheel_state_ = true;
    }

    ++status_.packets_received;
    return applied;
}
