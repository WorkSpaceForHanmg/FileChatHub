#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>

#define MAX_ROOMS 10
#define MAX_USERS_PER_ROOM 40
#define BUFFER_SIZE 8192
#define PORT 9000

struct ClientInfo {
    int sock;
    std::string nickname;
    int roomId = -1;
};

struct Room {
    std::string name;
    std::set<int> clients;
};

std::map<int, ClientInfo> clients;
std::map<int, Room> rooms;
std::mutex mtx;
int roomCounter = 0;

std::string getRoomUploadDir(int roomId) {
    return "uploads/" + std::to_string(roomId) + "/";
}

void ensureDirExists(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) == -1) {
        mkdir(dir.c_str(), 0755);
    }
}

std::string getFileListInRoom(int roomId) {
    std::ostringstream oss;
    std::string dirPath = getRoomUploadDir(roomId);
    DIR* dp = opendir(dirPath.c_str());
    oss << "[파일 목록]\n";
    if (dp) {
        struct dirent* ep;
        while ((ep = readdir(dp)) != NULL) {
            if (ep->d_type == DT_REG) {
                oss << "- " << ep->d_name << "\n";
            }
        }
        closedir(dp);
    } else {
        oss << "(파일 없음)\n";
    }
    return oss.str();
}

bool receiveFile(int sock, const std::string& filePath, int fileSize) {
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs) return false;

    char buffer[BUFFER_SIZE];
    int received = 0;
    while (received < fileSize) {
        int toRead = std::min(BUFFER_SIZE, fileSize - received);
        int len = recv(sock, buffer, toRead, 0);
        if (len <= 0) return false;
        ofs.write(buffer, len);
        received += len;
    }
    ofs.close();
    return true;
}

void sendDownloadHeader(int sock, int fileSize) {
    std::string notice = "파일 크기를 4바이트 int로 먼저 전송합니다. 그 다음 파일 데이터가 전송됩니다.\n";
    int fileSizeNet = htonl(fileSize);
    std::vector<char> header(notice.begin(), notice.end());
    header.insert(header.end(), (char*)&fileSizeNet, (char*)&fileSizeNet + sizeof(fileSizeNet));
    send(sock, header.data(), header.size(), 0);
}

bool sendFile(int sock, const std::string& filePath) {
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs) return false;
    int fileSize = ifs.tellg();
    ifs.seekg(0);

    sendDownloadHeader(sock, fileSize);

    char buffer[BUFFER_SIZE];
    int sent = 0;
    while (sent < fileSize) {
        int toSend = std::min(BUFFER_SIZE, fileSize - sent);
        ifs.read(buffer, toSend);
        int len = send(sock, buffer, toSend, 0);
        if (len <= 0) return false;
        sent += len;
    }
    ifs.close();
    return true;
}

void sendToClient(int sock, const std::string& message) {
    send(sock, message.c_str(), message.length(), 0);
}

void broadcastToRoom(int roomId, const std::string& message, int excludeSock = -1) {
    for (int client : rooms[roomId].clients) {
        if (client != excludeSock) {
            sendToClient(client, message);
        }
    }
}

void cleanupEmptyRooms() {
    std::vector<int> roomsToDelete;
    for (const auto& [id, room] : rooms) {
        if (room.clients.empty()) {
            roomsToDelete.push_back(id);
        }
    }
    for (int id : roomsToDelete) {
        rooms.erase(id);
        std::cout << "빈 채팅방 #" << id << "이(가) 삭제되었습니다.\n";
    }
}

std::string getRoomList() {
    std::ostringstream oss;
    oss << "[채팅방 목록]\n";
    for (const auto& [id, room] : rooms) {
        oss << id << ": " << room.name << " (" << room.clients.size() << "명)\n";
    }
    return oss.str();
}

std::string getUserListInRoom(int roomId) {
    std::ostringstream oss;
    oss << "[사용자 목록]\n";
    for (int sock : rooms[roomId].clients) {
        oss << "- " << clients[sock].nickname << "\n";
    }
    return oss.str();
}

