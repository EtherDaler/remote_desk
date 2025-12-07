#include "admin_client.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sstream>

AdminClient::AdminClient() : m_socket(-1) {}

AdminClient::~AdminClient() {
    disconnect();
}

bool AdminClient::connect(const std::string& host, uint16_t port, const std::string& token) {
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        std::cerr << "Error: Cannot create socket" << std::endl;
        return false;
    }
    
    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        std::cerr << "Error: Cannot resolve host " << host << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);
    
    if (::connect(m_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error: Cannot connect to " << host << ":" << port << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Авторизуемся
    if (!sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::ADMIN_AUTH), token)) {
        std::cerr << "Error: Failed to send auth" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    RemoteProto::PacketHeader header;
    std::vector<uint8_t> payload;
    if (!recvPacket(header, payload)) {
        std::cerr << "Error: Failed to receive auth response" << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    if (header.type != RemoteProto::MessageType::ADMIN_AUTHED) {
        std::string error(payload.begin(), payload.end());
        std::cerr << "Error: Authentication failed - " << error << std::endl;
        close(m_socket);
        m_socket = -1;
        return false;
    }
    
    return true;
}

void AdminClient::disconnect() {
    if (m_socket >= 0) {
        sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::DISCONNECT), "");
        close(m_socket);
        m_socket = -1;
    }
    m_selected_agent.clear();
}

std::vector<RemoteProto::AgentInfo> AdminClient::listAgents() {
    std::vector<RemoteProto::AgentInfo> agents;
    
    if (!isConnected()) return agents;
    
    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::LIST_AGENTS), "");
    
    RemoteProto::PacketHeader header;
    std::vector<uint8_t> payload;
    if (!recvPacket(header, payload)) {
        return agents;
    }
    
    if (header.type != RemoteProto::MessageType::AGENTS_LIST) {
        return agents;
    }
    
    std::string data(payload.begin(), payload.end());
    std::istringstream stream(data);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            agents.push_back(RemoteProto::AgentInfo::deserialize(line));
        }
    }
    
    return agents;
}

bool AdminClient::selectAgent(const std::string& agent_id) {
    if (!isConnected()) return false;
    
    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::SELECT_AGENT), agent_id);
    
    RemoteProto::PacketHeader header;
    std::vector<uint8_t> payload;
    if (!recvPacket(header, payload)) {
        return false;
    }
    
    if (header.type == RemoteProto::MessageType::AGENT_SELECTED) {
        m_selected_agent = agent_id;
        return true;
    } else if (header.type == RemoteProto::MessageType::AGENT_OFFLINE) {
        std::cerr << "Agent is offline" << std::endl;
        return false;
    }
    
    return false;
}

std::string AdminClient::executeCommand(const std::string& command) {
    if (!isConnected()) {
        return "Error: Not connected";
    }
    
    if (m_selected_agent.empty()) {
        return "Error: No agent selected";
    }
    
    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::COMMAND), command);
    
    RemoteProto::PacketHeader header;
    std::vector<uint8_t> payload;
    if (!recvPacket(header, payload)) {
        return "Error: Failed to receive response";
    }
    
    if (header.type == RemoteProto::MessageType::AGENT_OFFLINE) {
        m_selected_agent.clear();
        return "Error: Agent went offline";
    }
    
    if (header.type == RemoteProto::MessageType::ERROR) {
        return "Error: " + std::string(payload.begin(), payload.end());
    }
    
    return std::string(payload.begin(), payload.end());
}

bool AdminClient::sendAll(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(m_socket, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool AdminClient::recvAll(uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(m_socket, data + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool AdminClient::sendPacket(uint8_t msg_type, const std::string& payload) {
    auto packet = RemoteProto::createPacket(static_cast<RemoteProto::MessageType>(msg_type), payload);
    return sendAll(packet.data(), packet.size());
}

bool AdminClient::recvPacket(RemoteProto::PacketHeader& header, std::vector<uint8_t>& payload) {
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    if (!recvAll(header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        return false;
    }
    
    if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
        return false;
    }
    
    payload.resize(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(payload.data(), header.payload_size)) {
            return false;
        }
    }
    
    return true;
}

