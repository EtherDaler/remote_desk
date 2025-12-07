#include "shell_executor.h"

#include <array>
#include <memory>
#include <cstdio>
#include <sys/wait.h>

ShellExecutor::Result ShellExecutor::execute(const std::string& command) {
    Result result;
    result.exit_code = 0;
    
    std::array<char, 4096> buffer;
    std::string output;
    
    // Открываем pipe для чтения вывода команды
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "Error: Failed to execute command";
        result.exit_code = -1;
        return result;
    }
    
    // Читаем вывод
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    
    // Получаем код возврата
    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = -1;
    }
    
    result.output = output;
    return result;
}

