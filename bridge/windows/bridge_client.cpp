#include "bridge_client.hpp"
#include <windows.h>
#include <ws2tcpip.h>
#include <cstring>

namespace {

bool send_exact(SOCKET socket_handle, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;

    while (sent < size) {
        const int chunk = send(socket_handle, bytes + sent, static_cast<int>(size - sent), 0);
        if (chunk <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(chunk);
    }

    return true;
}

bool recv_exact(SOCKET socket_handle, void* data, std::size_t size) {
    auto* bytes = static_cast<char*>(data);
    std::size_t received = 0;

    while (received < size) {
        const int chunk = recv(socket_handle, bytes + received, static_cast<int>(size - received), 0);
        if (chunk <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(chunk);
    }

    return true;
}

void copy_c_string(char* destination, std::size_t destination_size, const char* source) {
    if (!destination || destination_size == 0) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    const std::size_t source_length = std::strlen(source);
    const std::size_t copy_length = (source_length < (destination_size - 1)) ? source_length : (destination_size - 1);
    std::memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
}

}  // namespace

void BridgeClient::initialize() {
    if (initialized_) {
        return;
    }

    InitializeCriticalSection(&lock_);
    socket_ = INVALID_SOCKET;
    hello_sent_ = false;
    std::memset(last_client_name_, 0, sizeof(last_client_name_));
    last_process_id_ = 0;
    WSADATA wsa_data{};
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    initialized_ = true;
}

void BridgeClient::shutdown() {
    if (!initialized_) {
        return;
    }

    EnterCriticalSection(&lock_);
    disconnect_locked();
    LeaveCriticalSection(&lock_);
    DeleteCriticalSection(&lock_);
    WSACleanup();
    initialized_ = false;
}

bool BridgeClient::send_hello(const char* client_name, std::uint32_t process_id) {
    if (!initialized_) {
        return false;
    }

    EnterCriticalSection(&lock_);
    const char* effective_name = (client_name && client_name[0]) ? client_name : "G923FFBProxy";
    copy_c_string(last_client_name_, sizeof(last_client_name_), effective_name);
    last_process_id_ = process_id;

    if (!ensure_connected_locked()) {
        LeaveCriticalSection(&lock_);
        return false;
    }

    g923bridge::HelloPayload payload{};
    copy_c_string(payload.client_name, sizeof(payload.client_name), last_client_name_);
    payload.process_id = process_id;

    if (!send_message_locked(g923bridge::MessageType::hello, &payload, sizeof(payload))) {
        disconnect_locked();
        LeaveCriticalSection(&lock_);
        return false;
    }

    g923bridge::MessageHeader header{};
    g923bridge::HelloAckPayload ack{};
    if (!recv_exact(socket_, &header, sizeof(header)) ||
        header.magic != g923bridge::kProtocolMagic ||
        header.version != g923bridge::kProtocolVersion ||
        header.type != static_cast<std::uint16_t>(g923bridge::MessageType::hello_ack) ||
        header.payload_size != sizeof(ack) ||
        !recv_exact(socket_, &ack, sizeof(ack)) ||
        !ack.accepted) {
        disconnect_locked();
        LeaveCriticalSection(&lock_);
        return false;
    }

    hello_sent_ = true;
    LeaveCriticalSection(&lock_);
    return true;
}

bool BridgeClient::send_state(const g923bridge::WheelStatePayload& state) {
    if (!initialized_) {
        return false;
    }

    EnterCriticalSection(&lock_);
    if (!ensure_connected_locked()) {
        LeaveCriticalSection(&lock_);
        return false;
    }

    if (!hello_sent_) {
        g923bridge::HelloPayload hello{};
        const char* client_name = last_client_name_[0] ? last_client_name_ : "G923FFBProxy";
        copy_c_string(hello.client_name, sizeof(hello.client_name), client_name);
        hello.process_id = last_process_id_;

        if (!send_message_locked(g923bridge::MessageType::hello, &hello, sizeof(hello))) {
            disconnect_locked();
            LeaveCriticalSection(&lock_);
            return false;
        }

        g923bridge::MessageHeader header{};
        g923bridge::HelloAckPayload ack{};
        if (!recv_exact(socket_, &header, sizeof(header)) ||
            header.magic != g923bridge::kProtocolMagic ||
            header.version != g923bridge::kProtocolVersion ||
            header.type != static_cast<std::uint16_t>(g923bridge::MessageType::hello_ack) ||
            header.payload_size != sizeof(ack) ||
            !recv_exact(socket_, &ack, sizeof(ack)) ||
            !ack.accepted) {
            disconnect_locked();
            LeaveCriticalSection(&lock_);
            return false;
        }

        hello_sent_ = true;
    }

    if (!send_message_locked(g923bridge::MessageType::apply_wheel_state, &state, sizeof(state))) {
        disconnect_locked();
        LeaveCriticalSection(&lock_);
        return false;
    }

    LeaveCriticalSection(&lock_);
    return true;
}

bool BridgeClient::send_stop_all() {
    if (!initialized_) {
        return false;
    }

    EnterCriticalSection(&lock_);
    if (!ensure_connected_locked()) {
        LeaveCriticalSection(&lock_);
        return false;
    }

    if (!hello_sent_) {
        g923bridge::HelloPayload hello{};
        const char* client_name = last_client_name_[0] ? last_client_name_ : "G923FFBProxy";
        copy_c_string(hello.client_name, sizeof(hello.client_name), client_name);
        hello.process_id = last_process_id_;

        if (!send_message_locked(g923bridge::MessageType::hello, &hello, sizeof(hello))) {
            disconnect_locked();
            LeaveCriticalSection(&lock_);
            return false;
        }

        g923bridge::MessageHeader header{};
        g923bridge::HelloAckPayload ack{};
        if (!recv_exact(socket_, &header, sizeof(header)) ||
            header.magic != g923bridge::kProtocolMagic ||
            header.version != g923bridge::kProtocolVersion ||
            header.type != static_cast<std::uint16_t>(g923bridge::MessageType::hello_ack) ||
            header.payload_size != sizeof(ack) ||
            !recv_exact(socket_, &ack, sizeof(ack)) ||
            !ack.accepted) {
            disconnect_locked();
            LeaveCriticalSection(&lock_);
            return false;
        }

        hello_sent_ = true;
    }

    if (!send_message_locked(g923bridge::MessageType::stop_all, nullptr, 0)) {
        disconnect_locked();
        LeaveCriticalSection(&lock_);
        return false;
    }

    LeaveCriticalSection(&lock_);
    return true;
}

bool BridgeClient::ensure_connected_locked() {
    if (socket_ != INVALID_SOCKET) {
        return true;
    }

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(g923bridge::kDefaultPort);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        disconnect_locked();
        return false;
    }

    return true;
}

bool BridgeClient::send_message_locked(g923bridge::MessageType type, const void* payload, std::uint32_t payload_size) {
    g923bridge::MessageHeader header{};
    header.type = static_cast<std::uint16_t>(type);
    header.payload_size = payload_size;

    if (!send_exact(socket_, &header, sizeof(header))) {
        return false;
    }

    if (payload_size > 0 && payload != nullptr) {
        return send_exact(socket_, payload, payload_size);
    }

    return true;
}

void BridgeClient::disconnect_locked() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    hello_sent_ = false;
}
