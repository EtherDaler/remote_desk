#pragma once

#include <string>

class ShellExecutor {
public:
    struct Result {
        std::string output;
        int exit_code;
    };
    
    // Выполняет команду и возвращает результат
    static Result execute(const std::string& command);
};

