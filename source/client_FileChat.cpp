#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstring>
#include <algorithm>

// ---- Constants ----
constexpr int PORT = 9001;           
constexpr int BUFFER_SIZE = 8192;    

// ---- Global Variables ----
int sock = -1;                       
std::string current_dir;             
std::atomic<bool> running(true);     

// ---- Function Declarations ----
void print_welcome();
void usage();
void print_command_guide();
void send_cmd(const std::string& cmd);
std::string recv_resp();
std::string join_path(const std::string& dir, const std::string& path);
std::string normalize_path(const std::string& path);

// ---- 채팅/알림 수신 스레드 ----
void recv_thread() {
    char buf[BUFFER_SIZE];
    std::string recv_buffer;
    while (running) {
        int len = recv(sock, buf, sizeof(buf)-1, MSG_DONTWAIT);
        if (len > 0) {
            recv_buffer.append(buf, len);
            size_t pos;
            while ((pos = recv_buffer.find('\n')) != std::string::npos) {
                std::string line = recv_buffer.substr(0, pos);
                recv_buffer.erase(0, pos + 1);
                if (line.substr(0, 4) == "MSG|") {
                    std::cout << "\n[받은메시지] " << line.substr(4) << std::endl;
                    std::cout << (current_dir.empty() ? "~" : current_dir) << " > " << std::flush;
                }
            }
        }
        usleep(100 * 1000); // 100ms 대기 (CPU 점유 최소화)
    }
}

// ---- Helper Functions ----

// 사용을 시작할 때 보여주는 환영 메시지
void print_welcome() {
    std::cout << "========================================\n";
    std::cout << "   FileChatHub 클라이언트에 오신 것을 환영합니다!\n";
    std::cout << "   - 이 프로그램은 서버와 파일/폴더를 주고받고,\n";
    std::cout << "     다른 사용자와 채팅 및 파일 공유가 가능합니다.\n";
    std::cout << "========================================\n";
    std::cout << std::endl;
}

// 명령어 전체 설명(더 친절하게)
void usage() {
    std::cout << "----------------------------------------\n";
    std::cout << "[ FileChatHub 사용법 및 명령어 안내 ]\n";
    std::cout << "서버에 파일을 업로드/다운로드하거나, 폴더/파일 관리\n";
    std::cout << "그리고 실시간 채팅/공유 기능을 사용할 수 있습니다.\n";
    std::cout << "명령어가 궁금하면 /help 또는 /? 를 입력하세요.\n";
    std::cout << "명령어는 반드시 앞에 '/'를 붙여 입력하세요.\n";
    std::cout << "----------------------------------------\n";
    std::cout <<
        "/ls [폴더]         - 현재 또는 지정한 폴더 목록 보기\n"
        "/mkdir <폴더>      - 새 폴더 생성\n"
        "/upload <로컬파일> [서버경로]   - 파일 업로드\n"
        "/download <서버경로> [로컬파일] - 파일 다운로드\n"
        "/rm <서버경로>     - 파일/폴더 삭제\n"
        "/mv <원경로> <새경로> - 파일/폴더 이름 변경/이동\n"
        "/share <경로> <상대유저>    - 파일/폴더 공유\n"
        "/unshare <경로> <상대유저>  - 공유 해제\n"
        "/sharedwithme      - 나에게 공유된 목록 보기\n"
        "/search <키워드>   - 파일/폴더명 키워드 검색\n"
        "/cd <폴더명>       - 폴더 이동\n"
        "/pwd               - 현재 경로 표시\n"
        "/msg <상대유저> <메시지> - 실시간 메시지 보내기\n"
        "/who               - 현재 접속 중인 유저 목록\n"
        "/quit              - 프로그램 종료\n"
        "/help, /?          - 이 도움말 다시 보기\n";
    std::cout << "----------------------------------------\n";
}

// 명령 입력 예시 안내
void print_command_guide() {
    std::cout << "\n[명령어 예시]\n";
    std::cout << "  /ls           (현재 폴더 목록)\n";
    std::cout << "  /ls myfolder  (myfolder 폴더 목록)\n";
    std::cout << "  /cd myfolder  (myfolder로 이동)\n";
    std::cout << "  /upload sample.txt      (sample.txt 파일 업로드)\n";
    std::cout << "  /download test.txt    (test.txt 파일 다운로드)\n";
    std::cout << "  /msg alice 안녕하세요!  (alice에게 메시지 보내기)\n";
    std::cout << "  /who          (현재 접속자 목록)\n";
    std::cout << "----------------------------------------\n";
}

