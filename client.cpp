#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {
    const char* server_ip = "127.0.0.1";  // Серверийн IP
    const int port = 8080;               // Серверийн порт

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation error\n";
        return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        std::cerr << "IP хаяг буруу эсвэл дэмжигдээгүй\n";
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Сервертэй холбогдож чадсангүй\n";
        return 1;
    }

    std::cout << "Сервертэй холбогдлоо. Командаа оруулна уу:\n";
    std::cout << "Командууд: SET <key> <value>, GET <key>, DEL <key>, KEYS, EXIT\n";

    while (true) {
        std::string input;
        std::cout << "> ";
        std::getline(std::cin, input);

        if (input.empty()) continue;

        input += "\n"; // Сервер newline-аар командыг дуусгасныг танина
        send(sock, input.c_str(), input.length(), 0);

        if (input.find("EXIT") != std::string::npos) {
            break;
        }

        char buffer[1024] = {0};
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread > 0) {
            std::cout << buffer;
        } else {
            std::cerr << "Серверээс уншиж чадсангүй\n";
            break;
        }
    }

    close(sock);
    std::cout << "Холболт хаагдлаа.\n";
    return 0;
}
