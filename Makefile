CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I.
LDFLAGS = -pthread

# Все цели
all: relay_server remote_agent admin_client

# Relay сервер (для VPS)
relay_server: relay/main.cpp relay/relay_server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Агент (для удалённых компьютеров)
remote_agent: agent/main.cpp agent/agent.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Админ клиент (для управления)
admin_client: admin/main.cpp admin/admin_client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Старые компоненты (для прямого подключения)
legacy: remote_server remote_client

remote_server: server/main.cpp server/server.cpp server/shell_executor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

remote_client: client/main.cpp client/client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f relay_server remote_agent admin_client remote_server remote_client

.PHONY: all legacy clean
