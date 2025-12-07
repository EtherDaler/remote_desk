#include "relay_server.h"

#include <iostream>
#include <csignal>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>

std::unique_ptr<RelayServer> g_server;

void signalHandler(int signal) {
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

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -p, --port <port>    Port to listen on (default: 9999)\n"
              << "  -t, --token <token>  Admin authentication token (auto-generated if not provided)\n"
              << "  -h, --help           Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = 9999;
    std::string token;
    
    // Парсим аргументы
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "-t" || arg == "--token") {
            if (i + 1 < argc) {
                token = argv[++i];
            }
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
    
    std::cout << "========================================\n"
              << "       Desktop Remote Relay Server      \n"
              << "========================================\n" << std::endl;
    
    g_server = std::make_unique<RelayServer>(port, token);
    
    if (!g_server->start()) {
        std::cerr << "[RELAY] Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}

