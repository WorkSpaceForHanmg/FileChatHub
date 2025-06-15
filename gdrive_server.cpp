#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>

// ---- Constants ----
constexpr int PORT = 9001;
constexpr int BUFFER_SIZE = 8192;
const std::string DATA_ROOT = "server_data/users/";
const std::string USER_DB_FILE = DATA_ROOT + ".userdb";
const std::string SHARE_MAP_FILE = "server_data/sharemap.txt";

// ---- Mutexes ----
std::mutex user_mutex, share_mutex, conn_mutex;

// ---- Global State ----
std::map<std::string, std::string> user_db;
std::multimap<std::string, std::pair<std::string, std::string>> share_map;
std::map<std::string, int> user_conn; // username -> client_sock

// ---- Utility Functions ----
namespace util {
    void ensure_dir(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            mkdir(path.c_str(), 0755);
        }
    }

    bool ensure_user_dir(const std::string& user) {
        std::string path = DATA_ROOT + user;
        ensure_dir(DATA_ROOT);
        ensure_dir(path);
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    void load_user_db() {
        user_db.clear();
        std::ifstream ifs(USER_DB_FILE);
        std::string line;
        while (std::getline(ifs, line)) {
            std::istringstream iss(line);
            std::string id, pw;
            if (iss >> id >> pw) user_db[id] = pw;
        }
    }
    void save_user_db() {
        std::ofstream ofs(USER_DB_FILE);
        for (const auto& kv : user_db) ofs << kv.first << " " << kv.second << "\n";
    }

    void load_share_map() {
        share_map.clear();
        std::ifstream ifs(SHARE_MAP_FILE);
        std::string line;
        while (std::getline(ifs, line)) {
            std::istringstream iss(line);
            std::string to_user, from_user, path;
            if (iss >> to_user >> from_user >> path)
                share_map.insert({to_user, {from_user, path}});
        }
    }
    void save_share_map() {
        std::ofstream ofs(SHARE_MAP_FILE);
        for (const auto& kv : share_map)
            ofs << kv.first << " " << kv.second.first << " " << kv.second.second << "\n";
    }

    std::string list_dir(const std::string& path) {
        std::ostringstream oss;
        DIR* dp = opendir(path.c_str());
        if (!dp) return "(폴더 없음)\n";
        struct dirent* ep;
        while ((ep = readdir(dp)) != nullptr) {
            if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) continue;
            std::string fullpath = path + "/" + ep->d_name;
            struct stat st;
            stat(fullpath.c_str(), &st);
            oss << (S_ISDIR(st.st_mode) ? "[DIR] " : "[FILE] ") << ep->d_name << "\n";
        }
        closedir(dp);
        return oss.str();
    }

    bool make_dir(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) return false;
        return mkdir(path.c_str(), 0755) == 0;
    }

    bool remove_path(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return false;
        if (S_ISDIR(st.st_mode)) {
            DIR* dir = opendir(path.c_str());
            if (!dir) return false;
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
                std::string child = path + "/" + entry->d_name;
                remove_path(child);
            }
            closedir(dir);
            return rmdir(path.c_str()) == 0;
        } else {
            return remove(path.c_str()) == 0;
        }
    }

    bool move_path(const std::string& from, const std::string& to) {
        size_t slash = to.find_last_of('/');
        if (slash != std::string::npos) {
            ensure_dir(to.substr(0, slash));
        }
        return rename(from.c_str(), to.c_str()) == 0;
    }

    void search_recursive(
        const std::string& base,
        const std::string& path,
        const std::string& keyword,
        std::vector<std::string>& results
    ) {
        std::string fullpath = base + (path.empty() ? "" : "/" + path);
        DIR* dir = opendir(fullpath.c_str());
        if (!dir) return;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            std::string rel = path.empty() ? entry->d_name : path + "/" + entry->d_name;
            if (strstr(entry->d_name, keyword.c_str()))
                results.push_back(rel);
            std::string child_full = fullpath + "/" + entry->d_name;
            struct stat st;
            if (stat(child_full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                search_recursive(base, rel, keyword, results);
        }
        closedir(dir);
    }
} // namespace util

