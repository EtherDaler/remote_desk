#include "relay_server.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sstream>
#include <fstream>
#include <ctime>
#include <cstdio>

RelayServer::RelayServer(uint16_t port, const std::string& admin_token)
    : m_port(port)
    , m_admin_token(admin_token)
    , m_server_socket(-1)
    , m_running(false)
{}

RelayServer::~RelayServer() {
    stop();
}

bool RelayServer::start() {
    m_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_socket < 0) {
        std::cerr << "[RELAY] Error: Cannot create socket" << std::endl;
        return false;
    }
    
    int opt = 1;
    setsockopt(m_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);
    
    if (bind(m_server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[RELAY] Error: Cannot bind to port " << m_port << std::endl;
        close(m_server_socket);
        return false;
    }
    
    if (listen(m_server_socket, 10) < 0) {
        std::cerr << "[RELAY] Error: Cannot listen on socket" << std::endl;
        close(m_server_socket);
        return false;
    }
    
    m_running = true;
    std::cout << "[RELAY] Server started on port " << m_port << std::endl;
    std::cout << "[RELAY] Admin token: " << m_admin_token << std::endl;
    
    acceptConnections();
    return true;
}

void RelayServer::stop() {
    m_running = false;
    if (m_server_socket >= 0) {
        close(m_server_socket);
        m_server_socket = -1;
    }
}

void RelayServer::acceptConnections() {
    while (m_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(m_server_socket, (sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (m_running) {
                std::cerr << "[RELAY] Error: Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "[RELAY] New connection from: " << client_ip << std::endl;
        
        // Устанавливаем таймаут на сокет (120 секунд для скриншотов)
        struct timeval tv;
        tv.tv_sec = 120;
        tv.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        // Включаем TCP keepalive
        int keepalive = 1;
        setsockopt(client_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        
        std::thread(&RelayServer::handleConnection, this, client_socket, std::string(client_ip)).detach();
    }
}

void RelayServer::handleConnection(int client_socket, const std::string& client_ip) {
    // Ждём первый пакет для определения типа клиента
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    
    if (!recvAll(client_socket, header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        close(client_socket);
        return;
    }
    
    RemoteProto::PacketHeader header;
    if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
        close(client_socket);
        return;
    }
    
    std::vector<uint8_t> payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(client_socket, payload.data(), header.payload_size)) {
            close(client_socket);
            return;
        }
    }
    
    std::string payload_str(payload.begin(), payload.end());
    
    if (header.type == RemoteProto::MessageType::AGENT_REGISTER) {
        // Агент регистрируется: payload = "id|name|os"
        auto info = RemoteProto::AgentInfo::deserialize(payload_str + "|1");
        
        std::cout << "[RELAY] Agent registered: " << info.name << " (" << info.id << ") from " << client_ip << std::endl;
        
        // Отправляем подтверждение
        sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENT_REGISTERED), "OK");
        
        // Добавляем в список
        {
            std::lock_guard<std::mutex> lock(m_agents_mutex);
            auto agent = std::make_shared<ConnectedAgent>();
            agent->socket = client_socket;
            agent->id = info.id;
            agent->name = info.name;
            agent->os = info.os;
            agent->ip = client_ip;
            agent->online = true;
            m_agents[info.id] = agent;
        }
        
        // Отправляем уведомление в Telegram
        notifyAgentConnected(info.name, info.os, client_ip);
        
        handleAgent(client_socket, info.id);
        
    } else if (header.type == RemoteProto::MessageType::ADMIN_AUTH) {
        // Админ авторизуется: payload = token
        if (payload_str == m_admin_token) {
            std::cout << "[RELAY] Admin authenticated" << std::endl;
            sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::ADMIN_AUTHED), "OK");
            
            {
                std::lock_guard<std::mutex> lock(m_admins_mutex);
                auto admin = std::make_shared<ConnectedAdmin>();
                admin->socket = client_socket;
                m_admins[client_socket] = admin;
            }
            
            handleAdmin(client_socket);
        } else {
            std::cout << "[RELAY] Admin auth failed" << std::endl;
            sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "Invalid token");
            close(client_socket);
        }
    } else {
        std::cerr << "[RELAY] Unknown client type" << std::endl;
        close(client_socket);
    }
}

