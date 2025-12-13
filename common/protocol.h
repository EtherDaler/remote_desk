#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

namespace RemoteProto {

// Типы сообщений
enum class MessageType : uint8_t {
    // Регистрация и аутентификация
    AGENT_REGISTER = 0x01,      // Агент регистрируется на relay
    AGENT_REGISTERED = 0x02,    // Подтверждение регистрации
    ADMIN_AUTH = 0x03,          // Админ авторизуется
    ADMIN_AUTHED = 0x04,        // Подтверждение авторизации
    
    // Управление устройствами
    LIST_AGENTS = 0x10,         // Запрос списка агентов
    AGENTS_LIST = 0x11,         // Список агентов
    SELECT_AGENT = 0x12,        // Выбор агента для управления
    AGENT_SELECTED = 0x13,      // Подтверждение выбора
    AGENT_OFFLINE = 0x14,       // Агент недоступен
    
    // Команды
    COMMAND = 0x20,             // Команда для выполнения
    RESPONSE = 0x21,            // Ответ с результатом
    
    // Блокировка ввода
    INPUT_LOCK = 0x25,          // Заблокировать клавиатуру и мышь
    INPUT_UNLOCK = 0x26,        // Разблокировать клавиатуру и мышь
    INPUT_LOCK_OK = 0x27,       // Подтверждение блокировки
    INPUT_UNLOCK_OK = 0x28,     // Подтверждение разблокировки
    
    // Скриншот
    SCREENSHOT = 0x40,          // Запрос скриншота
    SCREENSHOT_DATA = 0x41,     // Данные скриншота (PNG)
    SCREENSHOT_ERROR = 0x42,    // Ошибка создания скриншота
    
    // Служебные
    HEARTBEAT = 0x30,           // Проверка соединения
    DISCONNECT = 0x31,          // Отключение
    ERROR = 0x3F                // Ошибка
};

// Тип клиента
enum class ClientType : uint8_t {
    AGENT = 0x01,
    ADMIN = 0x02
};

// Заголовок пакета
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
struct PacketHeader {
    MessageType type;
    uint32_t payload_size;
}
#ifdef __GNUC__
__attribute__((packed))
#endif
;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

constexpr size_t HEADER_SIZE = sizeof(PacketHeader);
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB (для скриншотов)
constexpr uint16_t DEFAULT_PORT = 9999;

// Сериализация пакета
inline std::vector<uint8_t> createPacket(MessageType type, const std::string& payload) {
    std::vector<uint8_t> packet(HEADER_SIZE + payload.size());
    
    PacketHeader header;
    header.type = type;
    header.payload_size = static_cast<uint32_t>(payload.size());
    
    memcpy(packet.data(), &header, HEADER_SIZE);
    if (!payload.empty()) {
        memcpy(packet.data() + HEADER_SIZE, payload.data(), payload.size());
    }
    
    return packet;
}

inline std::vector<uint8_t> createPacket(MessageType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> packet(HEADER_SIZE + payload.size());
    
    PacketHeader header;
    header.type = type;
    header.payload_size = static_cast<uint32_t>(payload.size());
    
    memcpy(packet.data(), &header, HEADER_SIZE);
    if (!payload.empty()) {
        memcpy(packet.data() + HEADER_SIZE, payload.data(), payload.size());
    }
    
    return packet;
}

// Парсинг заголовка
inline bool parseHeader(const uint8_t* data, PacketHeader& header) {
    memcpy(&header, data, HEADER_SIZE);
    return header.payload_size <= MAX_PAYLOAD_SIZE;
}

// Информация об агенте (для сериализации)
struct AgentInfo {
    std::string id;           // Уникальный ID
    std::string name;         // Имя устройства
    std::string os;           // ОС
    bool online;              // Статус
    
    std::string serialize() const {
        return id + "|" + name + "|" + os + "|" + (online ? "1" : "0");
    }
    
    static AgentInfo deserialize(const std::string& data) {
        AgentInfo info;
        size_t pos1 = data.find('|');
        size_t pos2 = data.find('|', pos1 + 1);
        size_t pos3 = data.find('|', pos2 + 1);
        
        if (pos1 != std::string::npos && pos2 != std::string::npos && pos3 != std::string::npos) {
            info.id = data.substr(0, pos1);
            info.name = data.substr(pos1 + 1, pos2 - pos1 - 1);
            info.os = data.substr(pos2 + 1, pos3 - pos2 - 1);
            info.online = data.substr(pos3 + 1) == "1";
        }
        return info;
    }
};

} // namespace RemoteProto
