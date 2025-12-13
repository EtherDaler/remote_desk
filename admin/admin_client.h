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
    std::vector<RemoteProto::AgentInfo> listAgents();
    
    // Выбор агента для управления
    bool selectAgent(const std::string& agent_id);
    
    // Выполнение команды на выбранном агенте
    std::string executeCommand(const std::string& command);
    
    // Блокировка/разблокировка ввода на агенте
    bool lockInput();
    bool unlockInput();
    
    // Скриншот
    bool takeScreenshot();
    
    // Текущий выбранный агент
    std::string getSelectedAgent() const { return m_selected_agent; }
    bool isInputLocked() const { return m_input_locked; }
    
    bool isConnected() const { return m_socket >= 0; }

private:
    bool sendAll(const uint8_t* data, size_t size);
    bool recvAll(uint8_t* data, size_t size);
    bool sendPacket(uint8_t msg_type, const std::string& payload);
    bool recvPacket(RemoteProto::PacketHeader& header, std::vector<uint8_t>& payload);
    
    int m_socket;
    std::string m_selected_agent;
    bool m_input_locked;
};