void RelayServer::handleAgent(int client_socket, const std::string& agent_id) {
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    std::string agent_name;
    
    // Получаем имя агента для уведомления
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        auto it = m_agents.find(agent_id);
        if (it != m_agents.end()) {
            agent_name = it->second->name;
        }
    }
    
    while (m_running) {
        // Агент просто держит соединение и отвечает на команды
        // Команды приходят от relay, когда админ их отправляет
        if (!recvAll(client_socket, header_buffer.data(), RemoteProto::HEADER_SIZE)) {
            break;
        }
        
        RemoteProto::PacketHeader header;
        if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
            break;
        }
        
        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!recvAll(client_socket, payload.data(), header.payload_size)) {
                break;
            }
        }
        
        if (header.type == RemoteProto::MessageType::HEARTBEAT) {
            sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::HEARTBEAT), "pong");
        } else if (header.type == RemoteProto::MessageType::DISCONNECT) {
            break;
        }
        // RESPONSE обрабатывается в forwardCommandToAgent
    }
    
    // Удаляем агента
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        m_agents.erase(agent_id);
    }
    
    // Уведомление об отключении
    notifyAgentDisconnected(agent_name);
    
    std::cout << "[RELAY] Agent disconnected: " << agent_id << std::endl;
    close(client_socket);
}

void RelayServer::handleAdmin(int client_socket) {
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    std::shared_ptr<ConnectedAdmin> admin;
    
    {
        std::lock_guard<std::mutex> lock(m_admins_mutex);
        admin = m_admins[client_socket];
    }
    
    while (m_running) {
        if (!recvAll(client_socket, header_buffer.data(), RemoteProto::HEADER_SIZE)) {
            break;
        }
        
        RemoteProto::PacketHeader header;
        if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
            break;
        }
        
        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!recvAll(client_socket, payload.data(), header.payload_size)) {
                break;
            }
        }
        
        std::string payload_str(payload.begin(), payload.end());
        
        switch (header.type) {
            case RemoteProto::MessageType::LIST_AGENTS: {
                std::string list = getAgentsList();
                sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENTS_LIST), list);
                break;
            }
            
            case RemoteProto::MessageType::SELECT_AGENT: {
                std::lock_guard<std::mutex> lock(m_agents_mutex);
                if (m_agents.find(payload_str) != m_agents.end()) {
                    admin->selected_agent_id = payload_str;
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENT_SELECTED), payload_str);
                    std::cout << "[RELAY] Admin selected agent: " << payload_str << std::endl;
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENT_OFFLINE), payload_str);
                }
                break;
            }
            
            case RemoteProto::MessageType::COMMAND: {
                if (admin->selected_agent_id.empty()) {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "No agent selected");
                    break;
                }
                
                std::string response;
                if (forwardCommandToAgent(admin->selected_agent_id, payload_str, response)) {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::RESPONSE), response);
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENT_OFFLINE), admin->selected_agent_id);
                    admin->selected_agent_id.clear();
                }
                break;
            }
            
            case RemoteProto::MessageType::INPUT_LOCK: {
                if (admin->selected_agent_id.empty()) {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "No agent selected");
                    break;
                }
                
                RemoteProto::MessageType response_type;
                std::string response;
                if (forwardInputCommand(admin->selected_agent_id, RemoteProto::MessageType::INPUT_LOCK, response_type, response)) {
                    sendPacket(client_socket, static_cast<uint8_t>(response_type), response);
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENT_OFFLINE), admin->selected_agent_id);
                    admin->selected_agent_id.clear();
                }
                break;
            }
            
            case RemoteProto::MessageType::INPUT_UNLOCK: {
                if (admin->selected_agent_id.empty()) {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "No agent selected");
                    break;
                }
                
                RemoteProto::MessageType response_type;
                std::string response;
                if (forwardInputCommand(admin->selected_agent_id, RemoteProto::MessageType::INPUT_UNLOCK, response_type, response)) {
                    sendPacket(client_socket, static_cast<uint8_t>(response_type), response);
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::AGENT_OFFLINE), admin->selected_agent_id);
                    admin->selected_agent_id.clear();
                }
                break;
            }
            
            case RemoteProto::MessageType::SCREENSHOT: {
                if (admin->selected_agent_id.empty()) {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "No agent selected");
                    break;
                }
                
                std::cout << "[RELAY] Screenshot requested for agent: " << admin->selected_agent_id << std::endl;
                
                // Получаем имя агента для подписи
                std::string agent_name;
                {
                    std::lock_guard<std::mutex> lock(m_agents_mutex);
                    auto it = m_agents.find(admin->selected_agent_id);
                    if (it != m_agents.end()) {
                        agent_name = it->second->name;
                    }
                }
                
                std::vector<uint8_t> screenshot_data;
                if (forwardScreenshotRequest(admin->selected_agent_id, screenshot_data) && !screenshot_data.empty()) {
                    // Отправляем подтверждение клиенту
                    auto packet = RemoteProto::createPacket(RemoteProto::MessageType::SCREENSHOT_DATA, screenshot_data);
                    sendAll(client_socket, packet.data(), packet.size());
                    
                    // Отправляем скриншот в Telegram
                    std::string caption = "📸 Скриншот с устройства: " + agent_name;
                    sendTelegramPhoto(screenshot_data, caption);
                    
                    std::cout << "[RELAY] Screenshot sent to Telegram (" << screenshot_data.size() << " bytes)" << std::endl;
                } else {
                    sendPacket(client_socket, static_cast<uint8_t>(RemoteProto::MessageType::SCREENSHOT_ERROR), "Failed to get screenshot");
                }
                break;
            }
            
            case RemoteProto::MessageType::DISCONNECT:
                goto cleanup;
                
            default:
                break;
        }
    }
    
