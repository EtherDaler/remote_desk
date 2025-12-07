#include "agent.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <array>
#include <sys/utsname.h>

RemoteAgent::RemoteAgent(const std::string& relay_host, uint16_t relay_port,
                         const std::string& agent_id, const std::string& agent_name)
    : m_relay_host(relay_host)
    , m_relay_port(relay_port)
    , m_agent_id(agent_id)
    , m_agent_name(agent_name)
    , m_socket(-1)
    , m_running(false)
    , m_connected(false)
{}

RemoteAgent::~RemoteAgent() {
    stop();
}

std::string RemoteAgent::getOsInfo() {
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string(info.sysname) + " " + info.release;
    }
    return "Unknown";
}

bool RemoteAgent::connect() {
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        std::cerr << "[AGENT] Error: Cannot create socket" << std::endl;
        return false;
    }
    
    struct hostent* server = gethostbyname(m_relay_host.c_str());
    if (!server) {
        std::cerr << "[AGENT] Error: Cannot resolve host " << m_relay_host << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(m_relay_port);
    
    if (::connect(m_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[AGENT] Error: Cannot connect to relay " << m_relay_host << ":" << m_relay_port << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    std::cout << "[AGENT] Connected to relay server" << std::endl;
    
    // Регистрируемся
    std::string os_info = getOsInfo();
    std::string register_payload = m_agent_id + "|" + m_agent_name + "|" + os_info;
    
    if (!sendPacket(static_cast<uint8_t>(Protocol::MessageType::AGENT_REGISTER), register_payload)) {
        std::cerr << "[AGENT] Error: Failed to send registration" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Ждём подтверждение
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    if (!recvAll(header_buffer.data(), Protocol::HEADER_SIZE)) {
        std::cerr << "[AGENT] Error: Failed to receive registration response" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    Protocol::PacketHeader header;
    Protocol::parseHeader(header_buffer.data(), header);
    
    if (header.type != Protocol::MessageType::AGENT_REGISTERED) {
        std::cerr << "[AGENT] Error: Registration failed" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Пропускаем payload
    if (header.payload_size > 0) {
        std::vector<uint8_t> payload(header.payload_size);
        recvAll(payload.data(), header.payload_size);
    }
    
    std::cout << "[AGENT] Registered as: " << m_agent_name << " (" << m_agent_id << ")" << std::endl;
    m_connected = true;
    return true;
}

void RemoteAgent::run() {
    m_running = true;
    
    while (m_running) {
        if (!m_connected) {
            std::cout << "[AGENT] Connecting to relay..." << std::endl;
            if (!connect()) {
                std::cout << "[AGENT] Reconnecting in 5 seconds..." << std::endl;
                sleep(5);
                continue;
            }
        }
        
        handleCommands();
        
        // Если отключились, пробуем переподключиться
        if (m_running) {
            m_connected = false;
            std::cout << "[AGENT] Disconnected, reconnecting in 5 seconds..." << std::endl;
            sleep(5);
        }
    }
}

void RemoteAgent::handleCommands() {
    std::vector<uint8_t> header_buffer(Protocol::HEADER_SIZE);
    
    while (m_running && m_connected) {
        if (!recvAll(header_buffer.data(), Protocol::HEADER_SIZE)) {
            break;
        }
        
        Protocol::PacketHeader header;
        if (!Protocol::parseHeader(header_buffer.data(), header)) {
            break;
        }
        
        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!recvAll(payload.data(), header.payload_size)) {
                break;
            }
        }
        
        std::string payload_str(payload.begin(), payload.end());
        
        switch (header.type) {
            case Protocol::MessageType::COMMAND: {
                std::cout << "[AGENT] Executing: " << payload_str << std::endl;
                std::string result = executeCommand(payload_str);
                sendPacket(static_cast<uint8_t>(Protocol::MessageType::RESPONSE), result);
                break;
            }
            
            case Protocol::MessageType::HEARTBEAT: {
                sendPacket(static_cast<uint8_t>(Protocol::MessageType::HEARTBEAT), "pong");
                break;
            }
            
            case Protocol::MessageType::DISCONNECT: {
                m_connected = false;
                return;
            }
            
            default:
                break;
        }
    }
    
    m_connected = false;
}

std::string RemoteAgent::executeCommand(const std::string& command) {
    std::array<char, 4096> buffer;
    std::string output;
    int exit_code = 0;
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "-1\nError: Failed to execute command";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    
    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = -1;
    }
    
    return std::to_string(exit_code) + "\n" + output;
}

void RemoteAgent::stop() {
    m_running = false;
    m_connected = false;
    
    if (m_socket >= 0) {
        sendPacket(static_cast<uint8_t>(Protocol::MessageType::DISCONNECT), "");
        close(m_socket);
        m_socket = -1;
    }
}

bool RemoteAgent::sendAll(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(m_socket, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RemoteAgent::recvAll(uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(m_socket, data + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool RemoteAgent::sendPacket(uint8_t msg_type, const std::string& payload) {
    auto packet = Protocol::createPacket(static_cast<Protocol::MessageType>(msg_type), payload);
    return sendAll(packet.data(), packet.size());
}

