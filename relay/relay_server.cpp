#include "relay_server.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

RelayServer::RelayServer(uint16_t port, const std::string& admin_token)
    : m_port(port)
    , m_admin_token(admin_token)
    , m_server_socket(-1)
    , m_running(false)
{}

RelayServer::~RelayServer() {
    stop();
}

bool RelayServer::start() {
    m_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_socket < 0) {
        std::cerr << "[RELAY] Error: Cannot create socket" << std::endl;
        return false;
    }
    
    int opt = 1;
    setsockopt(m_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);
    
    if (bind(m_server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[RELAY] Error: Cannot bind to port " << m_port << std::endl;
        close(m_server_socket);
        return false;
    }
    
    if (listen(m_server_socket, 10) < 0) {
        std::cerr << "[RELAY] Error: Cannot listen on socket" << std::endl;
        close(m_server_socket);
        return false;
    }
    
    m_running = true;
    std::cout << "[RELAY] Server started on port " << m_port << std::endl;
    std::cout << "[RELAY] Admin token: " << m_admin_token << std::endl;
    
    acceptConnections();
    return true;
}

void RelayServer::stop() {
    m_running = false;
    if (m_server_socket >= 0) {
        close(m_server_socket);
        m_server_socket = -1;
    }
}

void RelayServer::acceptConnections() {
    while (m_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(m_server_socket, (sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (m_running) {
                std::cerr << "[RELAY] Error: Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "[RELAY] New connection from: " << client_ip << std::endl;
        
        std::thread(&RelayServer::handleConnection, this, client_socket).detach();
    }
}

void RelayServer::handleConnection(int client_socket) {
    // Ждём первый пакет для определения типа клиента
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    
    if (!recvAll(client_socket, header_buffer.data(), Protocol::HEADER_SIZE)) {
        close(client_socket);
        return;
    }
    
    Protocol::PacketHeader header;
    if (!Protocol::parseHeader(header_buffer.data(), header)) {
        close(client_socket);
        return;
    }
    
    std::vector<uint8_t> payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(client_socket, payload.data(), header.payload_size)) {
            close(client_socket);
            return;
        }
    }
    
    std::string payload_str(payload.begin(), payload.end());
    
    if (header.type == Protocol::MessageType::AGENT_REGISTER) {
        // Агент регистрируется: payload = "id|name|os"
        auto info = Protocol::AgentInfo::deserialize(payload_str + "|1");
        
        std::cout << "[RELAY] Agent registered: " << info.name << " (" << info.id << ")" << std::endl;
        
        // Отправляем подтверждение
        sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::AGENT_REGISTERED), "OK");
        
        // Добавляем в список
        {
            std::lock_guard<std::mutex> lock(m_agents_mutex);
            auto agent = std::make_shared<ConnectedAgent>();
            agent->socket = client_socket;
            agent->id = info.id;
            agent->name = info.name;
            agent->os = info.os;
            agent->online = true;
            m_agents[info.id] = agent;
        }
        
        handleAgent(client_socket, info.id);
        
    } else if (header.type == Protocol::MessageType::ADMIN_AUTH) {
        // Админ авторизуется: payload = token
        if (payload_str == m_admin_token) {
            std::cout << "[RELAY] Admin authenticated" << std::endl;
            sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::ADMIN_AUTHED), "OK");
            
            {
                std::lock_guard<std::mutex> lock(m_admins_mutex);
                auto admin = std::make_shared<ConnectedAdmin>();
                admin->socket = client_socket;
                m_admins[client_socket] = admin;
            }
            
            handleAdmin(client_socket);
        } else {
            std::cout << "[RELAY] Admin auth failed" << std::endl;
            sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::ERROR), "Invalid token");
            close(client_socket);
        }
    } else {
        std::cerr << "[RELAY] Unknown client type" << std::endl;
        close(client_socket);
    }
}

void RelayServer::handleAgent(int client_socket, const std::string& agent_id) {
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    
    while (m_running) {
        // Агент просто держит соединение и отвечает на команды
        // Команды приходят от relay, когда админ их отправляет
        if (!recvAll(client_socket, header_buffer.data(), Protocol::HEADER_SIZE)) {
            break;
        }
        
        Protocol::PacketHeader header;
        if (!Protocol::parseHeader(header_buffer.data(), header)) {
            break;
        }
        
        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!recvAll(client_socket, payload.data(), header.payload_size)) {
                break;
            }
        }
        
        if (header.type == Protocol::MessageType::HEARTBEAT) {
            sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::HEARTBEAT), "pong");
        } else if (header.type == Protocol::MessageType::DISCONNECT) {
            break;
        }
        // RESPONSE обрабатывается в forwardCommandToAgent
    }
    
    // Удаляем агента
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        m_agents.erase(agent_id);
    }
    std::cout << "[RELAY] Agent disconnected: " << agent_id << std::endl;
    close(client_socket);
}