cleanup:
    {
        std::lock_guard<std::mutex> lock(m_admins_mutex);
        m_admins.erase(client_socket);
    }
    std::cout << "[RELAY] Admin disconnected" << std::endl;
    close(client_socket);
}

std::string RelayServer::getAgentsList() {
    std::lock_guard<std::mutex> lock(m_agents_mutex);
    std::string result;
    
    for (const auto& [id, agent] : m_agents) {
        RemoteProto::AgentInfo info;
        info.id = agent->id;
        info.name = agent->name;
        info.os = agent->os;
        info.online = agent->online;
        
        if (!result.empty()) {
            result += "\n";
        }
        result += info.serialize();
    }
    
    return result;
}

bool RelayServer::forwardCommandToAgent(const std::string& agent_id, const std::string& command, std::string& response) {
    std::shared_ptr<ConnectedAgent> agent;
    
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        auto it = m_agents.find(agent_id);
        if (it == m_agents.end()) {
            return false;
        }
        agent = it->second;
    }
    
    std::lock_guard<std::mutex> lock(agent->socket_mutex);
    
    // Отправляем команду агенту
    if (!sendPacket(agent->socket, static_cast<uint8_t>(RemoteProto::MessageType::COMMAND), command)) {
        return false;
    }
    
    // Ждём ответ
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    if (!recvAll(agent->socket, header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        return false;
    }
    
    RemoteProto::PacketHeader header;
    if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
        return false;
    }
    
    std::vector<uint8_t> payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(agent->socket, payload.data(), header.payload_size)) {
            return false;
        }
    }
    
    response = std::string(payload.begin(), payload.end());
    return true;
}

