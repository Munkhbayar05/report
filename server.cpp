#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <unistd.h>

class KVDatabase {
private:
    std::unordered_map<std::string, std::string> store;
    std::mutex mutex;

public:
    bool set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex);
        store[key] = value;
        return true;
    }

    bool get(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (store.find(key) != store.end()) {
            value = store[key];
            return true;
        }
        return false;
    }
    bool has(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        return store.find(key) != store.end();
    }

    bool upd(const std::string& key, const std::string& new_value) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = store.find(key);
        if (it != store.end()) {
            it->second = new_value;
            return true;
        }
        return false;
    }

    bool del(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        if (store.find(key) != store.end()) {
            store.erase(key);
            return true;
        }
        return false;
    }

    std::vector<std::string> keys() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> result;
        for (const auto& pair : store) {
            result.push_back(pair.first);
        }
        return result;
    }
};

class DatabaseManager {
private:
    std::unordered_map<std::string, KVDatabase> databases;
    std::mutex mutex;

public:
    KVDatabase& getDatabase(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex);
        return databases[name];
    }
    std::vector<std::string> listDatabases() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> names;
        for (const auto& pair : databases) {
            names.push_back(pair.first);
        }
        return names;
    }
    
};

struct Task {
    int client_socket;
    std::string command;
    Task(int socket, const std::string& cmd) : client_socket(socket), command(cmd) {}
};

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<Task> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    DatabaseManager& dbManager;
    std::unordered_map<int, std::string> clientDBs;
    std::mutex dbMapMutex;

    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string processCommand(int client_socket, const std::string& command) {
        std::vector<std::string> tokens = split(command, ' ');
        if (tokens.empty()) {
            return "error: empty command\n";
        }

        if (tokens[0] == "USE" && tokens.size() == 2) {
            std::lock_guard<std::mutex> lock(dbMapMutex);
            clientDBs[client_socket] = tokens[1];
            return "ok: switched to database " + tokens[1] + "\n";
        }

        std::string dbname = "db";
        {
            std::lock_guard<std::mutex> lock(dbMapMutex);
            if (clientDBs.count(client_socket)) {
                dbname = clientDBs[client_socket];
            }
        }

        KVDatabase& db = dbManager.getDatabase(dbname);

        if (tokens[0] == "SET" && tokens.size() >= 3) {
            std::string value = tokens[2];
            for (size_t i = 3; i < tokens.size(); i++) {
                value += " " + tokens[i];
            }

            db.set(tokens[1], value);
            return "OK\n";
        } else if (tokens[0] == "GET" && tokens.size() == 2) {
            std::string value;
            if (db.get(tokens[1], value)) {
                return "VALUE: " + value + "\n";
            } else {
                return "NULL: Key not found\n";
            }
        } else if (tokens[0] == "DEL" && tokens.size() == 2) {
            if (db.del(tokens[1])) {
                return "OK: Key deleted\n";
            } else {
                return "ERROR: Key not found\n";
            }
        } else if (tokens[0] == "HAS" && tokens.size() == 2) {
            if (db.has(tokens[1])) {
                return "YES: Key has\n";
            } else {
                return "NO: Key not found\n";
            }
        } else if (tokens[0] == "UPD" && tokens.size() >= 3) {
            std::string new_value = tokens[2];
            for (size_t i = 3; i < tokens.size(); i++) {
                new_value += " " + tokens[i];
            }
            if (db.upd(tokens[1], new_value)) {
                return "OK: Value update\n";
            } else {
                return "ERROR: Key not found\n";
            }
        }else if (tokens[0] == "DBNAME" && tokens.size() == 1) {
            std::string dbname = "db";
            {
                std::lock_guard<std::mutex> lock(dbMapMutex);
                if (clientDBs.count(client_socket)) {
                    dbname = clientDBs[client_socket];
                }
            }
            return "CURRENT DB: " + dbname + "\n";
        }
        else if (tokens[0] == "DBLIST" && tokens.size() == 1) {
            std::vector<std::string> dbs = dbManager.listDatabases();
            std::string result = "DATABASES:\n";
            for (const auto& name : dbs) {
                result += "- " + name + "\n";
            }
            return result;
        }
        

        else if (tokens[0] == "KEYS" && tokens.size() == 1) {
            std::vector<std::string> keys = db.keys();
            std::string result = "KEYS:\n";
            for (const auto& key : keys) {
                result += "- " + key + "\n";
            }
            return result;
        } else {
            return "ERROR: Unknown command or wrong format\n";
        }
    }

public:
    ThreadPool(size_t threads, DatabaseManager& manager) : stop(false), dbManager(manager) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    Task task(0, "");
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()){ 
                            return;
                        }
                        task = tasks.front();
                        tasks.pop();
                    }
                    std::string result = processCommand(task.client_socket, task.command);
                    send(task.client_socket, result.c_str(), result.length(), 0);
                }
            });
        }
    }

    void enqueue(Task task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(task);
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
    }
};

class ConnectionListener {
private:
    int server_fd;
    int port;
    ThreadPool& pool;
    std::atomic<bool> running;
    std::thread listener_thread;

public:
    ConnectionListener(int port_num, ThreadPool& thread_pool) 
        : port(port_num), pool(thread_pool), running(false) {}

    bool start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) return false;

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) return false;
        if (listen(server_fd, 10) < 0) return false;

        running = true;
        listener_thread = std::thread([this, address]() {
            while (running) {
                int addrlen = sizeof(address);
                int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                if (!running || client_socket < 0) continue;
                std::cout << "Client connected" << std::endl;
                std::thread([this, client_socket]() {
                    char buffer[1024] = {0};
                    while (true) {
                        memset(buffer, 0, sizeof(buffer));
                        int bytes_read = read(client_socket, buffer, sizeof(buffer));
                        if (bytes_read <= 0) {
                            close(client_socket);
                            return;
                        }
                        std::string command(buffer);
                        if (!command.empty() && command.back() == '\n') command.pop_back();
                        if (command == "EXIT") {
                            send(client_socket, "Goodbye!\n", 9, 0);
                            close(client_socket);
                            return;
                        }
                        pool.enqueue(Task(client_socket, command));
                    }
                }).detach();
            }
        });
        return true;
    }

    void stop() {
        running = false;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        close(sock);

        if (listener_thread.joinable()) {
            listener_thread.join();
        }
        close(server_fd);
    }

    ~ConnectionListener() {
        stop();
    }
};

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);

    const int NUM_THREADS = 4;
    DatabaseManager dbManager;
    ThreadPool pool(NUM_THREADS, dbManager);
    ConnectionListener listener(port, pool);

    if (!listener.start()) {
        std::cerr << "Failed to start listener" << std::endl;
        return -1;
    }

    std::cout << "Server started with " << NUM_THREADS << " worker threads on port " << port << std::endl;
    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    listener.stop();
    std::cout << "Server stopped" << std::endl;
    return 0;
}
