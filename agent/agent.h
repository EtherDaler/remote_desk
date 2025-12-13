#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <memory>

#ifdef _WIN32
    #include <cstdint>
#else
    #include <cstdint>
#endif

class RemoteAgent {
public:
    RemoteAgent(const std::string& relay_host, uint16_t relay_port,
                const std::string& agent_id, const std::string& agent_name);
    ~RemoteAgent();
    
    // Подключение к relay серверу
    bool connect();
    
    // Главный цикл
    void run();
    
    // Остановка
    void stop();
    
    bool isRunning() const { return m_running; }

private:
    void handleCommands();
    std::string executeCommand(const std::string& command);
    
    // Блокировка ввода (клавиатура + мышь)
    bool lockInput();
    bool unlockInput();
    
    // Скриншот
    std::vector<uint8_t> takeScreenshot();
    
    bool sendAll(const uint8_t* data, size_t size);
    bool recvAll(uint8_t* data, size_t size);
    bool sendPacket(uint8_t msg_type, const std::string& payload);
    
    std::string getOsInfo();
    std::string m_cwd; // текущая рабочая директория для команд
    
    std::string m_relay_host;
    uint16_t m_relay_port;
    std::string m_agent_id;
    std::string m_agent_name;
    
    int m_socket;
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;
    bool m_input_locked;
};
