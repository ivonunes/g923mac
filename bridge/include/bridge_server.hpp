#pragma once

#include "ffb_bridge_protocol.hpp"
#include "wheel.hpp"
#include "device.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class BridgeServer {
public:
    struct Status {
        bool listening = false;
        bool client_connected = false;
        bool wheel_connected = false;
        std::uint16_t port = g923bridge::kDefaultPort;
        std::string client_name;
        std::string wheel_name;
        std::uint64_t packets_received = 0;
    };

    explicit BridgeServer(std::uint16_t port = g923bridge::kDefaultPort);
    ~BridgeServer();

    bool start();
    void stop();

    bool reconnect_wheel();
    Status status() const;

private:
    enum class WheelConnectResult {
        connected,
        unavailable,
        busy,
    };

    WheelConnectResult ensure_wheel_connected();
    bool complete_wheel_connect_cycle(bool force_reconnect);
    void begin_wheel_operation_locked(const std::string& status_text);
    void finish_wheel_operation_locked(std::vector<std::unique_ptr<WheelController>> wheels,
                                       const std::string& status_text);
    void disconnect_wheel_locked();
    void stop_wheel_forces_locked();

    void server_loop();
    void run_client_session(int client_fd);

    bool handle_message(int client_fd, const g923bridge::MessageHeader& header);
    bool send_hello_ack(int client_fd);
    bool apply_wheel_state_locked(const g923bridge::WheelStatePayload& payload);
    bool apply_led_pattern_locked(std::uint8_t pattern);

    std::uint16_t port_;
    mutable std::mutex mutex_;
    std::atomic<bool> stop_requested_;
    std::thread server_thread_;

    int listen_fd_;
    DeviceManager device_manager_;
    std::vector<std::unique_ptr<WheelController>> wheels_;
    bool wheel_operation_in_progress_ = false;
    bool last_constant_force_active_ = false;
    bool have_last_constant_level_ = false;
    int last_constant_level_ = 0;
    bool have_last_wheel_state_ = false;
    g923bridge::WheelStatePayload last_wheel_state_{};

    Status status_;
};