void RelayServer::handleAdmin(int client_socket) {
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    std::shared_ptr<ConnectedAdmin> admin;
    
    {
        std::lock_guard<std::mutex> lock(m_admins_mutex);
        admin = m_admins[client_socket];
    }
    
    while (m_running) {
        if (!recvAll(client_socket, header_buffer.data(), Protocol::HEADER_SIZE)) {
            break;
        }
        
        Protocol::PacketHeader header;
        if (!Protocol::parseHeader(header_buffer.data(), header)) {
            break;
        }
        
        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!recvAll(client_socket, payload.data(), header.payload_size)) {
                break;
            }
        }
        
        std::string payload_str(payload.begin(), payload.end());
        
        switch (header.type) {
            case Protocol::MessageType::LIST_AGENTS: {
                std::string list = getAgentsList();
                sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::AGENTS_LIST), list);
                break;
            }
            
            case Protocol::MessageType::SELECT_AGENT: {
                std::lock_guard<std::mutex> lock(m_agents_mutex);
                if (m_agents.find(payload_str) != m_agents.end()) {
                    admin->selected_agent_id = payload_str;
                    sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::AGENT_SELECTED), payload_str);
                    std::cout << "[RELAY] Admin selected agent: " << payload_str << std::endl;
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::AGENT_OFFLINE), payload_str);
                }
                break;
            }
            
            case Protocol::MessageType::COMMAND: {
                if (admin->selected_agent_id.empty()) {
                    sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::ERROR), "No agent selected");
                    break;
                }
                
                std::string response;
                if (forwardCommandToAgent(admin->selected_agent_id, payload_str, response)) {
                    sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::RESPONSE), response);
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(Protocol::MessageType::AGENT_OFFLINE), admin->selected_agent_id);
                    admin->selected_agent_id.clear();
                }
                break;
            }
            
            case Protocol::MessageType::DISCONNECT:
                goto cleanup;
                
            default:
                break;
        }
    }
    
cleanup:
    {
        std::lock_guard<std::mutex> lock(m_admins_mutex);
        m_admins.erase(client_socket);
    }
    std::cout << "[RELAY] Admin disconnected" << std::endl;
    close(client_socket);
}

std::string RelayServer::getAgentsList() {
    std::lock_guard<std::mutex> lock(m_agents_mutex);
    std::string result;
    
    for (const auto& [id, agent] : m_agents) {
        Protocol::AgentInfo info;
        info.id = agent->id;
        info.name = agent->name;
        info.os = agent->os;
        info.online = agent->online;
        
        if (!result.empty()) {
            result += "\n";
        }
        result += info.serialize();
    }
    
    return result;
}

bool RelayServer::forwardCommandToAgent(const std::string& agent_id, const std::string& command, std::string& response) {
    std::shared_ptr<ConnectedAgent> agent;
    
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        auto it = m_agents.find(agent_id);
        if (it == m_agents.end()) {
            return false;
        }
        agent = it->second;
    }
    
    std::lock_guard<std::mutex> lock(agent->socket_mutex);
    
    // Отправляем команду агенту
    if (!sendPacket(agent->socket, static_cast<uint8_t>(Protocol::MessageType::COMMAND), command)) {
        return false;
    }
    
    // Ждём ответ
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    if (!recvAll(agent->socket, header_buffer.data(), Protocol::HEADER_SIZE)) {
        return false;
    }
    
    Protocol::PacketHeader header;
    if (!Protocol::parseHeader(header_buffer.data(), header)) {
        return false;
    }
    
    std::vector<uint8_t> payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(agent->socket, payload.data(), header.payload_size)) {
            return false;
        }
    }
    
    response = std::string(payload.begin(), payload.end());
    return true;
}

bool RelayServer::sendAll(int socket, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(socket, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RelayServer::recvAll(int socket, uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(socket, data + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool RelayServer::sendPacket(int socket, uint8_t msg_type, const std::string& payload) {
    auto packet = Protocol::createPacket(static_cast<Protocol::MessageType>(msg_type), payload);
    return sendAll(socket, packet.data(), packet.size());
}