bool RelayServer::sendAll(int socket, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(socket, data + sent, size - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RelayServer::recvAll(int socket, uint8_t* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(socket, data + received, size - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool RelayServer::sendPacket(int socket, uint8_t msg_type, const std::string& payload) {
    auto packet = RemoteProto::createPacket(static_cast<RemoteProto::MessageType>(msg_type), payload);
    return sendAll(socket, packet.data(), packet.size());
}

bool RelayServer::forwardInputCommand(const std::string& agent_id, RemoteProto::MessageType cmd_type,
                                       RemoteProto::MessageType& response_type, std::string& response) {
    std::shared_ptr<ConnectedAgent> agent;
    
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        auto it = m_agents.find(agent_id);
        if (it == m_agents.end()) {
            std::cerr << "[RELAY] forwardInputCommand: agent not found" << std::endl;
            return false;
        }
        agent = it->second;
    }
    
    std::lock_guard<std::mutex> lock(agent->socket_mutex);
    
    std::cout << "[RELAY] Forwarding input command to agent " << agent_id << std::endl;
    
    // Отправляем команду агенту
    if (!sendPacket(agent->socket, static_cast<uint8_t>(cmd_type), "")) {
        std::cerr << "[RELAY] Failed to send input command to agent" << std::endl;
        return false;
    }
    
    std::cout << "[RELAY] Waiting for response from agent..." << std::endl;
    
    // Ждём ответ
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    if (!recvAll(agent->socket, header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        std::cerr << "[RELAY] Failed to receive header from agent" << std::endl;
        return false;
    }
    
    RemoteProto::PacketHeader header;
    if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
        std::cerr << "[RELAY] Failed to parse header from agent" << std::endl;
        return false;
    }
    
    std::vector<uint8_t> payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!recvAll(agent->socket, payload.data(), header.payload_size)) {
            std::cerr << "[RELAY] Failed to receive payload from agent" << std::endl;
            return false;
        }
    }
    
    response_type = header.type;
    response = std::string(payload.begin(), payload.end());
    std::cout << "[RELAY] Received response from agent: " << response << std::endl;
    return true;
}

// ==================== Telegram уведомления ====================

void RelayServer::sendTelegramNotification(const std::string& message) {
    // Запускаем отправку в отдельном потоке чтобы не блокировать основной поток
    std::thread([message]() {
        // Формируем HTTP запрос к Telegram API
        std::string host = "api.telegram.org";
        
        // URL-encode сообщения (простая версия)
        std::string encoded_message;
        for (char c : message) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded_message += c;
            } else if (c == ' ') {
                encoded_message += "%20";
            } else if (c == '\n') {
                encoded_message += "%0A";
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                encoded_message += hex;
            }
        }
        
        std::string path = "/bot" + std::string(TELEGRAM_BOT_TOKEN) + 
                          "/sendMessage?chat_id=" + std::string(TELEGRAM_CHAT_ID) + 
                          "&text=" + encoded_message +
                          "&parse_mode=HTML";
        
        // Используем curl для отправки (проще и надёжнее чем raw sockets с SSL)
        std::string cmd = "curl -s -X GET 'https://" + host + path + "' > /dev/null 2>&1";
        (void)system(cmd.c_str());
        
    }).detach();
}

void RelayServer::notifyAgentConnected(const std::string& name, const std::string& os, const std::string& ip) {
    std::ostringstream msg;
    msg << "🟢 <b>Агент подключился!</b>\n\n"
        << "📱 <b>Устройство:</b> " << name << "\n"
        << "💻 <b>ОС:</b> " << os << "\n"
        << "🌐 <b>IP:</b> " << ip << "\n\n"
        << "✅ Можно подключаться!";
    
    sendTelegramNotification(msg.str());
    std::cout << "[RELAY] Telegram notification sent: Agent connected" << std::endl;
}

void RelayServer::notifyAgentDisconnected(const std::string& name) {
    std::ostringstream msg;
    msg << "🔴 <b>Агент отключился</b>\n\n"
        << "📱 <b>Устройство:</b> " << name;
    
    sendTelegramNotification(msg.str());
    std::cout << "[RELAY] Telegram notification sent: Agent disconnected" << std::endl;
}

