#pragma once

#include "ffb_bridge_protocol.hpp"
#include <winsock2.h>
#include <windows.h>

class BridgeClient {
public:
    void initialize();
    void shutdown();

    bool send_hello(const char* client_name, std::uint32_t process_id);
    bool send_state(const g923bridge::WheelStatePayload& state);
    bool send_stop_all();

private:
    bool ensure_connected_locked();
    bool send_message_locked(g923bridge::MessageType type, const void* payload, std::uint32_t payload_size);
    void disconnect_locked();

    CRITICAL_SECTION lock_;
    SOCKET socket_;
    bool hello_sent_;
    char last_client_name_[64];
    std::uint32_t last_process_id_;
    bool initialized_;
};
