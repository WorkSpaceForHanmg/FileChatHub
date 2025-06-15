#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <fstream>

constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int SERVER_PORT = 9001;
constexpr int BUFFER_SIZE = 1024;

class ChatClient {
public:
    ChatClient() : isRunning(false), clientSocket(-1) {}

    ~ChatClient() {
        disconnect();
    }

    void run() {
        if (!connectToServer()) {
            return;
        }

        printWelcomeMessage();
        startMessageReceiver();
        handleUserInput();
        stopMessageReceiver();
    }

private:
    std::atomic<bool> isRunning;
    int clientSocket;
    std::mutex coutMutex;
    std::thread receiverThread;

    bool connectToServer() {
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == -1) {
            printError("소켓 생성 실패");
            return false;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(SERVER_PORT);

        if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0) {
            printError("잘못된 서버 주소");
            close(clientSocket);
            return false;
        }

        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
            printError("서버 연결 실패");
            close(clientSocket);
            return false;
        }

        isRunning = true;
        return true;
    }

    void disconnect() {
        if (clientSocket != -1) {
            close(clientSocket);
            clientSocket = -1;
        }
    }

    void printWelcomeMessage() {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "=== 서버에 연결되었습니다 ===\n";
        std::cout << "명령어:\n";
        std::cout << "/nick <닉네임>        - 닉네임 설정\n";
        std::cout << "/upload <파일명>       - 파일 업로드\n";
        std::cout << "/download <파일명>     - 파일 다운로드\n";
        std::cout << "/quit                  - 종료\n";
        std::cout << "============================\n";
    }

    void printError(const std::string& msg) {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cerr << msg << ": " << strerror(errno) << "\n";
    }

    void startMessageReceiver() {
        receiverThread = std::thread(&ChatClient::receiveMessages, this);
    }

    void stopMessageReceiver() {
        isRunning = false;
        if (receiverThread.joinable()) {
            receiverThread.join();
        }
    }

    void receiveMessages() {
        char buffer[BUFFER_SIZE];

        while (isRunning) {
            memset(buffer, 0, sizeof(buffer));
            int recvLen = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (recvLen <= 0) {
                if (isRunning) {
                    printInfo("서버와 연결이 끊어졌습니다.");
                }
                isRunning = false;
                disconnect();
                break;
            }

            printInfo(buffer);
        }
    }

    void handleUserInput() {
        std::string input;

        while (isRunning) {
            printPrompt();

            std::getline(std::cin, input);
            if (!isRunning) break;
            if (input.empty()) continue;

            if (input.rfind("/upload ", 0) == 0) {
                uploadFile(input.substr(8));
            }
            else if (input.rfind("/download ", 0) == 0) {
                downloadFile(input.substr(10));
            }
            else if (input == "/quit") {
                sendMessage("QUIT\n");
                isRunning = false;
                break;
            }
            else {
                sendMessage(input + "\n");
            }
        }
    }

    void sendMessage(const std::string& message) {
        if (send(clientSocket, message.c_str(), message.length(), 0) <= 0) {
            printError("메시지 전송 실패");
            isRunning = false;
        }
    }

    void uploadFile(const std::string& filename) {
        std::ifstream infile(filename, std::ios::binary);
        if (!infile) {
            printError("파일 열기 실패: " + filename);
            return;
        }

        sendMessage("UPLOAD " + filename + "\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 

        char buffer[BUFFER_SIZE];
        while (infile.read(buffer, sizeof(buffer)) || infile.gcount() > 0) {
            send(clientSocket, buffer, infile.gcount(), 0);
        }

        sendMessage("EOF\n");
        printInfo("파일 업로드 완료: " + filename);
    }

    void downloadFile(const std::string& filename) {
        sendMessage("DOWNLOAD " + filename + "\n");

        std::ofstream outfile(filename, std::ios::binary);
        char buffer[BUFFER_SIZE];

        while (isRunning) {
            memset(buffer, 0, sizeof(buffer));
            int recvLen = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (recvLen <= 0) {
                printError("다운로드 중 연결 끊김");
                break;
            }

            if (strncmp(buffer, "EOF", 3) == 0) {
                break;
            }

            outfile.write(buffer, recvLen);
        }

        outfile.close();
        printInfo("파일 다운로드 완료: " + filename);
    }

    void printPrompt() {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "> " << std::flush;
    }

    void printInfo(const std::string& msg) {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "\n" << msg << "\n";
    }
};

int main() {
    ChatClient client;
    client.run();
    return 0;
}