bool RelayServer::forwardScreenshotRequest(const std::string& agent_id, std::vector<uint8_t>& screenshot_data) {
    std::shared_ptr<ConnectedAgent> agent;
    
    {
        std::lock_guard<std::mutex> lock(m_agents_mutex);
        auto it = m_agents.find(agent_id);
        if (it == m_agents.end()) {
            return false;
        }
        agent = it->second;
    }
    
    std::lock_guard<std::mutex> lock(agent->socket_mutex);
    
    std::cout << "[RELAY] Sending screenshot request to agent..." << std::endl;
    
    // Отправляем запрос на скриншот
    if (!sendPacket(agent->socket, static_cast<uint8_t>(RemoteProto::MessageType::SCREENSHOT), "")) {
        std::cerr << "[RELAY] Failed to send screenshot request" << std::endl;
        return false;
    }
    
    std::cout << "[RELAY] Waiting for screenshot response..." << std::endl;
    
    // Ждём ответ (может быть большим)
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    if (!recvAll(agent->socket, header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        std::cerr << "[RELAY] Failed to receive screenshot header" << std::endl;
        return false;
    }
    
    RemoteProto::PacketHeader header;
    if (!RemoteProto::parseHeader(header_buffer.data(), header)) {
        std::cerr << "[RELAY] Failed to parse screenshot header" << std::endl;
        return false;
    }
    
    std::cout << "[RELAY] Screenshot header received: type=" << static_cast<int>(header.type) 
              << ", size=" << header.payload_size << std::endl;
    
    if (header.type != RemoteProto::MessageType::SCREENSHOT_DATA) {
        std::cerr << "[RELAY] Screenshot response is not SCREENSHOT_DATA" << std::endl;
        // Пропускаем payload ошибки
        if (header.payload_size > 0) {
            std::vector<uint8_t> error_payload(header.payload_size);
            recvAll(agent->socket, error_payload.data(), header.payload_size);
            std::cerr << "[RELAY] Screenshot error: " << std::string(error_payload.begin(), error_payload.end()) << std::endl;
        }
        return false;
    }
    
    screenshot_data.resize(header.payload_size);
    if (header.payload_size > 0) {
        std::cout << "[RELAY] Receiving screenshot data (" << header.payload_size << " bytes)..." << std::endl;
        if (!recvAll(agent->socket, screenshot_data.data(), header.payload_size)) {
            std::cerr << "[RELAY] Failed to receive screenshot data" << std::endl;
            return false;
        }
        std::cout << "[RELAY] Screenshot data received successfully" << std::endl;
    }
    
    return true;
}

void RelayServer::sendTelegramPhoto(const std::vector<uint8_t>& photo_data, const std::string& caption) {
    // Запускаем отправку в отдельном потоке
    std::thread([photo_data, caption]() {
        // Сохраняем фото во временный файл
        std::string tmp_file = "/tmp/screenshot_telegram_" + std::to_string(time(nullptr)) + ".png";
        
        std::ofstream file(tmp_file, std::ios::binary);
        if (!file.good()) {
            std::cerr << "[RELAY] Failed to create temp file for screenshot" << std::endl;
            return;
        }
        file.write(reinterpret_cast<const char*>(photo_data.data()), photo_data.size());
        file.close();
        
        // Отправляем через curl
        std::ostringstream cmd;
        cmd << "curl -s -X POST 'https://api.telegram.org/bot" << TELEGRAM_BOT_TOKEN << "/sendPhoto' "
            << "-F 'chat_id=" << TELEGRAM_CHAT_ID << "' "
            << "-F 'photo=@" << tmp_file << "' "
            << "-F 'caption=" << caption << "' "
            << "> /dev/null 2>&1";
        
        (void)system(cmd.str().c_str());
        
        // Удаляем временный файл
        std::remove(tmp_file.c_str());
        
    }).detach();
}

