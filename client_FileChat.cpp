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

// ---- Constants ----
constexpr int PORT = 9001;           // 서버 포트 번호
constexpr int BUFFER_SIZE = 8192;    // 네트워크 송수신 버퍼 크기

// ---- Global Variables ----
int sock = -1;                       // 서버 소켓 파일 디스크립터
std::string current_dir;             // 현재 위치(클라이언트 기준, ""면 루트)
std::atomic<bool> running(true);     // 수신 스레드 동작 상태 제어

// ---- Function Declarations ----
void usage();
// 서버에 명령어 문자열 전송
void send_cmd(const std::string& cmd);
// 서버로부터 응답 수신(한 번)
std::string recv_resp();
// 경로 합치기(현재경로 + 상대경로)
std::string join_path(const std::string& dir, const std::string& path);
// 경로 정규화 (.., . 등 처리)
std::string normalize_path(const std::string& path);

// ---- 채팅/알림 수신 스레드 ----
// 서버에서 오는 메시지(특히 /msg 등) 비동기 출력
void recv_thread() {
    char buf[BUFFER_SIZE];
    std::string recv_buffer;
    while (running) {
        // MSG_DONTWAIT: 논블로킹 수신
        int len = recv(sock, buf, sizeof(buf)-1, MSG_DONTWAIT);
        if (len > 0) {
            recv_buffer.append(buf, len);
            size_t pos;
            // 여러 메시지가 한 번에 올 수도 있으므로 한 줄씩 분리
            while ((pos = recv_buffer.find('\n')) != std::string::npos) {
                std::string line = recv_buffer.substr(0, pos);
                recv_buffer.erase(0, pos + 1);
                // 서버가 MSG|로 시작하는 메시지 보낼 때
                if (line.substr(0, 4) == "MSG|") {
                    std::cout << "\n[받은메시지] " << line.substr(4) << std::endl;
                    // 현재 입력 프롬프트 재출력
                    std::cout << (current_dir.empty() ? "~" : current_dir) << " > " << std::flush;
                }
            }
        }
        usleep(100 * 1000); // 100ms 대기 (CPU 점유 최소화)
    }
}

// ---- Helper Functions ----

// 명령어 요약 출력
void usage() {
    std::cout <<
        "/ls [폴더]\n"
        "/mkdir <폴더>\n"
        "/upload <로컬파일> [서버경로]\n"
        "/download <서버경로> [로컬파일]\n"
        "/rm <서버경로>\n"
        "/mv <원경로> <새경로>\n"
        "/share <경로> <상대유저>\n"
        "/unshare <경로> <상대유저>\n"
        "/sharedwithme\n"
        "/search <키워드>\n"
        "/cd <폴더명>\n"
        "/pwd\n"
        "/msg <상대유저> <메시지>\n"
        "/who\n"
        "/quit\n";
}

// 서버에 명령어 문자열 전송
void send_cmd(const std::string& cmd) {
    send(sock, cmd.c_str(), cmd.size(), 0);
}

// 서버 응답 1회 수신 (주로 OK|/ERR| 응답)
std::string recv_resp() {
    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    int len = recv(sock, buf, sizeof(buf)-1, 0);
    if (len <= 0) return "";
    buf[len] = 0;
    return std::string(buf, len);
}

// 현재경로 + 상대경로 -> 합쳐진 경로 반환
std::string join_path(const std::string& dir, const std::string& path) {
    if (path.empty()) return dir;
    if (path[0] == '/') return path; // 절대경로
    if (dir.empty()) return path;
    return dir + "/" + path;
}

