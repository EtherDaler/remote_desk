#include "agent.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <array>

// Кросс-платформенные заголовки
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    #define close closesocket
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/utsname.h>
#endif

RemoteAgent::RemoteAgent(const std::string& relay_host, uint16_t relay_port,
                         const std::string& agent_id, const std::string& agent_name)
    : m_relay_host(relay_host)
    , m_relay_port(relay_port)
    , m_agent_id(agent_id)
    , m_agent_name(agent_name)
    , m_socket(-1)
    , m_running(false)
    , m_connected(false)
{
#ifdef _WIN32
    // Инициализация Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

RemoteAgent::~RemoteAgent() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

std::string RemoteAgent::getOsInfo() {
#ifdef _WIN32
    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOA));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    
    // Используем простой способ определения версии
    return "Windows";
#else
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string(info.sysname) + " " + info.release;
    }
    return "Unknown";
#endif
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
    
    if (!sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::AGENT_REGISTER), register_payload)) {
        std::cerr << "[AGENT] Error: Failed to send registration" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Ждём подтверждение
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    if (!recvAll(header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        std::cerr << "[AGENT] Error: Failed to receive registration response" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    RemoteProto::PacketHeader header;
    RemoteProto::parseHeader(header_buffer.data(), header);
    
    if (header.type != RemoteProto::MessageType::AGENT_REGISTERED) {
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
#ifdef _WIN32
                Sleep(5000);
#else
                sleep(5);
#endif
                continue;
            }
        }
        
        handleCommands();
        
        // Если отключились, пробуем переподключиться
        if (m_running) {
            m_connected = false;
            std::cout << "[AGENT] Disconnected, reconnecting in 5 seconds..." << std::endl;
#ifdef _WIN32
            Sleep(5000);
#else
            sleep(5);
#endif
        }
    }
}

void RemoteAgent::handleCommands() {
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    
    while (m_running && m_connected) {
        if (!recvAll(header_buffer.data(), RemoteProto::HEADER_SIZE)) {
            break;
        }
        
        RemoteProto::PacketHeader header;
        if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
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
            case RemoteProto::MessageType::COMMAND: {
                std::cout << "[AGENT] Executing: " << payload_str << std::endl;
                std::string result = executeCommand(payload_str);
                sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::RESPONSE), result);
                break;
            }
            
            case RemoteProto::MessageType::HEARTBEAT: {
                sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::HEARTBEAT), "pong");
                break;
            }
            
            case RemoteProto::MessageType::DISCONNECT: {
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
    
#ifdef _WIN32
    // На Windows используем _popen/_pclose
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) {
        return "-1\nError: Failed to execute command";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    
    exit_code = _pclose(pipe);
#else
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
#endif
    
    return std::to_string(exit_code) + "\n" + output;
}

void RemoteAgent::stop() {
    m_running = false;
    m_connected = false;
    
    if (m_socket >= 0) {
        sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::DISCONNECT), "");
        close(m_socket);
        m_socket = -1;
    }
}

bool RemoteAgent::sendAll(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        int n = send(m_socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(size - sent), 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RemoteAgent::recvAll(uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        int n = recv(m_socket, reinterpret_cast<char*>(data + received), static_cast<int>(size - received), 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool RemoteAgent::sendPacket(uint8_t msg_type, const std::string& payload) {
    auto packet = RemoteProto::createPacket(static_cast<RemoteProto::MessageType>(msg_type), payload);
    return sendAll(packet.data(), packet.size());
}
