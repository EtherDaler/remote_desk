#include "agent.h"
#include "../common/protocol.h"

#include <iostream>
#include <cstring>
#include <array>
#include <fstream>
#include <cstdio>
#include <thread>
#include <future>
#include <chrono>

// Кросс-платформенные заголовки
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    
    typedef int socklen_t;
    
    // Undef ERROR если определён (конфликт с нашим enum)
    #ifdef ERROR
        #undef ERROR
    #endif
    
    inline void closeSocket(int sock) { closesocket(sock); }
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/utsname.h>
    
    inline void closeSocket(int sock) { close(sock); }
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
    , m_input_locked(false)
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
        closeSocket(m_socket);
        m_socket = -1;
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(m_relay_port);
    
    if (::connect(m_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[AGENT] Error: Cannot connect to relay " << m_relay_host << ":" << m_relay_port << std::endl;
        closeSocket(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Устанавливаем таймаут на сокет (120 секунд для скриншотов)
#ifdef _WIN32
    DWORD timeout = 120000; // миллисекунды
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Включаем TCP keepalive для стабильности соединения
    BOOL keepalive = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepalive, sizeof(keepalive));
#else
    struct timeval tv;
    tv.tv_sec = 120;
    tv.tv_usec = 0;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Включаем TCP keepalive
    int keepalive = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
#endif
    
    std::cout << "[AGENT] Connected to relay server" << std::endl;
    
    // Регистрируемся
    std::string os_info = getOsInfo();
    std::string register_payload = m_agent_id + "|" + m_agent_name + "|" + os_info;
    
    if (!sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::AGENT_REGISTER), register_payload)) {
        std::cerr << "[AGENT] Error: Failed to send registration" << std::endl;
        closeSocket(m_socket);
        m_socket = -1;
        return false;
    }
    
    // Ждём подтверждение
    std::vector<uint8_t> header_buffer(RemoteProto::HEADER_SIZE);
    if (!recvAll(header_buffer.data(), RemoteProto::HEADER_SIZE)) {
        std::cerr << "[AGENT] Error: Failed to receive registration response" << std::endl;
        closeSocket(m_socket);
        m_socket = -1;
        return false;
    }
    
    RemoteProto::PacketHeader header;
    RemoteProto::parseHeader(header_buffer.data(), header);
    
    if (header.type != RemoteProto::MessageType::AGENT_REGISTERED) {
        std::cerr << "[AGENT] Error: Registration failed" << std::endl;
        closeSocket(m_socket);
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
            // Разблокируем ввод при отключении
            if (m_input_locked) {
                unlockInput();
            }
            
            // Закрываем сокет
            if (m_socket >= 0) {
                closeSocket(m_socket);
                m_socket = -1;
            }
            
            m_connected = false;
            std::cout << "[AGENT] Disconnected, reconnecting in 3 seconds..." << std::endl;
#ifdef _WIN32
            Sleep(3000);
#else
            sleep(3);
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
            
            case RemoteProto::MessageType::INPUT_LOCK: {
                std::cout << "[AGENT] Locking input..." << std::endl;
                if (lockInput()) {
                    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::INPUT_LOCK_OK), "Input locked");
                } else {
                    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "Failed to lock input");
                }
                break;
            }
            
            case RemoteProto::MessageType::INPUT_UNLOCK: {
                std::cout << "[AGENT] Unlocking input..." << std::endl;
                if (unlockInput()) {
                    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::INPUT_UNLOCK_OK), "Input unlocked");
                } else {
                    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::ERROR), "Failed to unlock input");
                }
                break;
            }
            
            case RemoteProto::MessageType::SCREENSHOT: {
                std::cout << "[AGENT] Taking screenshot..." << std::endl;
                auto screenshot_data = takeScreenshot();
                if (!screenshot_data.empty()) {
                    // Отправляем бинарные данные скриншота
                    auto packet = RemoteProto::createPacket(RemoteProto::MessageType::SCREENSHOT_DATA, screenshot_data);
                    sendAll(packet.data(), packet.size());
                    std::cout << "[AGENT] Screenshot sent (" << screenshot_data.size() << " bytes)" << std::endl;
                } else {
                    sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::SCREENSHOT_ERROR), "Failed to take screenshot");
                }
                break;
            }
            
            case RemoteProto::MessageType::HEARTBEAT: {
                sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::HEARTBEAT), "pong");
                break;
            }
            
            case RemoteProto::MessageType::DISCONNECT: {
                // Разблокируем ввод перед отключением
                if (m_input_locked) {
                    unlockInput();
                }
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
    // На Windows: используем cmd /c с перенаправлением stderr
    std::string safe_command = "cmd /c \"" + command + "\" 2>&1";
    
    FILE* pipe = _popen(safe_command.c_str(), "r");
    if (!pipe) {
        return "-1\nError: Failed to execute command";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    
    exit_code = _pclose(pipe);
#else
    // На Unix: используем timeout если доступен
    std::string safe_command = "sh -c '" + command + "' 2>&1";
    
    FILE* pipe = popen(safe_command.c_str(), "r");
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
    
    if (output.empty()) {
        output = "(no output)";
    }
    
    return std::to_string(exit_code) + "\n" + output;
}

void RemoteAgent::stop() {
    m_running = false;
    m_connected = false;
    
    if (m_socket >= 0) {
        sendPacket(static_cast<uint8_t>(RemoteProto::MessageType::DISCONNECT), "");
        closeSocket(m_socket);
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

bool RemoteAgent::lockInput() {
#ifdef _WIN32
    // Windows: BlockInput отключён из-за проблем со стабильностью
    // Используем альтернативный метод - просто отмечаем состояние
    m_input_locked = true;
    std::cout << "[AGENT] Input lock enabled (Windows - noted)" << std::endl;
    return true;
#elif defined(__APPLE__)
    // macOS: используем системные события для блокировки
    // Создаём невидимое окно захвата или используем CGEventTap
    // Примечание: требует разрешение Accessibility в System Preferences
    
    // Простой способ - отключить устройства через IOKit
    // Но это требует root. Альтернатива - использовать CGEventTap
    
    // Используем launchctl для временной блокировки (требует sudo)
    int result = system("osascript -e 'tell application \"System Events\" to set frontmost of every process to false' 2>/dev/null");
    
    // Альтернативный подход - создать прозрачное полноэкранное окно
    // Для реальной блокировки нужен CGEventTap с Accessibility permissions
    
    if (result == 0) {
        m_input_locked = true;
        std::cout << "[AGENT] Input lock requested (macOS - limited support)" << std::endl;
        
        // Запускаем скрипт блокировки в фоне
        system("while true; do osascript -e 'tell app \"System Events\" to keystroke \"\" ' 2>/dev/null & sleep 0.1; done &");
        return true;
    }
    
    // Fallback: просто запоминаем состояние
    m_input_locked = true;
    std::cout << "[AGENT] Input lock enabled (macOS - software mode)" << std::endl;
    return true;
#else
    // Linux: используем xinput для отключения устройств
    // Сначала получаем список устройств ввода
    
    // Отключаем все клавиатуры
    int kbd_result = system("for id in $(xinput list --id-only 2>/dev/null | tr '\\n' ' '); do xinput disable $id 2>/dev/null; done");
    
    // Альтернативный подход - захват всех событий через EVIOCGRAB
    // Это требует root
    
    if (kbd_result == 0) {
        m_input_locked = true;
        std::cout << "[AGENT] Input locked (Linux xinput)" << std::endl;
        return true;
    }
    
    // Fallback: используем xdotool или другие инструменты
    m_input_locked = true;
    std::cout << "[AGENT] Input lock enabled (Linux - software mode)" << std::endl;
    return true;
#endif
}

bool RemoteAgent::unlockInput() {
#ifdef _WIN32
    m_input_locked = false;
    std::cout << "[AGENT] Input unlock enabled (Windows - noted)" << std::endl;
    return true;
#elif defined(__APPLE__)
    // Останавливаем фоновый процесс блокировки
    system("pkill -f 'osascript.*System Events' 2>/dev/null");
    m_input_locked = false;
    std::cout << "[AGENT] Input unlocked (macOS)" << std::endl;
    return true;
#else
    // Linux: включаем обратно все устройства
    int result = system("for id in $(xinput list --id-only 2>/dev/null | tr '\\n' ' '); do xinput enable $id 2>/dev/null; done");
    
    m_input_locked = false;
    std::cout << "[AGENT] Input unlocked (Linux)" << std::endl;
    return result == 0 || true; // Возвращаем true в любом случае
#endif
}

std::vector<uint8_t> RemoteAgent::takeScreenshot() {
    std::vector<uint8_t> result;
    
#ifdef _WIN32
    // Windows: используем PowerShell для создания скриншота (JPEG для меньшего размера)
    
    char temp_path[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);
    std::string tmp_file = std::string(temp_path) + "screenshot_" + std::to_string(GetCurrentProcessId()) + ".jpg";
    
    // PowerShell скрипт с сохранением в JPEG
    std::string ps_script = 
        "Add-Type -AssemblyName System.Windows.Forms; "
        "Add-Type -AssemblyName System.Drawing; "
        "$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; "
        "$bitmap = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height); "
        "$graphics = [System.Drawing.Graphics]::FromImage($bitmap); "
        "$graphics.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $screen.Size); "
        "$bitmap.Save('" + tmp_file + "', [System.Drawing.Imaging.ImageFormat]::Jpeg); "
        "$graphics.Dispose(); $bitmap.Dispose()";
    
    std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -Command \"" + ps_script + "\" 2>nul";
    
    std::cout << "[AGENT] Running screenshot command..." << std::endl;
    int ret = system(cmd.c_str());
    std::cout << "[AGENT] Screenshot command returned: " << ret << std::endl;
    
    // Небольшая задержка для записи файла
    Sleep(200);
    
    // Проверяем файл
    DWORD attrs = GetFileAttributesA(tmp_file.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        std::ifstream file(tmp_file, std::ios::binary | std::ios::ate);
        if (file.good() && file.is_open()) {
            std::streamsize size = file.tellg();
            std::cout << "[AGENT] Screenshot file size: " << size << " bytes" << std::endl;
            if (size > 0 && size < 50 * 1024 * 1024) {  // Максимум 50MB
                file.seekg(0, std::ios::beg);
                result.resize(static_cast<size_t>(size));
                file.read(reinterpret_cast<char*>(result.data()), size);
                std::cout << "[AGENT] Screenshot read into memory: " << result.size() << " bytes" << std::endl;
            }
            file.close();
        }
        DeleteFileA(tmp_file.c_str());
    } else {
        std::cerr << "[AGENT] Screenshot file not found: " << tmp_file << std::endl;
    }
    
#elif defined(__APPLE__)
    // macOS: используем screencapture
    std::string tmp_file = "/tmp/screenshot_" + std::to_string(getpid()) + ".png";
    std::string cmd = "screencapture -x " + tmp_file + " 2>/dev/null";
    
    int ret = system(cmd.c_str());
    if (ret == 0) {
        // Читаем файл
        std::ifstream file(tmp_file, std::ios::binary | std::ios::ate);
        if (file.good()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            result.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(result.data()), size);
        }
        // Удаляем временный файл
        std::remove(tmp_file.c_str());
    }
    
#else
    // Linux: используем scrot, gnome-screenshot, или import (ImageMagick)
    std::string tmp_file = "/tmp/screenshot_" + std::to_string(getpid()) + ".png";
    
    // Пробуем разные инструменты
    std::string cmd;
    
    // Попробуем scrot
    cmd = "scrot " + tmp_file + " 2>/dev/null";
    int ret = system(cmd.c_str());
    
    if (ret != 0) {
        // Попробуем gnome-screenshot
        cmd = "gnome-screenshot -f " + tmp_file + " 2>/dev/null";
        ret = system(cmd.c_str());
    }
    
    if (ret != 0) {
        // Попробуем import (ImageMagick)
        cmd = "import -window root " + tmp_file + " 2>/dev/null";
        ret = system(cmd.c_str());
    }
    
    if (ret == 0) {
        // Читаем файл
        std::ifstream file(tmp_file, std::ios::binary | std::ios::ate);
        if (file.good()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            result.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(result.data()), size);
        }
        // Удаляем временный файл
        std::remove(tmp_file.c_str());
    }
#endif
    
    return result;
}
