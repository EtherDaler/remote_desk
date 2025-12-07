#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>

struct ConnectedAgent {
    int socket;
    std::string id;
    std::string name;
    std::string os;
    bool online;
    std::mutex socket_mutex;
};

struct ConnectedAdmin {
    int socket;
    std::string selected_agent_id;
    std::mutex socket_mutex;
};

class RelayServer {
public:
    RelayServer(uint16_t port, const std::string& admin_token);
    ~RelayServer();
    
    bool start();
    void stop();
    bool isRunning() const { return m_running; }

private:
    void acceptConnections();
    void handleConnection(int client_socket);
    void handleAgent(int client_socket, const std::string& agent_id);
    void handleAdmin(int client_socket);
    
    // Утилиты
    bool sendAll(int socket, const uint8_t* data, size_t size);
    bool recvAll(int socket, uint8_t* data, size_t size);
    bool sendPacket(int socket, uint8_t msg_type, const std::string& payload);
    
    // Получение списка агентов
    std::string getAgentsList();
    
    // Пересылка команды агенту
    bool forwardCommandToAgent(const std::string& agent_id, const std::string& command, std::string& response);

    uint16_t m_port;
    std::string m_admin_token;
    int m_server_socket;
    std::atomic<bool> m_running;
    
    std::map<std::string, std::shared_ptr<ConnectedAgent>> m_agents;
    std::mutex m_agents_mutex;
    
    std::map<int, std::shared_ptr<ConnectedAdmin>> m_admins;
    std::mutex m_admins_mutex;
};

