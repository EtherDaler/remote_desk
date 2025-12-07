#pragma once

#include <string>
#include <cstdint>

class RemoteClient {
public:
    RemoteClient();
    ~RemoteClient();
    
    // Подключение к серверу
    bool connect(const std::string& host, uint16_t port);
    
    // Отключение
    void disconnect();
    
    // Выполнение команды
    std::string executeCommand(const std::string& command);
    
    // Проверка соединения
    bool isConnected() const { return m_socket >= 0; }

private:
    bool sendAll(const uint8_t* data, size_t size);
    bool recvAll(uint8_t* data, size_t size);
    
    int m_socket;
};

