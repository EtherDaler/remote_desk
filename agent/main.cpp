#include "agent.h"

#include <iostream>
#include <string>
#include <random>
#include <fstream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>
    #include <csignal>
#else
    #include <csignal>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <cstdlib>
#endif

// ==================== Настройки по умолчанию ====================
constexpr const char* DEFAULT_RELAY_HOST = "213.108.4.126";
constexpr uint16_t DEFAULT_PORT = 9999;

std::unique_ptr<RemoteAgent> g_agent;

void signalHandler(int) {
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
#ifdef _WIN32
    char hostname[256];
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        return hostname;
    }
    return "unknown-windows";
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return hostname;
    }
    return "unknown";
#endif
}

// Путь к файлу конфигурации
std::string getConfigPath() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "\\desktop_remote_agent.id";
    }
    return "desktop_remote_agent.id";
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.desktop_remote_agent";
    }
    return ".desktop_remote_agent";
#endif
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

// Проверка прав администратора
bool isRunningAsAdmin() {
#ifdef _WIN32
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
#else
    return geteuid() == 0;
#endif
}

// Запрос прав администратора
bool requestAdminPrivileges(int argc, char* argv[]) {
#ifdef _WIN32
    if (!isRunningAsAdmin()) {
        // Получаем путь к текущему exe
        char szPath[MAX_PATH];
        GetModuleFileNameA(NULL, szPath, MAX_PATH);
        
        // Формируем аргументы
        std::string args;
        for (int i = 1; i < argc; ++i) {
            if (!args.empty()) args += " ";
            args += argv[i];
        }
        
        // Запускаем от имени администратора
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.lpVerb = "runas";
        sei.lpFile = szPath;
        sei.lpParameters = args.c_str();
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;
        
        if (ShellExecuteExA(&sei)) {
            exit(0); // Закрываем текущий процесс
        }
        return false;
    }
    return true;
#else
    if (!isRunningAsAdmin()) {
        std::cerr << "[AGENT] Требуются права root. Запустите с sudo." << std::endl;
        
        // Пытаемся перезапустить с sudo
        std::string cmd = "sudo ";
        for (int i = 0; i < argc; ++i) {
            cmd += argv[i];
            cmd += " ";
        }
        
        std::cout << "[AGENT] Запрашиваем права root..." << std::endl;
        int result = system(cmd.c_str());
        if (result == 0) {
            exit(0);
        }
        return false;
    }
    return true;
#endif
}

// Запуск в режиме демона (фоновый процесс)
bool daemonize() {
#ifdef _WIN32
    // Windows: скрываем консольное окно
    HWND hWnd = GetConsoleWindow();
    if (hWnd) {
        ShowWindow(hWnd, SW_HIDE);
    }
    // Отсоединяемся от консоли
    FreeConsole();
    return true;
#else
    // Unix: классический double-fork для демонизации
    pid_t pid = fork();
    
    if (pid < 0) {
        return false;
    }
    
    if (pid > 0) {
        // Родительский процесс - выходим
        std::cout << "[AGENT] Started in background (PID: " << pid << ")" << std::endl;
        exit(0);
    }
    
    // Дочерний процесс
    if (setsid() < 0) {
        return false;
    }
    
    // Второй fork
    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        exit(0);
    }
    
    umask(0);
    chdir("/");
    
    // Перенаправляем потоки в /dev/null
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

void printUsage(const char* program) {
    std::cout << "Desktop Remote Agent\n"
              << "====================\n\n"
              << "Usage: " << program << " [options]\n\n"
              << "По умолчанию подключается к серверу " << DEFAULT_RELAY_HOST << ":" << DEFAULT_PORT << "\n\n"
              << "Options:\n"
              << "  -d, --daemon         Запуск в фоновом режиме\n"
              << "  -h, --help           Показать справку\n"
              << "\nПримеры:\n"
              << "  " << program << "           # Обычный запуск\n"
              << "  " << program << " -d        # Запуск в фоне\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    bool daemon_mode = false;
    
    // Парсим аргументы
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-d" || arg == "--daemon") {
            daemon_mode = true;
        }
    }
    
    // Запрашиваем права администратора
    if (!requestAdminPrivileges(argc, argv)) {
        std::cerr << "[AGENT] Не удалось получить права администратора" << std::endl;
        return 1;
    }
    
    // Используем захардкоженные настройки
    std::string relay_host = DEFAULT_RELAY_HOST;
    uint16_t port = DEFAULT_PORT;
    std::string name = getHostname();
    std::string id = loadOrGenerateId();
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "========================================\n"
              << "       Desktop Remote Agent             \n"
              << "========================================\n"
              << "Server: " << relay_host << ":" << port << "\n"
              << "Name:   " << name << "\n"
              << "ID:     " << id << "\n"
              << "Mode:   " << (daemon_mode ? "Background" : "Foreground") << "\n"
              << "Admin:  " << (isRunningAsAdmin() ? "Yes" : "No") << "\n"
              << "========================================\n" << std::endl;
    
    // Запуск в фоновом режиме
    if (daemon_mode) {
        if (!daemonize()) {
            std::cerr << "[AGENT] Failed to daemonize" << std::endl;
            return 1;
        }
    }
    
    g_agent = std::make_unique<RemoteAgent>(relay_host, port, id, name);
    g_agent->run();
    
    return 0;
}
