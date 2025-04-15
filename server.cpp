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
        return false;\
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
    KVDatabase& db;


    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string processCommand(const std::string& command) {
        std::vector<std::string> tokens = split(command, ' ');
        
        if (tokens.empty()) {
            return "ERROR: Empty command\n";
        }

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
        } else if (tokens[0] == "KEYS" && tokens.size() == 1) {
            std::vector<std::string> keys = db.keys();
            if (keys.empty()) {
                return "KEYS: (empty)\n";
            }
            
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
    ThreadPool(size_t threads, KVDatabase& database) : stop(false), db(database) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    Task task(0, "");
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) {
                            return;
                        }
                        task = tasks.front();
                        tasks.pop();
                    }
                    
                    std::string result = processCommand(task.command);// Команд боловсруулна
                    send(task.client_socket, result.c_str(), result.length(), 0);// Клиент рүү үр дүнг илгээнэ
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
        server_fd = socket(AF_INET, SOCK_STREAM, 0);//IPv4 TCP сокет үүсгэнэ
        if (server_fd < 0) {
            std::cerr << "Socket creation error" << std::endl;
            return false;
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { //Порт дахин ашиглах (Энэ нь серверийг хааж, дахин эхлүүлэхэд өмнөх порт түгжигдэхээс сэргийлнэ.)
            std::cerr << "Setsockopt error" << std::endl;
            return false;
        }

        struct sockaddr_in address;
        address.sin_family = AF_INET;//AF_INET – IPv4 хаяг ашиглана.
        address.sin_addr.s_addr = INADDR_ANY;//Бүх сүлжээний интерфэйс дээр ажиллана.
        address.sin_port = htons(port);//port-ыг big-endian формат руу хөрвүүлнэ.
 
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {//bind() нь серверийг порт дээр холбож байна.
            std::cerr << "Bind failed" << std::endl;
            return false;
        }

        if (listen(server_fd, 10) < 0) {//listen() нь серверийг клиентээс холболт хүлээх горимд оруулна.
            std::cerr << "Listen failed" << std::endl;
            return false;
        }

        running = true;
        listener_thread = std::thread([this, address]() {//listener_thread нэртэй шинэ утас үүсгэж, серверийг асуулгатай ажиллуулах.
            std::cout << "Listener started on port " << port << std::endl;
            
            while (running) {
                int addrlen = sizeof(address);//accept() нь шинэ клиент холболтыг хүлээж авах бөгөөд шинэ сокет буцаана.
                int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);//client_socket – тухайн клиенттэй холбогдох шинэ сокет.
                
                if (!running) break;//Хэрэв сервер унтраасан бол давталтыг зогсооно.
                
                if (client_socket < 0) {
                    std::cerr << "Accept failed" << std::endl;//Хэрэв клиент холбогдож чадсангүй бол алдааны мэдэгдэл хэвлэнэ.
                    continue;
                }

                std::cout << "Client connected" << std::endl;//Клиент холбогдвол мэдээлэл хэвлэнэ.
                
                std::thread([this, client_socket]() {
                    char buffer[1024] = {0};
                    
                    while (true) {
                        memset(buffer, 0, sizeof(buffer));
                        int bytes_read = read(client_socket, buffer, sizeof(buffer));
                        
                        if (bytes_read <= 0) {
                            std::cout << "Client disconnected" << std::endl;
                            close(client_socket);
                            return;
                        }

                        std::string command(buffer);
                        if (!command.empty() && command[command.length()-1] == '\n') {
                            command.erase(command.length()-1);
                        }

                        if (command == "EXIT") {
                            std::string msg = "Goodbye!\n";
                            send(client_socket, msg.c_str(), msg.length(), 0);
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
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    const int NUM_THREADS = 4;

    KVDatabase db;

    ThreadPool pool(NUM_THREADS, db);

    ConnectionListener listener(port, pool);
    if (!listener.start()) {
        std::cerr << "Failed to start listener" << std::endl;
        return -1;
    }

    std::cout << "Server started with " << NUM_THREADS << " worker threads" << std::endl;
    std::cout << "Press Enter to stop the server..." << std::endl;

    std::cin.get();
    
    std::cout << "Stopping server..." << std::endl;
    listener.stop();
    std::cout << "Server stopped" << std::endl;

    return 0;
}
