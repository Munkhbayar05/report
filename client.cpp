#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>


void printHelp() {
    std::cout << "\nКомандууд:" << std::endl;
    std::cout << "  SET <key> <value>  - Түлхүүр утга хосыг хадгалах" << std::endl;
    std::cout << "  GET <key>          - Түлхүүрээр утгыг олж авах" << std::endl;
    std::cout << "  DEL <key>          - Түлхүүр утгын хосыг устгах" << std::endl;
    std::cout << "  UPD <key><value>   - Түлхүүрээр утгыг өөрчлөх" << std::endl;
    std::cout << "  HAS <key>          - Түлхүүр өгөгдлийн санд байгааг харах" << std::endl;
    std::cout << "  KEYS               - Өгөгдлийн сангийн бүх түлхүүрүүдийг жагсаах" << std::endl;
    std::cout << "  USE                - Хэрэглэгч өөрийн ашиглаж буй database солих." << std::endl;
    std::cout << "  DBNAME             - Тухайн хэрэглэгчийн идэвхтэй database-г харуулах." << std::endl;
    std::cout << "  DBLIST             - Бүх database-ийн нэрсийг жагсаах" << std::endl;
    std::cout << "  EXIT               - Гарах" << std::endl;
}

int main() {
    const char* server_ip = "127.0.0.1";  
    const int port = 8080;               

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket cretion error\n";
        return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid or unsupported IP address\n";
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Failed to server\n";
        return 1;
    }

    std::cout << "Connected to the server. Please enter a command:\n";
    printHelp();

    while (true) {
        std::string input;
        std::cout << "db> ";
        std::getline(std::cin, input);

        if (input.empty()) continue;

        input += "\n"; 
        send(sock, input.c_str(), input.length(), 0);

        if (input.find("EXIT") != std::string::npos) {
            break;
        }

        char buffer[1024] = {0};
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread > 0) {
            std::cout << buffer;
        } else {
            std::cerr << "Failed to server\n";
            break;
        }
    }

    close(sock);
    std::cout << "Connection closed.\n";
    return 0;
}
