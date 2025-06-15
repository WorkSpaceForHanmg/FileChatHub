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
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <string> // <-- 이 헤더 추가

constexpr int MAX_ROOMS = 10;
constexpr int MAX_USERS_PER_ROOM = 40;
constexpr int BUFFER_SIZE = 4096;
constexpr int PORT = 9001;
const std::string STORAGE_DIR = "/tmp/mydrive/";

struct ClientInfo {
    int socket;
    std::string nickname;
    std::string password;
    int roomId = -1;
};

struct Room {
    std::string name;
    std::set<int> clients;
};

class ChatServer {
public:
    ChatServer() {
        mkdir(STORAGE_DIR.c_str(), 0777);
    }

    void run() {
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            std::cerr << "소켓 생성 실패: " << strerror(errno) << "\n";
            return;
        }

        // 소켓 옵션 설정 (재사용 허용)
        int opt = 1;
        if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            std::cerr << "setsockopt 실패: " << strerror(errno) << "\n";
            close(server_sock);
            return;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "바인드 실패: " << strerror(errno) << "\n";
            close(server_sock);
            return;
        }

        if (listen(server_sock, 5) < 0) {
            std::cerr << "리스닝 실패: " << strerror(errno) << "\n";
            close(server_sock);
            return;
        }

        std::cout << "서버가 포트 " << PORT << "에서 대기 중입니다...\n";

        while (true) {
            int client_sock = accept(server_sock, nullptr, nullptr);
            if (client_sock < 0) {
                std::cerr << "클라이언트 연결 실패: " << strerror(errno) << "\n";
                continue;
            }
            std::thread client_thread(&ChatServer::handleClient, this, client_sock);
            client_thread.detach();
        }

        close(server_sock);
    }