// /a/b/../c/./d -> a/c/d 변환 (상대경로 정규화)
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
    std::string serv_ip;
    std::cout << "서버 IP 입력: ";
    std::cin >> serv_ip;

    // 소켓 생성
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "소켓 생성 실패\n";
        return 1;
    }
    // 서버 주소 설정
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, serv_ip.c_str(), &serv_addr.sin_addr);

    // 서버 연결 시도
    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "서버 연결 실패\n";
        return 2;
    }

    // 서버 환영 메시지 수신
    std::string welcome = recv_resp();
    std::cout << welcome;

    // ---- 로그인/회원가입 루프 ----
    bool logged_in = false;
    while (!logged_in) {
        std::string mode, id, pw;
        std::cout << "1) 로그인    2) 회원가입   번호 입력: ";
        std::getline(std::cin >> std::ws, mode);
        std::cout << "아이디 입력: ";
        std::getline(std::cin, id);
        std::cout << "비밀번호 입력: ";
        std::getline(std::cin, pw);
        // 명령 문자열 구성 (파이프 구분자)
        std::ostringstream oss;
        oss << mode << "|" << id << "|" << pw << "|\n"; // 끝에 | 붙임
        send_cmd(oss.str());
        std::string resp = recv_resp();
        std::cout << resp;
        if (resp.find("OK|") == 0) logged_in = true;
    }

    usage(); // 명령어 도움말 출력

    // 채팅/알림 수신 스레드 시작 (비동기 메시지 출력)
    std::thread th(recv_thread);

    // ---- 메인 명령 입력 루프 ----
    while (true) {
        std::cout << (current_dir.empty() ? "~" : current_dir) << " > ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "/help") { usage(); continue; }
        if (line == "/quit") { send_cmd("/quit|\n"); break; }

        std::istringstream iss(line);
        std::string cmd, arg1, arg2;
        iss >> cmd >> arg1 >> arg2;

        // ---- 명령어 파싱 및 처리 ----
        if (cmd == "/pwd") {
            // 현재 경로 출력
            std::cout << "/" << (current_dir.empty() ? "" : current_dir) << std::endl;
        }
        else if (cmd == "/cd") {
            // 폴더 이동 (존재 확인은 서버에 /ls로)
            if (arg1.empty()) {
                std::cout << "이동할 폴더명을 입력하세요.\n";
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
                std::cout << "폴더가 존재하지 않습니다.\n";
            }
        }
        else if (cmd == "/ls") {
            // 현재/지정 폴더 내 파일/폴더 목록
            std::string path = arg1.empty() ? current_dir : join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/ls|" << path << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/mkdir") {
            // 폴더 생성
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/mkdir|" << path << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/rm") {
            // 파일/폴더 삭제
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/rm|" << path << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/mv") {
            // 파일/폴더 이동(이름변경)
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
            // 파일/폴더 공유
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/share|" << path << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/unshare") {
            // 공유 해제
            std::string path = join_path(current_dir, arg1);
            path = normalize_path(path);
            std::ostringstream oss;
            oss << "/unshare|" << path << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/sharedwithme") {
            // 내가 받은 공유 목록
            send_cmd("/sharedwithme||\n");
            std::cout << recv_resp();
        }
        else if (cmd == "/search") {
            // 파일/폴더/공유 검색
            std::ostringstream oss;
            oss << "/search|" << arg1 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/upload") {
            // 파일 업로드 (로컬 파일 -> 서버)
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
            std::cout << "업로드 시작 (" << filesize << " 바이트)..." << std::endl;
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
            std::cout << "\r업로드 완료           " << std::endl;
            std::cout << recv_resp();
        }
        else if (cmd == "/download") {
            // 파일 다운로드 (서버 -> 로컬)
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
            // 응답 메시지에 파일 일부가 포함된 경우 처리
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
                std::cout << "\r다운로드 완료: " << local << "           " << std::endl;
            else
                std::cout << "\r다운로드 실패: " << local << "           " << std::endl;
        }
        else if (cmd == "/msg") {
            // 1:1 메시지 보내기
            std::string msg;
            std::getline(iss, msg); // arg2 이후 전체 메시지 추출 (공백 포함)
            if (!msg.empty() && msg[0] == ' ') msg = msg.substr(1);
            std::ostringstream oss;
            oss << "/msg|" << arg1 << "|" << arg2;
            if (!msg.empty()) oss << " " << msg;
            oss << "\n"; // 끝에 | 없이 바로 개행!
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/who") {
            // 온라인 유저 목록
            send_cmd("/who||\n");
            std::cout << recv_resp();
        }
        else {
            // 알 수 없는 명령어
            std::cout << "알 수 없는 명령. /help로 도움말 확인\n";
        }
    }

    // 종료 처리: 수신 스레드 종료 대기
    running = false;
    th.join();
    close(sock);
    return 0;
}