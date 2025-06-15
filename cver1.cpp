#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <iomanip>

#define BUFFER_SIZE 8192

int sock;

void printProgressBar(int percent, const std::string& prefix) {
    int width = 50;
    int pos = percent * width / 100;
    std::cout << "\r" << prefix << " [";
    for (int i = 0; i < width; i++) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::setw(3) << percent << "% 완료" << std::flush;
}

// 서버에서 오는 메시지 읽기
void recvThreadFunc() {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (len <= 0) {
            std::cout << "\n[서버와 연결이 끊어졌습니다]\n";
            exit(0);
        }
        buffer[len] = 0;
        std::cout << buffer;
        std::cout.flush();
    }
}

// 파일 업로드 함수 (진행률 표시)
void uploadFile(const std::string& cmd, const std::string& fileName) {
    std::ifstream ifs(fileName, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cout << "[클라이언트] 파일을 열 수 없습니다: " << fileName << "\n";
        return;
    }
    int fileSize = ifs.tellg();
    ifs.seekg(0);

    std::string uploadCmd = cmd + " " + fileName + "\n";
    send(sock, uploadCmd.c_str(), uploadCmd.length(), 0);

    char buffer[BUFFER_SIZE];
    int len = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (len <= 0) {
        std::cout << "[클라이언트] 서버 응답 없음\n";
        return;
    }
    buffer[len] = 0;
    std::cout << buffer;

    int fileSizeNet = htonl(fileSize);
    send(sock, &fileSizeNet, sizeof(fileSizeNet), 0);

    int sent = 0;
    int lastPercent = -1;
    while (sent < fileSize) {
        int toSend = std::min(BUFFER_SIZE, fileSize - sent);
        ifs.read(buffer, toSend);
        int l = send(sock, buffer, toSend, 0);
        if (l <= 0) {
            std::cout << "[클라이언트] 파일 전송 중 오류\n";
            return;
        }
        sent += l;
        int percent = static_cast<int>((long long)sent * 100 / fileSize);
        if (percent != lastPercent && percent % 1 == 0) {
            printProgressBar(percent, "[업로드]");
            lastPercent = percent;
        }
    }
    ifs.close();
    printProgressBar(100, "[업로드]");
    std::cout << std::endl;

    len = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (len > 0) {
        buffer[len] = 0;
        std::cout << buffer;
    }
}

// 파일 다운로드 함수 (진행률 표시)
void downloadFile(const std::string& cmd, const std::string& fileName) {
    std::string downloadCmd = cmd + " " + fileName + "\n";
    send(sock, downloadCmd.c_str(), downloadCmd.length(), 0);

    char buffer[BUFFER_SIZE];
    int len = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (len <= 0) {
        std::cout << "[클라이언트] 서버 응답 없음\n";
        return;
    }
    buffer[len] = 0;
    std::cout << buffer;
    if (strstr(buffer, "파일이 존재하지 않습니다") || strstr(buffer, "다운로드가 가능") == nullptr) {
        return;
    }
    int fileSizeNet;
    len = recv(sock, &fileSizeNet, sizeof(fileSizeNet), 0);
    if (len != sizeof(fileSizeNet)) {
        std::cout << "[클라이언트] 파일 크기 수신 실패\n";
        return;
    }
    int fileSize = ntohl(fileSizeNet);
    if (fileSize <= 0) {
        std::cout << "[클라이언트] 파일 크기 오류\n";
        return;
    }
    std::ofstream ofs(fileName, std::ios::binary);
    if (!ofs) {
        std::cout << "[클라이언트] 파일 저장 실패: " << fileName << "\n";
        return;
    }
    int received = 0;
    int lastPercent = -1;
    while (received < fileSize) {
        int toRead = std::min(BUFFER_SIZE, fileSize - received);
        int l = recv(sock, buffer, toRead, 0);
        if (l <= 0) {
            std::cout << "[클라이언트] 파일 수신 중 오류\n";
            ofs.close();
            return;
        }
        ofs.write(buffer, l);
        received += l;
        int percent = static_cast<int>((long long)received * 100 / fileSize);
        if (percent != lastPercent && percent % 1 == 0) {
            printProgressBar(percent, "[다운로드]");
            lastPercent = percent;
        }
    }
    ofs.close();
    printProgressBar(100, "[다운로드]");
    std::cout << "\n[클라이언트] 파일 다운로드 완료: " << fileName << std::endl;
}

int main() {
    std::string serverIp;
    int port = 9000;
    std::cout << "서버 IP를 입력하세요: ";
    std::cin >> serverIp;

    struct sockaddr_in serverAddr;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "소켓 생성 실패\n";
        return -1;
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "서버 연결 실패\n";
        return -1;
    }

    std::thread recvThread(recvThreadFunc);
    recvThread.detach();

    std::cin.ignore();

    while (true) {
        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) continue;

        std::istringstream iss(input);
        std::string cmd, arg;
        iss >> cmd >> arg;

        if (cmd == "/upload") {
            if (arg.empty()) {
                std::cout << "사용법: /upload <파일명>\n";
                continue;
            }
            uploadFile(cmd, arg);
        } else if (cmd == "/download") {
            if (arg.empty()) {
                std::cout << "사용법: /download <파일명>\n";
                continue;
            }
            downloadFile(cmd, arg);
        } else {
            input += "\n";
            send(sock, input.c_str(), input.length(), 0);
        }
    }
    close(sock);
    return 0;
}