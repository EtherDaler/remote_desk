#include "server.h"

#include <iostream>
#include <csignal>
#include <memory>

std::unique_ptr<RemoteServer> g_server;

void signalHandler(int signal) {
    std::cout << "\nShutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    uint16_t port = 9999; // Порт по умолчанию
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    // Обработка сигналов для корректного завершения
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    g_server = std::make_unique<RemoteServer>(port);
    
    if (!g_server->start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}