void send_cmd(const std::string& cmd) {
    ssize_t sent = send(sock, cmd.c_str(), cmd.size(), 0);
    if (sent < 0) {
        std::cerr << "[ERROR] 서버로 명령 전송 실패: " << strerror(errno) << "\n";
    }
}

std::string recv_resp() {
    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    int len = recv(sock, buf, sizeof(buf)-1, 0);
    if (len <= 0) return "";
    buf[len] = 0;
    return std::string(buf, len);
}

std::string join_path(const std::string& dir, const std::string& path) {
    if (path.empty()) return dir;
    if (path[0] == '/') return path; // 절대경로
    if (dir.empty()) return path;
    return dir + "/" + path;
}

std::string normalize_path(const std::string& path) {
    std::vector<std::string> stack;
    std::istringstream iss(path);
    std::string token;
    while (getline(iss, token, '/')) {
        if (token.empty() || token == ".") continue;
        if (token == "..") {
            if (!stack.empty()) stack.pop_back();
        } else {
            stack.push_back(token);
        }
    }
    std::string result;
    for (const auto& t : stack) {
        if (!result.empty()) result += "/";
        result += t;
    }
    return result;
}

// ---- Main ----
int main() {
    print_welcome();
    std::string serv_ip;
    std::cout << "[서버에 접속하려면] 서버 IP를 입력하세요 (예: 127.0.0.1): ";
    std::cin >> serv_ip;

    // 소켓 생성
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "소켓 생성 실패: " << strerror(errno) << "\n";
        return 1;
    }
    // 서버 주소 설정
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, serv_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "IP 변환 실패 (입력값 확인)\n";
        return 1;
    }

    // 서버 연결 시도
    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "서버 연결 실패: " << strerror(errno) << "\n";
        return 2;
    }

    // 서버 환영 메시지 수신
    std::string welcome = recv_resp();
    std::cout << welcome;

    // ---- 로그인/회원가입 루프 ----
    bool logged_in = false;
    while (!logged_in) {
        std::string mode, id, pw;
        std::cout << "[1:로그인] [2:회원가입] 중 번호 입력: ";
        std::getline(std::cin >> std::ws, mode);
        std::cout << "아이디 입력: ";
        std::getline(std::cin, id);
        std::cout << "비밀번호 입력: ";
        std::getline(std::cin, pw);
        std::ostringstream oss;
        oss << mode << "|" << id << "|" << pw << "|\n";
        send_cmd(oss.str());
        std::string resp = recv_resp();
        std::cout << resp;
        if (resp.find("OK|") == 0) logged_in = true;
    }

    usage(); // 명령어 도움말 출력
    print_command_guide();

    // 채팅/알림 수신 스레드 시작
    std::thread th(recv_thread);

    // ---- 메인 명령 입력 루프 ----
    while (true) {
        std::cout << (current_dir.empty() ? "~" : current_dir) << " > ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "/help" || line == "/?") { usage(); print_command_guide(); continue; }
        if (line == "/quit") { send_cmd("/quit|\n"); break; }

        std::istringstream iss(line);
        std::string cmd, arg1, arg2;
        iss >> cmd >> arg1 >> arg2;

        // 명령어 파싱 및 처리
        if (cmd == "/pwd") {
            std::cout << "/" << (current_dir.empty() ? "" : current_dir) << std::endl;
        }
        else if (cmd == "/cd") {
            if (arg1.empty()) {
                std::cout << "[안내] 이동할 폴더명을 입력하세요.\n";
                continue;
            }
            std::string new_dir = join_path(current_dir, arg1);
            new_dir = normalize_path(new_dir);
            std::ostringstream oss;
            oss << "/ls|" << new_dir << "|\n";
            send_cmd(oss.str());
            std::string resp = recv_resp();
            if (resp.find("OK|") == 0 && resp.find("(폴더 없음)") == std::string::npos) {
                current_dir = new_dir;
            } else {
                std::cout << "[안내] 폴더가 존재하지 않습니다.\n";
            }
        }
        else if (cmd == "/ls") {
            std::string path = arg1.empty() ? current_dir : join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/ls|" << path << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/mkdir") {
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/mkdir|" << path << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/rm") {
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/rm|" << path << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/mv") {
            std::string from = join_path(current_dir, arg1);
            from = normalize_path(from);
            std::string to = join_path(current_dir, arg2);
            to = normalize_path(to);
            std::ostringstream oss;
            oss << "/mv|" << from << "|" << to << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/share") {
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/share|" << path << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/unshare") {
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/unshare|" << path << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/sharedwithme") {
            send_cmd("/sharedwithme||\n");
            std::cout << recv_resp();
        }
        else if (cmd == "/search") {
            std::ostringstream oss;
            oss << "/search|" << arg1 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/upload") {
            if (arg1.empty()) {
                std::cout << "[안내] 업로드할 파일명을 입력하세요.\n";
                continue;
            }
            std::string remote = join_path(current_dir, arg2.empty() ? arg1 : arg2);
            remote = normalize_path(remote);
            std::ifstream ifs(arg1, std::ios::binary | std::ios::ate);
            if (!ifs) {
                std::cout << "파일 열기 실패: " << arg1 << std::endl;
                continue;
            }
            int filesize = ifs.tellg();
            ifs.seekg(0);
            std::ostringstream oss;
            oss << "/upload|" << remote << "|" << filesize << "|\n";
            send_cmd(oss.str());
            char buf[BUFFER_SIZE];
            int sent = 0;
            std::cout << "[안내] 업로드 시작 (" << filesize << " 바이트)..." << std::endl;
            while (sent < filesize) {
                int tosend = std::min(BUFFER_SIZE, filesize - sent);
                ifs.read(buf, tosend);
                int l = send(sock, buf, tosend, 0);
                if (l <= 0) break;
                sent += l;
                int percent = (int)(100.0 * sent / filesize);
                if (percent % 10 == 0)
                    std::cout << "\r" << percent << "% 완료" << std::flush;
            }
            ifs.close();
            std::cout << "\r[안내] 업로드 완료           " << std::endl;
            // === 전송 바이트 검증 추가 ===
            if (sent != filesize) {
                std::cout << "\n[경고] 파일 전송 바이트 불일치 (전송:" << sent << ", 기대:" << filesize << ")\n";
            }
            std::cout << recv_resp();
        }
        else if (cmd == "/download") {
            if (arg1.empty()) {
                std::cout << "[안내] 다운로드할 파일명을 입력하세요.\n";
                continue;
            }
            std::string remote = join_path(current_dir, arg1);
            remote = normalize_path(remote);
            std::string local = arg2.empty() ? arg1 : arg2;
            std::ostringstream oss;
            oss << "/download|" << remote << "|\n";
            send_cmd(oss.str());
            std::string resp = recv_resp();
            if (resp.substr(0, 3) != "OK|") {
                std::cout << resp;
                continue;
            }
            size_t p1 = resp.find('|', 3);
            int filesize = std::stoi(resp.substr(3, p1 - 3));
            size_t file_start = p1 + 1;
            std::ofstream ofs(local, std::ios::binary);
            int recvd = 0;
            // 응답 메시지에 파일 일부가 포함되어 있는 경우 처리
            if (resp.size() > file_start) {
                int remain = resp.size() - file_start;
                ofs.write(resp.data() + file_start, remain);
                recvd += remain;
            }
            char buf[BUFFER_SIZE];
            while (recvd < filesize) {
                int toread = std::min(BUFFER_SIZE, filesize - recvd);
                int l = recv(sock, buf, toread, 0);
                if (l <= 0) break;
                ofs.write(buf, l);
                recvd += l;
            }
            ofs.close();
            if (recvd == filesize)
                std::cout << "\r[안내] 다운로드 완료: " << local << "           " << std::endl;
            else {
                std::cout << "\r[경고] 다운로드 실패: " << local << "           " << std::endl;
                std::cout << "\n[경고] 파일 수신 바이트 불일치 (수신:" << recvd << ", 기대:" << filesize << ")\n";
            }
        }
        else if (cmd == "/msg") {
            if (arg1.empty()) {
                std::cout << "[안내] 메시지를 받을 유저명을 입력하세요.\n";
                continue;
            }
            std::string msg;
            std::getline(iss, msg);
            if (!msg.empty() && msg[0] == ' ') msg = msg.substr(1);
            std::ostringstream oss;
            oss << "/msg|" << arg1 << "|" << arg2;
            if (!msg.empty()) oss << " " << msg;
            oss << "\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/who") {
            send_cmd("/who||\n");
            std::cout << recv_resp();
        }
        else {
            std::cout << "[안내] 알 수 없는 명령입니다. /help 또는 /?로 도움말을 확인하세요.\n";
        }
    }

    // 종료 처리: 수신 스레드 종료 대기
    running = false;
    th.join();
    close(sock);
    std::cout << "\n[안내] 프로그램을 종료합니다. 감사합니다!\n";
    return 0;
}