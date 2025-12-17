#include "admin_client.h"

#include <iostream>
#include <string>
#include <csignal>
#include <iomanip>

AdminClient* g_client = nullptr;

void signalHandler(int) {
    std::cout << "\nDisconnecting..." << std::endl;
    if (g_client) {
        g_client->disconnect();
    }
    exit(0);
}

void printHelp() {
    std::cout << "\nCommands:\n"
              << "  list              - List all connected agents\n"
              << "  select <id>       - Select agent to control\n"
              << "  lock              - Lock keyboard and mouse on agent\n"
              << "  unlock            - Unlock keyboard and mouse on agent\n"
              << "  screenshot        - Take screenshot and send to Telegram\n"
              << "  <command>         - Execute shell command on selected agent\n"
              << "  help              - Show this help\n"
              << "  exit              - Disconnect and exit\n"
              << std::endl;
}

void printAgents(const std::vector<RemoteProto::AgentInfo>& agents) {
    if (agents.empty()) {
        std::cout << "\nNo agents connected.\n" << std::endl;
        return;
    }
    
    std::cout << "\n";
    std::cout << std::left 
              << std::setw(12) << "ID" 
              << std::setw(20) << "Name" 
              << std::setw(20) << "OS" 
              << std::setw(10) << "Status" 
              << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    for (const auto& agent : agents) {
        std::cout << std::left 
                  << std::setw(12) << agent.id 
                  << std::setw(20) << agent.name 
                  << std::setw(20) << agent.os 
                  << std::setw(10) << (agent.online ? "Online" : "Offline")
                  << std::endl;
    }
    std::cout << std::endl;
}

// Порт (обязателен, задаётся при сборке через -DDEFAULT_PORT=...)
#ifndef DEFAULT_PORT
#error "DEFAULT_PORT must be provided via -DDEFAULT_PORT=..."
#endif

void printUsage(const char* program) {
    std::cout << "Desktop Remote Admin Console\n"
              << "============================\n\n"
              << "Usage: " << program << " <relay_host> <token>\n\n"
              << "Использует порт " << DEFAULT_PORT << " по умолчанию\n\n"
              << "Options:\n"
              << "  -h, --help           Показать справку\n"
              << "\nПример:\n"
              << "  " << program << " 213.108.4.126 mySecretToken123\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string relay_host;
    std::string token;
    uint16_t port = DEFAULT_PORT;
    
    // Парсим аргументы
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (relay_host.empty() && arg[0] != '-') {
            relay_host = arg;
        } else if (token.empty() && arg[0] != '-') {
            token = arg;
        }
    }
    
    if (relay_host.empty() || token.empty()) {
        printUsage(argv[0]);
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    AdminClient client;
    g_client = &client;
    
    std::cout << "Connecting to " << relay_host << ":" << port << "..." << std::endl;
    
    if (!client.connect(relay_host, port, token)) {
        return 1;
    }
    
    std::cout << "Connected!\n" << std::endl;
    std::cout << "========================================\n"
              << "     Desktop Remote Admin Console       \n"
              << "========================================" << std::endl;
    
    printHelp();
    
    std::string input;
    while (true) {
        // Показываем выбранного агента в промпте
        if (client.getSelectedAgent().empty()) {
            std::cout << "admin> ";
        } else {
            // Показываем статус блокировки: 🔒 если заблокировано
            std::string lock_indicator = client.isInputLocked() ? " \033[1;31m[LOCKED]\033[0m" : "";
            std::cout << "admin@" << client.getSelectedAgent() << lock_indicator << "> ";
        }
        std::cout.flush();
        
        if (!std::getline(std::cin, input)) {
            break;
        }
        
        if (input.empty()) continue;
        
        if (input == "exit" || input == "quit") {
            break;
        }
        
        if (input == "help") {
            printHelp();
            continue;
        }
        
        if (input == "list") {
            auto agents = client.listAgents();
            printAgents(agents);
            continue;
        }
        
        if (input.substr(0, 7) == "select ") {
            std::string agent_id = input.substr(7);
            if (client.selectAgent(agent_id)) {
                std::cout << "Selected agent: " << agent_id << std::endl;
            }
            continue;
        }
        
        // Если агент не выбран, предупреждаем
        if (client.getSelectedAgent().empty()) {
            std::cout << "No agent selected. Use 'list' and 'select <id>' first." << std::endl;
            continue;
        }
        
        if (input == "lock") {
            if (client.lockInput()) {
                std::cout << "\033[1;32m✓ Input locked on agent\033[0m" << std::endl;
            }
            continue;
        }
        
        if (input == "unlock") {
            if (client.unlockInput()) {
                std::cout << "\033[1;32m✓ Input unlocked on agent\033[0m" << std::endl;
            }
            continue;
        }
        
        if (input == "screenshot") {
            std::cout << "📸 Taking screenshot..." << std::endl;
            if (client.takeScreenshot()) {
                std::cout << "\033[1;32m✓ Screenshot sent to Telegram!\033[0m" << std::endl;
            }
            continue;
        }
        
        // Выполняем команду
        std::string result = client.executeCommand(input);
        
        // Парсим результат
        size_t newline_pos = result.find('\n');
        if (newline_pos != std::string::npos) {
            std::string exit_code = result.substr(0, newline_pos);
            std::string output = result.substr(newline_pos + 1);
            
            std::cout << output;
            
            if (exit_code != "0" && !exit_code.empty()) {
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

