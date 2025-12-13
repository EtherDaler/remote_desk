#include "relay_server.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <fcntl.h>
#endif

// ==================== Настройки по умолчанию ====================
constexpr uint16_t DEFAULT_PORT = 9999;

std::unique_ptr<RelayServer> g_server;

void signalHandler(int) {
    std::cout << "\n[RELAY] Shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

// Генерация случайного токена
std::string generateToken(size_t length = 32) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    
    std::string token;
    token.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        token += charset[dist(gen)];
    }
    return token;
}

// Убить процесс, занимающий порт
void killProcessOnPort(uint16_t port) {
#ifdef _WIN32
    // Windows: netstat + taskkill
    std::ostringstream cmd;
    cmd << "for /f \"tokens=5\" %a in ('netstat -ano ^| findstr :" << port 
        << " ^| findstr LISTENING') do taskkill /F /PID %a 2>nul";
    system(cmd.str().c_str());
#else
    // Unix: lsof + kill или fuser
    std::ostringstream cmd;
    
    // Попробуем fuser (более надёжный способ)
    cmd << "fuser -k " << port << "/tcp 2>/dev/null";
    int result = system(cmd.str().c_str());
    
    if (result != 0) {
        // Fallback на lsof
        cmd.str("");
        cmd << "lsof -ti :" << port << " | xargs kill -9 2>/dev/null";
        system(cmd.str().c_str());
    }
    
    // Даём время на освобождение порта
    usleep(500000); // 500ms
#endif
    std::cout << "[RELAY] Cleared port " << port << std::endl;
}

// Запуск в режиме демона
bool daemonize() {
#ifdef _WIN32
    HWND hWnd = GetConsoleWindow();
    if (hWnd) {
        ShowWindow(hWnd, SW_HIDE);
    }
    FreeConsole();
    return true;
#else
    pid_t pid = fork();
    
    if (pid < 0) {
        return false;
    }
    
    if (pid > 0) {
        std::cout << "[RELAY] Started in background (PID: " << pid << ")" << std::endl;
        exit(0);
    }
    
    if (setsid() < 0) {
        return false;
    }
    
    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        exit(0);
    }
    
    umask(0);
    chdir("/");
    
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) {
            close(null_fd);
        }
    }
    
    return true;
#endif
}

// Сохранить токен в файл
void saveToken(const std::string& token) {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    std::string path = appdata ? std::string(appdata) + "\\relay_token.txt" : "relay_token.txt";
#else
    const char* home = getenv("HOME");
    std::string path = home ? std::string(home) + "/.relay_token" : ".relay_token";
#endif
    
    std::ofstream file(path);
    if (file.good()) {
        file << token;
        std::cout << "[RELAY] Token saved to: " << path << std::endl;
    }
}

void printUsage(const char* program) {
    std::cout << "Desktop Remote Relay Server\n"
              << "===========================\n\n"
              << "Usage: " << program << " [options]\n\n"
              << "По умолчанию использует порт " << DEFAULT_PORT << "\n\n"
              << "Options:\n"
              << "  -t, --token <token>  Токен для авторизации (авто-генерируется)\n"
              << "  -d, --daemon         Запуск в фоновом режиме\n"
              << "  -h, --help           Показать справку\n"
              << "\nПримеры:\n"
              << "  " << program << "                    # Обычный запуск\n"
              << "  " << program << " -d                 # Запуск в фоне\n"
              << "  " << program << " -t mySecretToken   # С заданным токеном\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = DEFAULT_PORT;
    std::string token;
    bool daemon_mode = false;
    
    // Парсим аргументы
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-t" || arg == "--token") {
            if (i + 1 < argc) {
                token = argv[++i];
            }
        } else if (arg == "-d" || arg == "--daemon") {
            daemon_mode = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    // Генерируем токен если не указан
    if (token.empty()) {
        token = generateToken();
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Убиваем процесс на порту если занят
    std::cout << "[RELAY] Checking port " << port << "..." << std::endl;
    killProcessOnPort(port);
    
    std::cout << "========================================\n"
              << "       Desktop Remote Relay Server      \n"
              << "========================================\n"
              << "Port:   " << port << "\n"
              << "Token:  " << token << "\n"
              << "Mode:   " << (daemon_mode ? "Background" : "Foreground") << "\n"
              << "========================================\n" << std::endl;
    
    // Сохраняем токен
    saveToken(token);
    
    // Запуск в фоновом режиме
    if (daemon_mode) {
        if (!daemonize()) {
            std::cerr << "[RELAY] Failed to daemonize" << std::endl;
            return 1;
        }
    }
    
    g_server = std::make_unique<RelayServer>(port, token);
    
    if (!g_server->start()) {
        std::cerr << "[RELAY] Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}
