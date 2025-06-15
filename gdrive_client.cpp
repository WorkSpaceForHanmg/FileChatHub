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

#define PORT 9001
#define BUFFER_SIZE 8192

int sock = -1;
std::atomic<bool> running(true);

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
    "/msg <상대유저> <메시지>\n"
    "/quit\n";
}

void send_cmd(const std::string& cmd) {
    send(sock, cmd.c_str(), cmd.size(), 0);
}

void recv_thread() {
    char buf[BUFFER_SIZE];
    while (running) {
        memset(buf, 0, sizeof(buf));
        int len = recv(sock, buf, sizeof(buf)-1, MSG_DONTWAIT);
        if (len > 0) {
            buf[len] = 0;
            std::string resp(buf, len);
            if (resp.substr(0, 4) == "MSG|") {
                std::cout << "\n[받은메시지] " << resp.substr(4);
                std::cout << "> " << std::flush;
            }
        }
        usleep(100 * 1000);
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

int main() {
    std::string serv_ip;
    std::cout << "서버 IP 입력: ";
    std::cin >> serv_ip;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "소켓 생성 실패\n";
        return 1;
    }
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, serv_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "서버 연결 실패\n";
        return 2;
    }

    std::string welcome = recv_resp();
    std::cout << welcome;

    bool logged_in = false;
    while (!logged_in) {
        std::string mode, id, pw;
        std::cout << "1) 로그인    2) 회원가입   번호 입력: ";
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
        if (resp.find("OK|") == 0) {
            logged_in = true;
        }
    }

    usage();

    std::thread th(recv_thread);

    while (true) {
        std::cout << "> ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "/help") { usage(); continue; }
        if (line == "/quit") {
            running = false;
            send_cmd("/quit|\n");
            break;
        }
        std::istringstream iss(line);
        std::string cmd, arg1;
        iss >> cmd >> arg1;

        if (cmd == "/msg") {
            std::string msg;
            std::getline(iss, msg);
            if (!msg.empty() && msg[0] == ' ') msg = msg.substr(1);
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|" << msg << "|\n";
            send_cmd(oss.str());
            std::string resp = recv_resp();
            std::cout << resp;
        }
        else if (cmd == "/ls") {
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/mkdir") {
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/rm") {
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/mv") {
            std::string arg2;
            iss >> arg2;
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/share") {
            std::string arg2;
            iss >> arg2;
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/unshare") {
            std::string arg2;
            iss >> arg2;
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|" << arg2 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/sharedwithme") {
            std::ostringstream oss;
            oss << cmd << "||\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/search") {
            std::ostringstream oss;
            oss << cmd << "|" << arg1 << "|\n";
            send_cmd(oss.str());
            std::cout << recv_resp();
        }
        else if (cmd == "/upload") {
            std::string arg2;
            iss >> arg2;
            std::string local = arg1;
            std::string remote = arg2.empty() ? arg1 : arg2;
            std::ifstream ifs(local, std::ios::binary | std::ios::ate);
            if (!ifs) {
                std::cout << "파일 열기 실패: " << local << std::endl;
                continue;
            }
            int filesize = ifs.tellg();
            ifs.seekg(0);
            std::ostringstream oss;
            oss << cmd << "|" << remote << "|" << filesize << "|\n";
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
            std::string arg2;
            iss >> arg2;
            std::string remote = arg1;
            std::string local = arg2.empty() ? arg1 : arg2;
            std::ostringstream oss;
            oss << cmd << "|" << remote << "|\n";
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
        else {
            std::cout << "알 수 없는 명령. /help로 도움말 확인\n";
        }
    }
    th.join();
    close(sock);
    return 0;
}