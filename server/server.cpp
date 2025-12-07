#include "server.h"
#include "shell_executor.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>

RemoteServer::RemoteServer(uint16_t port) 
    : m_port(port)
    , m_server_socket(-1)
    , m_running(false) 
{}

RemoteServer::~RemoteServer() {
    stop();
}

bool RemoteServer::start() {
    // Создаём сокет
    m_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_socket < 0) {
        std::cerr << "Error: Cannot create socket" << std::endl;
        return false;
    }
    
    // Разрешаем переиспользование адреса
    int opt = 1;
    setsockopt(m_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Привязываем к порту
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);
    
    if (bind(m_server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Cannot bind to port " << m_port << std::endl;
        close(m_server_socket);
        return false;
    }
    
    // Начинаем слушать
    if (listen(m_server_socket, 5) < 0) {
        std::cerr << "Error: Cannot listen on socket" << std::endl;
        close(m_server_socket);
        return false;
    }
    
    m_running = true;
    std::cout << "Server started on port " << m_port << std::endl;
    
    // Принимаем подключения
    acceptConnections();
    
    return true;
}

void RemoteServer::stop() {
    m_running = false;
    if (m_server_socket >= 0) {
        close(m_server_socket);
        m_server_socket = -1;
    }
}

void RemoteServer::acceptConnections() {
    while (m_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(m_server_socket, (sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (m_running) {
                std::cerr << "Error: Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected: " << client_ip << std::endl;
        
        // Обрабатываем клиента в отдельном потоке
        std::thread(&RemoteServer::handleClient, this, client_socket).detach();
    }
}

void RemoteServer::handleClient(int client_socket) {
    Protocol::PacketHeader header;
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    
    while (m_running) {
        // Читаем заголовок
        if (!recvAll(client_socket, header_buffer.data(), Protocol::HEADER_SIZE)) {
            break;
        }
        
        if (!Protocol::parseHeader(header_buffer.data(), header)) {
            std::cerr << "Error: Invalid packet header" << std::endl;
            break;
        }
        
        // Читаем payload
        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!recvAll(client_socket, payload.data(), header.payload_size)) {
                break;
            }
        }
        
        // Обрабатываем сообщение
        switch (header.type) {
            case Protocol::MessageType::COMMAND: {
                std::string command(payload.begin(), payload.end());
                std::cout << "Executing: " << command << std::endl;
                
                auto result = ShellExecutor::execute(command);
                
                // Формируем ответ: exit_code + output
                std::string response = std::to_string(result.exit_code) + "\n" + result.output;
                auto packet = Protocol::createPacket(Protocol::MessageType::RESPONSE, response);
                
                if (!sendAll(client_socket, packet.data(), packet.size())) {
                    goto cleanup;
                }
                break;
            }
            
            case Protocol::MessageType::HEARTBEAT: {
                auto packet = Protocol::createPacket(Protocol::MessageType::HEARTBEAT, "pong");
                sendAll(client_socket, packet.data(), packet.size());
                break;
            }
            
            case Protocol::MessageType::DISCONNECT:
                goto cleanup;
                
            default:
                std::cerr << "Unknown message type" << std::endl;
        }
    }
    
cleanup:
    std::cout << "Client disconnected" << std::endl;
    close(client_socket);
}

bool RemoteServer::sendAll(int socket, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(socket, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RemoteServer::recvAll(int socket, uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(socket, data + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