// ---- Client Handler ----

void send_response(int client_sock, const std::string& msg) {
    send(client_sock, msg.c_str(), msg.size(), 0);
}

void handle_msg(int client_sock, const std::string& sender, const std::string& target, const std::string& message) {
    std::lock_guard<std::mutex> lock(conn_mutex);
    auto it = user_conn.find(target);
    if (it != user_conn.end()) {
        std::ostringstream oss;
        oss << "MSG|[" << sender << "] " << message << "\n";
        send_response(it->second, oss.str());
        send_response(client_sock, "OK|메시지 전송 완료\n");
    } else {
        send_response(client_sock, "ERR|상대방이 온라인이 아님\n");
    }
}

// ---- 로그인/회원가입 & 중복 로그인 방지 ----
bool try_login(const std::string& id, const std::string& pw, std::string& response) {
    std::lock_guard<std::mutex> lock(user_mutex);
    util::load_user_db();
    if (!user_db.count(id) || user_db[id] != pw) {
        response = "ERR|로그인 실패. 다시 시도\n";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock2(conn_mutex);
        if (user_conn.count(id)) {
            response = "ERR|이미 로그인 중인 계정입니다\n";
            return false;
        }
    }
    response = "OK|로그인 성공\n";
    return true;
}

bool try_signup(const std::string& id, const std::string& pw, std::string& response) {
    std::lock_guard<std::mutex> lock(user_mutex);
    util::load_user_db();
    if (user_db.count(id)) {
        response = "ERR|이미 존재하는 아이디입니다. 다시 시도\n";
        return false;
    }
    user_db[id] = pw;
    util::save_user_db();
    response = "OK|회원가입 및 로그인 성공\n";
    return true;
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    std::string username;
    bool logged_in = false;

    send_response(client_sock, "OK|로그인 또는 회원가입 선택: (1) 로그인 (2) 회원가입 입력\n");

    // --- 로그인/회원가입 루프 ---
    while (!logged_in) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(client_sock, buffer, sizeof(buffer)-1, 0);
        if (len <= 0) return;
        buffer[len] = 0;
        std::istringstream iss(buffer);
        std::string mode, id, pw;
        getline(iss, mode, '|');
        getline(iss, id, '|');
        getline(iss, pw, '|');

        std::string response;
        if (mode == "1") { // 로그인
            if (try_login(id, pw, response)) {
                username = id;
                logged_in = true;
                util::ensure_user_dir(username);
            }
            send_response(client_sock, response);
        }
        else if (mode == "2") { // 회원가입
            if (try_signup(id, pw, response)) {
                username = id;
                logged_in = true;
                util::ensure_user_dir(username);
            }
            send_response(client_sock, response);
        }
        else {
            send_response(client_sock, "ERR|1 또는 2만 입력 가능\n");
        }
    }

    {
        std::lock_guard<std::mutex> lock(conn_mutex);
        user_conn[username] = client_sock;
    }
    {
        std::lock_guard<std::mutex> lock(share_mutex);
        util::load_share_map();
    }

    // --- 명령어 루프 ---
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(client_sock, buffer, sizeof(buffer)-1, 0);
        if (len <= 0) break;
        buffer[len] = 0;
        std::istringstream iss(buffer);
        std::string cmd, arg1, arg2;
        getline(iss, cmd, '|');
        getline(iss, arg1, '|');
        getline(iss, arg2, '|');

        if (cmd == "/msg") {
            std::string message = arg2;
            // 메시지에 |가 포함될 경우 뒷부분도 붙여줌
            std::string extra;
            getline(iss, extra, '|');
            if (!extra.empty()) message += "|" + extra;
            // 메시지 끝에 |가 붙어 오면 제거 (방어적)
            if (!message.empty() && message.back() == '|') message.pop_back();
            handle_msg(client_sock, username, arg1, message);
        }
        else if (cmd == "/who") {
            std::ostringstream oss;
            oss << "OK|";
            {
                std::lock_guard<std::mutex> lock(conn_mutex);
                for (const auto& kv : user_conn)
                    oss << kv.first << " ";
            }
            oss << "\n";
            send_response(client_sock, oss.str());
        }
        else if (cmd == "/share") {
            bool user_ok = false;
            {
                std::lock_guard<std::mutex> ulock(user_mutex);
                util::load_user_db();
                user_ok = user_db.count(arg2) > 0;
            }
            if (!user_ok) {
                send_response(client_sock, "ERR|상대 유저 없음\n");
                continue;
            }
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                util::load_share_map();
                bool already = false;
                for (auto it = share_map.lower_bound(arg2); it != share_map.upper_bound(arg2); ++it) {
                    if (it->second.first == username && it->second.second == arg1) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    send_response(client_sock, "ERR|이미 공유한 항목입니다\n");
                } else {
                    share_map.insert({arg2, {username, arg1}});
                    util::save_share_map();
                    send_response(client_sock, "OK|공유 성공\n");
                }
            }
        }
        else if (cmd == "/unshare") {
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                util::load_share_map();
                bool found = false;
                for (auto it = share_map.lower_bound(arg2); it != share_map.upper_bound(arg2); ) {
                    if (it->second.first == username && it->second.second == arg1) {
                        it = share_map.erase(it);
                        found = true;
                    } else {
                        ++it;
                    }
                }
                if (found) {
                    util::save_share_map();
                    send_response(client_sock, "OK|공유 해제 성공\n");
                } else {
                    send_response(client_sock, "ERR|공유 항목 없음\n");
                }
            }
        }
        else if (cmd == "/sharedwithme") {
            std::ostringstream oss;
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                util::load_share_map();
                for (auto it = share_map.lower_bound(username); it != share_map.upper_bound(username); ++it) {
                    oss << "[FROM " << it->second.first << "] " << it->second.second << "\n";
                }
            }
            std::string result = oss.str();
            if (result.empty()) result = "(공유받은 항목 없음)\n";
            send_response(client_sock, "OK|" + result);
        }
        else if (cmd == "/ls") {
            std::string dir = DATA_ROOT + username + (arg1.empty() ? "" : "/" + arg1);
            std::string result = util::list_dir(dir);
            send_response(client_sock, "OK|" + result);
        }
        else if (cmd == "/mkdir") {
            std::string dir = DATA_ROOT + username + "/" + arg1;
            if (util::make_dir(dir))
                send_response(client_sock, "OK|폴더 생성 성공\n");
            else
                send_response(client_sock, "ERR|폴더 생성 실패\n");
        }
        else if (cmd == "/rm") {
            std::string path = DATA_ROOT + username + "/" + arg1;
            if (util::remove_path(path))
                send_response(client_sock, "OK|삭제 성공\n");
            else
                send_response(client_sock, "ERR|삭제 실패\n");
        }
        else if (cmd == "/mv") {
            std::string from = DATA_ROOT + username + "/" + arg1;
            std::string to = DATA_ROOT + username + "/" + arg2;
            if (util::move_path(from, to))
                send_response(client_sock, "OK|이동/이름변경 성공\n");
            else
                send_response(client_sock, "ERR|이동/이름변경 실패\n");
        }
        else if (cmd == "/upload") {
            std::string fpath = DATA_ROOT + username + "/" + arg1;
            int filesize = stoi(arg2);
            size_t slash = fpath.find_last_of('/');
            if (slash != std::string::npos)
                util::ensure_dir(fpath.substr(0, slash));
            std::ofstream ofs(fpath, std::ios::binary);
            int received = 0;
            while (received < filesize) {
                int to_read = std::min(BUFFER_SIZE, filesize - received);
                int l = recv(client_sock, buffer, to_read, 0);
                if (l <= 0) break;
                ofs.write(buffer, l);
                received += l;
            }
            ofs.close();
            if (received == filesize)
                send_response(client_sock, "OK|업로드 성공\n");
            else
                send_response(client_sock, "ERR|업로드 실패\n");
        }
        else if (cmd == "/download") {
            std::string fpath = DATA_ROOT + username + "/" + arg1;
            struct stat st;
            bool found = false, shared = false;
            std::string owner;
            if (stat(fpath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                found = true;
            } else {
                std::lock_guard<std::mutex> slock(share_mutex);
                util::load_share_map();
                for (auto it = share_map.lower_bound(username); it != share_map.upper_bound(username); ++it) {
                    if (it->second.second == arg1) {
                        owner = it->second.first;
                        found = true;
                        shared = true;
                        break;
                    }
                }
                if (found) {
                    fpath = DATA_ROOT + owner + "/" + arg1;
                    if (stat(fpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                        send_response(client_sock, "ERR|파일 없음\n");
                        continue;
                    }
                }
            }
            if (!found) {
                send_response(client_sock, "ERR|파일 없음\n");
                continue;
            }
            int filesize = st.st_size;
            std::ifstream ifs(fpath, std::ios::binary);
            if (!ifs) {
                send_response(client_sock, "ERR|파일 열기 실패\n");
                continue;
            }
            std::ostringstream oss;
            oss << "OK|" << filesize << "|";
            send_response(client_sock, oss.str());
            int sent = 0;
            while (sent < filesize) {
                int tosend = std::min(BUFFER_SIZE, filesize - sent);
                ifs.read(buffer, tosend);
                int l = send(client_sock, buffer, tosend, 0);
                if (l <= 0) break;
                sent += l;
            }
            ifs.close();
        }
        else if (cmd == "/search") {
            std::string keyword = arg1;
            std::vector<std::string> results;
            std::string userdir = DATA_ROOT + username;
            util::search_recursive(userdir, "", keyword, results);
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                util::load_share_map();
                for (auto it = share_map.lower_bound(username); it != share_map.upper_bound(username); ++it) {
                    std::string fname = it->second.second;
                    if (fname.find(keyword) != std::string::npos) {
                        std::string shared_from = "[공유:" + it->second.first + "] " + fname;
                        results.push_back(shared_from);
                    }
                }
            }
            std::ostringstream oss;
            if (results.empty()) oss << "OK|(검색 결과 없음)\n";
            else {
                oss << "OK|";
                for (const auto& r : results) oss << r << "\n";
            }
            send_response(client_sock, oss.str());
        }
        else if (cmd == "/quit") {
            break;
        }
        else {
            send_response(client_sock, "ERR|알 수 없는 명령\n");
        }
    }

    {
        std::lock_guard<std::mutex> lock(conn_mutex);
        if (!username.empty()) user_conn.erase(username);
    }
    close(client_sock);
}

// ---- Main Entrypoint ----

int main() {
    util::ensure_dir("server_data");
    util::ensure_dir(DATA_ROOT);
    { std::ofstream touch(USER_DB_FILE, std::ios::app); touch.close(); }
    { std::ofstream touch2(SHARE_MAP_FILE, std::ios::app); touch2.close(); }
    {
        std::lock_guard<std::mutex> lock(user_mutex);
        util::load_user_db();
    }
    {
        std::lock_guard<std::mutex> lock(share_mutex);
        util::load_share_map();
    }

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock < 0) { std::cerr << "소켓 생성 실패\n"; return 1; }
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(serv_sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "바인드 실패\n"; return 2;
    }
    if (listen(serv_sock, 5) < 0) {
        std::cerr << "리스닝 실패\n"; return 3;
    }
    std::cout << "서버 시작: 포트 " << PORT << std::endl;
    while (true) {
        int cli_sock = accept(serv_sock, nullptr, nullptr);
        if (cli_sock < 0) continue;
        std::thread t(handle_client, cli_sock);
        t.detach();
    }
    return 0;
}