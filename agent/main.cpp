#include "agent.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <random>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

std::unique_ptr<RemoteAgent> g_agent;

void signalHandler(int signal) {
    std::cout << "\n[AGENT] Shutting down..." << std::endl;
    if (g_agent) {
        g_agent->stop();
    }
    exit(0);
}

// Генерация уникального ID
std::string generateId() {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    
    std::string id;
    for (int i = 0; i < 8; ++i) {
        id += charset[dist(gen)];
    }
    return id;
}

// Получение имени хоста
std::string getHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return hostname;
    }
    return "unknown";
}

// Путь к файлу конфигурации
std::string getConfigPath() {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.desktop_remote_agent";
    }
    return ".desktop_remote_agent";
}

// Загрузка или генерация ID агента
std::string loadOrGenerateId() {
    std::string config_path = getConfigPath();
    std::ifstream file(config_path);
    
    if (file.good()) {
        std::string id;
        std::getline(file, id);
        if (!id.empty()) {
            return id;
        }
    }
    
    // Генерируем новый ID
    std::string new_id = generateId();
    
    // Сохраняем
    std::ofstream out(config_path);
    if (out.good()) {
        out << new_id;
    }
    
    return new_id;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <relay_host> [options]\n"
              << "Options:\n"
              << "  -p, --port <port>    Relay server port (default: 9999)\n"
              << "  -n, --name <name>    Agent name (default: hostname)\n"
              << "  -i, --id <id>        Agent ID (auto-generated if not provided)\n"
              << "  -h, --help           Show this help\n"
              << "\nExample:\n"
              << "  " << program << " my-vps.example.com -p 9999 -n \"Home PC\"\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string relay_host;
    uint16_t port = 9999;
    std::string name = getHostname();
    std::string id;
    
    // Парсим аргументы
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-n" || arg == "--name") {
            if (i + 1 < argc) {
                name = argv[++i];
            }
        } else if (arg == "-i" || arg == "--id") {
            if (i + 1 < argc) {
                id = argv[++i];
            }
        } else if (relay_host.empty() && arg[0] != '-') {
            relay_host = arg;
        }
    }
    
    if (relay_host.empty()) {
        std::cerr << "[AGENT] Error: Relay host is required" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    // Загружаем или генерируем ID
    if (id.empty()) {
        id = loadOrGenerateId();
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "========================================\n"
              << "       Desktop Remote Agent             \n"
              << "========================================\n"
              << "Relay:  " << relay_host << ":" << port << "\n"
              << "Name:   " << name << "\n"
              << "ID:     " << id << "\n"
              << "========================================\n" << std::endl;
    
    g_agent = std::make_unique<RemoteAgent>(relay_host, port, id, name);
    g_agent->run();
    
    return 0;
}