void handleClient(int clientSock) {
    char buffer[BUFFER_SIZE];
    {
        std::lock_guard<std::mutex> lock(mtx);
        clients[clientSock] = {clientSock, "", -1};
        std::cout << "클라이언트 연결됨: 소켓 " << clientSock << "\n";
    }

    sendToClient(clientSock, "닉네임을 설정하세요: ");

    // 닉네임 설정
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(clientSock, buffer, sizeof(buffer), 0);
        if (len <= 0) break;

        std::string input(buffer);
        std::istringstream iss(input);
        std::string cmd;
        iss >> cmd;

        std::lock_guard<std::mutex> lock(mtx);

        if (cmd == "/quit") {
            break;
        } else if (cmd == "/help") {
            sendToClient(clientSock,
                "/create <방이름>\n/join <방번호>\n/nick <닉네임>\n/list\n/w <닉네임> <내용>\n/exit\n/files\n/upload <파일명>\n/download <파일명>\n/quit\n");

        } else if (cmd == "/nick") {
            std::string newNick;
            iss >> newNick;

            bool duplicate = false;
            for (const auto& [_, c] : clients) {
                if (c.roomId == clients[clientSock].roomId && c.nickname == newNick) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                sendToClient(clientSock, "해당 닉네임은 이미 사용 중입니다.\n");
            } else {
                clients[clientSock].nickname = newNick;
                sendToClient(clientSock, "닉네임 설정 완료.\n");
                std::cout << "클라이언트 " << clientSock << " 닉네임 변경: " << newNick << "\n";
                break;
            }
        } else {
            sendToClient(clientSock, "닉네임을 먼저 설정해주세요.\n");
        }
    }

    sendToClient(clientSock, "명령어를 입력하세요 (/help)\n");

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(clientSock, buffer, sizeof(buffer), 0);
        if (len <= 0) break;

        std::string input(buffer);
        std::istringstream iss(input);
        std::string cmd;
        iss >> cmd;

        std::lock_guard<std::mutex> lock(mtx);

        if (cmd == "/quit") {
            break;
        } else if (cmd == "/help") {
            sendToClient(clientSock,
                "/create <방이름>\n/join <방번호>\n/nick <닉네임>\n/list\n/w <닉네임> <내용>\n/exit\n/files\n/upload <파일명>\n/download <파일명>\n/quit\n");

        } else if (cmd == "/create") {
            std::string roomName;
            iss >> roomName;
            if (rooms.size() >= MAX_ROOMS) {
                sendToClient(clientSock, "방 최대 개수를 초과했습니다.\n");
            } else {
                int newRoomId = roomCounter++;
                rooms[newRoomId] = {roomName, {clientSock}};
                clients[clientSock].roomId = newRoomId;
                sendToClient(clientSock, "방 생성 및 입장 완료.\n");
                ensureDirExists("uploads");
                ensureDirExists(getRoomUploadDir(newRoomId));
                std::cout << "새 채팅방 생성됨: #" << newRoomId << " (" << roomName << ")\n";
            }
        } else if (cmd == "/join") {
            int roomId;
            iss >> roomId;
            if (rooms.count(roomId) == 0) {
                sendToClient(clientSock, "존재하지 않는 방입니다.\n");
            } else if (rooms[roomId].clients.size() >= MAX_USERS_PER_ROOM) {
                sendToClient(clientSock, "방 인원 초과입니다.\n");
            } else {
                rooms[roomId].clients.insert(clientSock);
                clients[clientSock].roomId = roomId;
                ensureDirExists("uploads");
                ensureDirExists(getRoomUploadDir(roomId));
                sendToClient(clientSock, "방 입장 완료.\n");
                broadcastToRoom(roomId, clients[clientSock].nickname + " 님이 입장했습니다.\n", clientSock);
                std::cout << "클라이언트 " << clientSock << "이(가) 방 #" << roomId << "에 입장\n";
            }
        } else if (cmd == "/list") {
            int roomId = clients[clientSock].roomId;
            if (roomId == -1) {
                sendToClient(clientSock, getRoomList());
            } else {
                sendToClient(clientSock, getUserListInRoom(roomId));
            }
        } else if (cmd == "/files") {
            int roomId = clients[clientSock].roomId;
            if (roomId == -1) {
                sendToClient(clientSock, "방에 입장 후 사용 가능합니다.\n");
            } else {
                sendToClient(clientSock, getFileListInRoom(roomId));
            }
        } else if (cmd == "/upload") {
            int roomId = clients[clientSock].roomId;
            if (roomId == -1) {
                sendToClient(clientSock, "방에 입장 후 파일 업로드가 가능합니다.\n");
                continue;
            }
            std::string fileName;
            iss >> fileName;
            if (fileName.empty()) {
                sendToClient(clientSock, "파일명을 입력해주세요. 예: /upload test.txt\n");
                continue;
            }
            std::string filePath = getRoomUploadDir(roomId) + fileName;
            sendToClient(clientSock, "파일 크기를 4바이트 int로 먼저 보내고, 그 다음 파일 데이터를 전송하세요.\n");
            int fileSizeNet;
            int len = recv(clientSock, &fileSizeNet, sizeof(fileSizeNet), 0);
            if (len != sizeof(fileSizeNet)) {
                sendToClient(clientSock, "파일 크기 수신 실패\n");
                continue;
            }
            int fileSize = ntohl(fileSizeNet);
            if (fileSize <= 0) {
                sendToClient(clientSock, "파일 크기 오류\n");
                continue;
            }
            if (receiveFile(clientSock, filePath, fileSize)) {
                sendToClient(clientSock, "파일 업로드 성공\n");
                broadcastToRoom(roomId, "[알림] " + clients[clientSock].nickname + " 님이 파일을 업로드: " + fileName + "\n", clientSock);
                std::cout << "파일 업로드: " << filePath << " (" << fileSize << " bytes)\n";
            } else {
                sendToClient(clientSock, "파일 업로드 실패\n");
            }
        } else if (cmd == "/download") {
            int roomId = clients[clientSock].roomId;
            if (roomId == -1) {
                sendToClient(clientSock, "방에 입장 후 파일 다운로드가 가능합니다.\n");
                continue;
            }
            std::string fileName;
            iss >> fileName;
            if (fileName.empty()) {
                sendToClient(clientSock, "파일명을 입력해주세요. 예: /download test.txt\n");
                continue;
            }
            std::string filePath = getRoomUploadDir(roomId) + fileName;
            if (access(filePath.c_str(), F_OK) == -1) {
                sendToClient(clientSock, "파일이 존재하지 않습니다.\n");
                continue;
            }
            if (sendFile(clientSock, filePath)) {
                std::cout << "파일 다운로드: " << filePath << "\n";
            } else {
                sendToClient(clientSock, "파일 전송 실패\n");
            }
        } else if (cmd == "/nick") {
            std::string newNick;
            iss >> newNick;

            bool duplicate = false;
            for (const auto& [_, c] : clients) {
                if (c.roomId == clients[clientSock].roomId && c.nickname == newNick) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                sendToClient(clientSock, "해당 닉네임은 이미 사용 중입니다.\n");
            } else {
                clients[clientSock].nickname = newNick;
                sendToClient(clientSock, "닉네임 설정 완료.\n");
                std::cout << "클라이언트 " << clientSock << " 닉네임 변경: " << newNick << "\n";
            }
        } else if (cmd == "/w") {
            std::string targetNick, message;
            iss >> targetNick;
            std::getline(iss, message);
            message = message.substr(1);

            bool found = false;
            for (const auto& [sock, c] : clients) {
                if (c.nickname == targetNick) {
                    sendToClient(sock, "[귓속말] " + clients[clientSock].nickname + ": " + message + "\n");
                    found = true;
                    break;
                }
            }
            if (!found) {
                sendToClient(clientSock, "해당 닉네임의 유저를 찾을 수 없습니다.\n");
            }
        } else if (cmd == "/exit") {
            int roomId = clients[clientSock].roomId;
            if (roomId != -1) {
                rooms[roomId].clients.erase(clientSock);
                broadcastToRoom(roomId, clients[clientSock].nickname + " 님이 나갔습니다.\n", clientSock);
                clients[clientSock].roomId = -1;
                sendToClient(clientSock, "방을 나왔습니다.\n");
                std::cout << "클라이언트 " << clientSock << "이(가) 방 #" << roomId << "에서 퇴장\n";
                if (rooms[roomId].clients.empty()) {
                    rooms.erase(roomId);
                    std::cout << "빈 채팅방 #" << roomId << "이(가) 삭제되었습니다.\n";
                }
            }
        } else {
            int roomId = clients[clientSock].roomId;
            if (roomId == -1) {
                sendToClient(clientSock, "방에 입장 후 메시지를 입력할 수 있습니다.\n");
            } else {
                std::string nickname = clients[clientSock].nickname;
                std::string message = "[" +nickname + "] : " + input;
                broadcastToRoom(roomId, message, clientSock);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        int roomId = clients[clientSock].roomId;
        if (roomId != -1) {
            rooms[roomId].clients.erase(clientSock);
            broadcastToRoom(roomId, clients[clientSock].nickname + " 님이 나갔습니다.\n", clientSock);
            std::cout << "클라이언트 " << clientSock << "이(가) 방 #" << roomId << "에서 퇴장\n";
            if (rooms[roomId].clients.empty()) {
                rooms.erase(roomId);
                std::cout << "빈 채팅방 #" << roomId << "이(가) 삭제되었습니다.\n";
            }
        }
        clients.erase(clientSock);
        std::cout << "클라이언트 연결 종료: 소켓 " << clientSock << "\n";
    }
    close(clientSock);
}

void cleanupTask() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(1));
        cleanupEmptyRooms();
    }
}

int main() {
    ensureDirExists("uploads");
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "소켓 생성 실패\n";
        return -1;
    }
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "바인드 실패\n";
        return -1;
    }

    if (listen(serverSock, 5) < 0) {
        std::cerr << "리스닝 실패\n";
        return -1;
    }

    std::cout << "서버 시작됨, 포트: " << PORT << "\n";

    std::thread cleanupThread(cleanupTask);
    cleanupThread.detach();

    while (true) {
        int clientSock = accept(serverSock, NULL, NULL);
        if (clientSock < 0) {
            std::cerr << "클라이언트 연결 실패\n";
            continue;
        }
        std::thread clientThread(handleClient, clientSock);
        clientThread.detach();
    }
    return 0;
}