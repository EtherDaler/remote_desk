#include "client.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

RemoteClient::RemoteClient() : m_socket(-1) {}

RemoteClient::~RemoteClient() {
    disconnect();
}

bool RemoteClient::connect(const std::string& host, uint16_t port) {
    // Создаём сокет
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        std::cerr << "Error: Cannot create socket" << std::endl;
        return false;
    }
    
    // Резолвим хост
    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        std::cerr << "Error: Cannot resolve host " << host << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Настраиваем адрес
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);
    
    // Подключаемся
    if (::connect(m_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Cannot connect to " << host << ":" << port << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    std::cout << "Connected to " << host << ":" << port << std::endl;
    return true;
}

void RemoteClient::disconnect() {
    if (m_socket >= 0) {
        // Отправляем сообщение об отключении
        auto packet = Protocol::createPacket(Protocol::MessageType::DISCONNECT, "");
        sendAll(packet.data(), packet.size());
        
        close(m_socket);
        m_socket = -1;
    }
}

std::string RemoteClient::executeCommand(const std::string& command) {
    if (!isConnected()) {
        return "Error: Not connected";
    }
    
    // Отправляем команду
    auto packet = Protocol::createPacket(Protocol::MessageType::COMMAND, command);
    if (!sendAll(packet.data(), packet.size())) {
        return "Error: Failed to send command";
    }
    
    // Получаем заголовок ответа
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    if (!recvAll(header_buffer.data(), Protocol::HEADER_SIZE)) {
        return "Error: Failed to receive response header";
    }
    
    Protocol::PacketHeader header;
    if (!Protocol::parseHeader(header_buffer.data(), header)) {
        return "Error: Invalid response header";
    }
    
    // Получаем тело ответа
    std::vector<uint8_t> payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(payload.data(), header.payload_size)) {
            return "Error: Failed to receive response body";
        }
    }
    
    return std::string(payload.begin(), payload.end());
}

bool RemoteClient::sendAll(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(m_socket, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RemoteClient::recvAll(uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(m_socket, data + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

