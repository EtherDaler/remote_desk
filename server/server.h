#pragma once

#include <string>
#include <functional>
#include <atomic>

class RemoteServer {
public:
    RemoteServer(uint16_t port);
    ~RemoteServer();
    
    // Запуск сервера
    bool start();
    
    // Остановка сервера
    void stop();
    
    // Проверка статуса
    bool isRunning() const { return m_running; }

private:
    void acceptConnections();
    void handleClient(int client_socket);
    bool sendAll(int socket, const uint8_t* data, size_t size);
    bool recvAll(int socket, uint8_t* data, size_t size);
    
    uint16_t m_port;
    int m_server_socket;
    std::atomic<bool> m_running;
};