private:
    std::map<int, ClientInfo> clients;
    std::map<int, Room> rooms;
    std::mutex mtx;
    int roomCounter = 0;

    void sendToClient(int sock, const std::string &msg) {
        if (send(sock, msg.c_str(), msg.size(), 0) < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                std::cerr << "메시지 전송 실패 (소켓 " << sock << "): " << strerror(errno) << "\n";
            }
        }
    }

    void sendLine(int sock, const std::string &msg) {
        sendToClient(sock, msg + "\n");
    }

    void broadcastToRoom(int roomId, const std::string &msg, int except = -1) {
        std::lock_guard<std::mutex> lock(mtx);
        if (rooms.find(roomId) != rooms.end()) {
            for (int s : rooms[roomId].clients) {
                if (s != except) sendToClient(s, msg);
            }
        }
    }

    void cleanupEmptyRooms() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<int> toErase;
        for (auto &p : rooms) {
            if (p.second.clients.empty()) toErase.push_back(p.first);
        }
        for (int rid : toErase) {
            rooms.erase(rid);
            std::cout << "빈 방 #" << rid << " 제거됨\n";
        }
    }

    std::string getRoomList() {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream oss;
        oss << "[현재 방 목록]\n";
        for (auto &p : rooms) {
            oss << "방번호 " << p.first << " | " << p.second.name 
                << " | 인원: " << p.second.clients.size() << "\n";
        }
        return oss.str();
    }

    std::string getUserList(int roomId) {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream oss;
        oss << "[현재 방 사용자 목록]\n";
        if (rooms.find(roomId) != rooms.end()) {
            for (int s : rooms[roomId].clients) {
                if (clients.find(s) != clients.end()) {
                    oss << clients[s].nickname << "\n";
                }
            }
        }
        return oss.str();
    }

    std::vector<std::string> listFiles() {
        std::vector<std::string> files;
        DIR *dp = opendir(STORAGE_DIR.c_str());
        if (dp) {
            struct dirent *ep;
            while ((ep = readdir(dp))) {
                if (ep->d_type == DT_REG) files.push_back(ep->d_name);
            }
            closedir(dp);
        }
        return files;
    }

    void cleanupClient(int sock) {
        std::lock_guard<std::mutex> lock(mtx);
        if (clients.find(sock) != clients.end()) {
            int rid = clients[sock].roomId;
            if (rid != -1 && rooms.find(rid) != rooms.end()) {
                rooms[rid].clients.erase(sock);
                broadcastToRoom(rid, clients[sock].nickname + " 님이 나갔습니다.\n", sock);
            }
            clients.erase(sock);
        }
        close(sock);
        std::cout << "클라이언트 소켓 " << sock << " 종료\n";
    }

    void handleClient(int sock) {
        char buf[BUFFER_SIZE];
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            clients[sock] = {sock, "", "", -1};
            std::cout << "새 클라이언트 연결: 소켓 " << sock << "\n";
        }
        
        sendLine(sock, "환영합니다! /register 또는 /login <닉네임> <비밀번호> 를 입력하세요.");

        bool auth = false;
        while (!auth) {
            int len = recv(sock, buf, sizeof(buf)-1, 0);
            if (len <= 0) {
                cleanupClient(sock);
                return;
            }
            buf[len] = '\0'; // 문자열 종료
            
            // 공백으로 명령어 분할
            std::vector<std::string> tokens;
            std::istringstream iss(buf);
            std::string token;
            while (iss >> token) {
                tokens.push_back(token);
            }
            
            if (tokens.size() < 3) {
                sendLine(sock, "잘못된 형식입니다. /register 또는 /login <닉네임> <비밀번호>");
                continue;
            }
            
            std::string cmd = tokens[0];
            std::string u = tokens[1];
            std::string p = tokens[2];
            
            std::lock_guard<std::mutex> lock(mtx);
            if (cmd == "/register") {
                clients[sock].nickname = u;
                clients[sock].password = p;
                auth = true;
                sendLine(sock, "회원가입 및 로그인 성공!");
            } else if (cmd == "/login") {
                bool ok = false;
                for (auto &client : clients) {
                    if (client.second.nickname == u && client.second.password == p) {
                        ok = true;
                        break;
                    }
                }
                if (ok) {
                    clients[sock].nickname = u;
                    clients[sock].password = p;
                    auth = true;
                    sendLine(sock, "로그인 성공!");
                } else {
                    sendLine(sock, "로그인 실패. 닉네임 또는 비밀번호를 확인하세요.");
                }
            } else {
                sendLine(sock, "/register 또는 /login <닉네임> <비밀번호> 를 사용하세요.");
            }
        }
        
        sendLine(sock, "명령어 목록은 /help 를 입력하세요.");

        while (true) {
            int len = recv(sock, buf, sizeof(buf)-1, 0);
            if (len <= 0) break;
            buf[len] = '\0';
            
            std::istringstream iss(buf);
            std::string cmd;
            iss >> cmd;

            if (cmd == "/quit") {
                break;
            } else if (cmd == "/help") {
                sendToClient(sock,
                    "/create <방이름>\n/join <방번호>\n/list\n/exit\n/upload <파일명>\n"
                    "/download <파일명>\n/files\n/comment <파일명> <메시지>\n/showc <파일명>\n/quit\n");
            } else if (cmd == "/create") {
                std::string rn;
                std::getline(iss >> std::ws, rn);
                if (rn.empty()) {
                    sendLine(sock, "방 이름을 입력하세요.");
                    continue;
                }
                std::lock_guard<std::mutex> lock(mtx);
                if (rooms.size() >= MAX_ROOMS) {
                    sendLine(sock, "방 최대 개수를 초과했습니다.");
                } else {
                    int rid = roomCounter++;
                    rooms[rid] = {rn, {sock}};
                    clients[sock].roomId = rid;
                    sendLine(sock, "방 생성 및 입장 완료! 방 번호: " + std::to_string(rid));
                }
            } else if (cmd == "/join") {
                int rid;
                if (!(iss >> rid)) {
                    sendLine(sock, "사용법: /join <방번호>");
                    continue;
                }
                std::lock_guard<std::mutex> lock(mtx);
                if (rooms.find(rid) == rooms.end()) {
                    sendLine(sock, "해당 방이 존재하지 않습니다.");
                } else if (rooms[rid].clients.size() >= MAX_USERS_PER_ROOM) {
                    sendLine(sock, "방이 가득 찼습니다.");
                } else {
                    // 현재 방이 있으면 먼저 나가기
                    int currentRoom = clients[sock].roomId;
                    if (currentRoom != -1) {
                        rooms[currentRoom].clients.erase(sock);
                        broadcastToRoom(currentRoom, clients[sock].nickname + " 님이 방을 나갔습니다.\n", sock);
                    }
                    // 새 방에 입장
                    rooms[rid].clients.insert(sock);
                    clients[sock].roomId = rid;
                    sendLine(sock, "방에 입장했습니다.");
                    broadcastToRoom(rid, clients[sock].nickname + " 님이 입장했습니다.\n", sock);
                }
            } else if (cmd == "/list") {
                std::lock_guard<std::mutex> lock(mtx);
                int rid = clients[sock].roomId;
                if (rid == -1) {
                    sendToClient(sock, getRoomList());
                } else {
                    sendToClient(sock, getUserList(rid));
                }
            } else if (cmd == "/exit") {
                std::lock_guard<std::mutex> lock(mtx);
                int rid = clients[sock].roomId;
                if (rid != -1) {
                    rooms[rid].clients.erase(sock);
                    broadcastToRoom(rid, clients[sock].nickname + " 님이 방을 나갔습니다.\n", sock);
                    clients[sock].roomId = -1;
                    sendLine(sock, "방에서 나왔습니다.");
                }
            } else if (cmd == "/upload") {
                std::string fname;
                iss >> fname;
                if (fname.empty()) {
                    sendLine(sock, "사용법: /upload <파일명>");
                    continue;
                }
                sendLine(sock, "업로드 준비 완료. EOF 입력 시 종료됩니다.");
                std::ofstream ofs(STORAGE_DIR + fname, std::ios::binary);
                if (!ofs) {
                    sendLine(sock, "파일 열기 실패");
                    continue;
                }
                
                while (true) {
                    int len = recv(sock, buf, BUFFER_SIZE, 0);
                    if (len <= 0) break;
                    std::string chunk(buf, len);
                    if (chunk.find("EOF") != std::string::npos) break;
                    ofs.write(buf, len);
                }
                ofs.close();
                sendLine(sock, "파일 업로드 완료.");
            } else if (cmd == "/download") {
                std::string fname;
                iss >> fname;
                std::ifstream ifs(STORAGE_DIR + fname, std::ios::binary);
                if (!ifs) {
                    sendLine(sock, "파일이 존재하지 않습니다.");
                    continue;
                }
                sendLine(sock, "파일 다운로드 시작.");
                
                while (!ifs.eof()) {
                    ifs.read(buf, BUFFER_SIZE);
                    int read_count = ifs.gcount();
                    if (read_count > 0) {
                        send(sock, buf, read_count, 0);
                    }
                }
                sendLine(sock, "EOF"); // 다운로드 종료 알림
            } else if (cmd == "/files") {
                auto f = listFiles();
                std::ostringstream oss;
                oss << "[파일 목록]\n";
                for (auto &f2 : f) oss << f2 << "\n";
                sendToClient(sock, oss.str());
            } else if (cmd == "/comment") {
                std::string fname, msg;
                iss >> fname;
                std::getline(iss >> std::ws, msg);
                std::ofstream cf(STORAGE_DIR + fname + ".comments", std::ios::app);
                cf << clients[sock].nickname << ": " << msg << "\n";
                cf.close();
                sendLine(sock, "댓글이 추가되었습니다.");
            } else if (cmd == "/showc") {
                std::string fname;
                iss >> fname;
                std::ifstream cf(STORAGE_DIR + fname + ".comments");
                std::ostringstream oss;
                oss << "[댓글 목록]\n";
                std::string line;
                while (std::getline(cf, line)) oss << line << "\n";
                sendToClient(sock, oss.str());
            } else {
                int rid = clients[sock].roomId;
                if (rid == -1) {
                    sendLine(sock, "채팅을 하려면 방에 입장하세요.");
                } else {
                    broadcastToRoom(rid, "[" + clients[sock].nickname + "]: " + std::string(buf), sock);
                }
            }
            cleanupEmptyRooms();
        }

        cleanupClient(sock);
    }
};

int main() {
    ChatServer server;
    server.run();
    return 0;
}