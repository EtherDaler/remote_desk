#include "client.h"

#include <iostream>
#include <string>
#include <csignal>

RemoteClient* g_client = nullptr;

void signalHandler(int signal) {
    std::cout << "\nDisconnecting..." << std::endl;
    if (g_client) {
        g_client->disconnect();
    }
    exit(0);
}

void printHelp() {
    std::cout << "Remote Terminal Client\n"
              << "Commands:\n"
              << "  help     - Show this help\n"
              << "  exit     - Disconnect and exit\n"
              << "  <cmd>    - Execute command on remote server\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 9999;
    
    // Парсим аргументы
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    
    // Обработка сигналов
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    RemoteClient client;
    g_client = &client;
    
    // Подключаемся
    if (!client.connect(host, port)) {
        return 1;
    }
    
    printHelp();
    
    // Главный цикл
    std::string input;
    while (true) {
        std::cout << "remote> ";
        std::cout.flush();
        
        if (!std::getline(std::cin, input)) {
            break;
        }
        
        // Пропускаем пустые строки
        if (input.empty()) {
            continue;
        }
        
        // Локальные команды
        if (input == "exit" || input == "quit") {
            break;
        }
        
        if (input == "help") {
            printHelp();
            continue;
        }
        
        // Выполняем команду на сервере
        std::string result = client.executeCommand(input);
        
        // Разбираем ответ (первая строка - код возврата)
        size_t newline_pos = result.find('\n');
        if (newline_pos != std::string::npos) {
            std::string exit_code = result.substr(0, newline_pos);
            std::string output = result.substr(newline_pos + 1);
            
            std::cout << output;
            
            if (exit_code != "0") {
                std::cout << "[Exit code: " << exit_code << "]" << std::endl;
            }
        } else {
            std::cout << result << std::endl;
        }
    }
    
    client.disconnect();
    std::cout << "Goodbye!" << std::endl;
    
    return 0;
}

