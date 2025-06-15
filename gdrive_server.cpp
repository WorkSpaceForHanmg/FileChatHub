#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstring>

#define PORT 9001
#define BUFFER_SIZE 8192

std::mutex user_mutex;
std::mutex share_mutex;
std::mutex conn_mutex;
std::map<std::string, int> user_conn; // username -> client_sock

std::string user_db_file = "server_data/users/.userdb";
std::map<std::string, std::string> user_db;

std::string share_map_file = "server_data/sharemap.txt";
std::multimap<std::string, std::pair<std::string, std::string>> share_map; // to_user -> (from_user, path)

void load_user_db() {
    user_db.clear();
    std::ifstream ifs(user_db_file);
    std::string line;
    while (std::getline(ifs, line)) {
        std::istringstream iss(line);
        std::string id, pw;
        if (iss >> id >> pw) user_db[id] = pw;
    }
}
void save_user_db() {
    std::ofstream ofs(user_db_file);
    for (auto& kv : user_db) ofs << kv.first << " " << kv.second << "\n";
}

std::string data_root = "server_data/users/";

void load_share_map() {
    share_map.clear();
    std::ifstream ifs(share_map_file);
    std::string line;
    while (std::getline(ifs, line)) {
        std::istringstream iss(line);
        std::string to_user, from_user, path;
        if (iss >> to_user >> from_user >> path) {
            share_map.insert({to_user, {from_user, path}});
        }
    }
}
void save_share_map() {
    std::ofstream ofs(share_map_file);
    for (const auto& kv : share_map) {
        ofs << kv.first << " " << kv.second.first << " " << kv.second.second << "\n";
    }
}

bool ensure_user_dir(const std::string& user) {
    std::string upath = data_root + user;
    struct stat st;
    if (stat(upath.c_str(), &st) != 0) {
        return mkdir(upath.c_str(), 0755) == 0;
    }
    return S_ISDIR(st.st_mode);
}

std::string list_dir(const std::string& path) {
    std::ostringstream oss;
    DIR* dp = opendir(path.c_str());
    if (!dp) {
        oss << "(폴더 없음)\n";
        return oss.str();
    }
    struct dirent* ep;
    while ((ep = readdir(dp)) != NULL) {
        if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) continue;
        std::string fullpath = path + "/" + ep->d_name;
        struct stat st;
        stat(fullpath.c_str(), &st);
        if (S_ISDIR(st.st_mode))
            oss << "[DIR] ";
        else
            oss << "[FILE] ";
        oss << ep->d_name << "\n";
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
        while ((entry = readdir(dir)) != NULL) {
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
        std::string dir = to.substr(0, slash);
        struct stat st;
        if (stat(dir.c_str(), &st) != 0) mkdir(dir.c_str(), 0755);
    }
    return rename(from.c_str(), to.c_str()) == 0;
}

void search_recursive(const std::string& base, const std::string& path, const std::string& keyword, std::vector<std::string>& results) {
    std::string fullpath = base + (path.empty() ? "" : "/" + path);
    DIR* dir = opendir(fullpath.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        std::string rel = path.empty() ? entry->d_name : path + "/" + entry->d_name;
        if (strstr(entry->d_name, keyword.c_str())) {
            results.push_back(rel);
        }
        std::string child_full = fullpath + "/" + entry->d_name;
        struct stat st;
        if (stat(child_full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            search_recursive(base, rel, keyword, results);
        }
    }
    closedir(dir);
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    std::string username;
    bool logged_in = false;

    send(client_sock, "OK|로그인 또는 회원가입 선택: (1) 로그인 (2) 회원가입 입력\n", 60, 0);

    while (!logged_in) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(client_sock, buffer, sizeof(buffer)-1, 0);
        if (len <= 0) return;
        buffer[len] = 0;
        std::string input(buffer);
        std::istringstream iss(input);
        std::string mode, id, pw;
        getline(iss, mode, '|');
        getline(iss, id, '|');
        getline(iss, pw, '|');

        {
            std::lock_guard<std::mutex> lock(user_mutex);
            load_user_db();

            if (mode == "1") {
                if (user_db.count(id) && user_db[id] == pw) {
                    username = id;
                    logged_in = true;
                    ensure_user_dir(username);
                    send(client_sock, "OK|로그인 성공\n", 20, 0);
                } else {
                    send(client_sock, "ERR|로그인 실패. 다시 시도\n", 40, 0);
                }
            }
            else if (mode == "2") {
                if (user_db.count(id)) {
                    send(client_sock, "ERR|이미 존재하는 아이디입니다. 다시 시도\n", 60, 0);
                } else {
                    user_db[id] = pw;
                    save_user_db();
                    username = id;
                    logged_in = true;
                    ensure_user_dir(username);
                    send(client_sock, "OK|회원가입 및 로그인 성공\n", 40, 0);
                }
            }
            else {
                send(client_sock, "ERR|1 또는 2만 입력 가능\n", 40, 0);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(conn_mutex);
        user_conn[username] = client_sock;
    }
    {
        std::lock_guard<std::mutex> lock(share_mutex);
        load_share_map();
    }

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(client_sock, buffer, sizeof(buffer)-1, 0);
        if (len <= 0) break;
        buffer[len] = 0;
        std::string input(buffer);
        std::istringstream iss(input);
        std::string cmd, arg1, arg2;
        getline(iss, cmd, '|');
        getline(iss, arg1, '|');
        getline(iss, arg2, '|');

        if (cmd == "/msg") {
            std::lock_guard<std::mutex> lock(conn_mutex);
            auto it = user_conn.find(arg1);
            if (it != user_conn.end()) {
                std::ostringstream oss;
                oss << "MSG|[" << username << "] " << arg2 << "\n";
                send(it->second, oss.str().c_str(), oss.str().size(), 0);
                send(client_sock, "OK|메시지 전송 완료\n", 32, 0);
            } else {
                send(client_sock, "ERR|상대방이 온라인이 아님\n", 44, 0);
            }
        }
        else if (cmd == "/share") {
            bool user_ok = false;
            {
                std::lock_guard<std::mutex> ulock(user_mutex);
                load_user_db();
                if (user_db.count(arg2)) user_ok = true;
            }
            if (!user_ok) {
                send(client_sock, "ERR|상대 유저 없음\n", 28, 0);
                continue;
            }
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                load_share_map();
                bool already = false;
                for (auto it = share_map.lower_bound(arg2); it != share_map.upper_bound(arg2); ++it) {
                    if (it->second.first == username && it->second.second == arg1) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    send(client_sock, "ERR|이미 공유한 항목입니다\n", 40, 0);
                } else {
                    share_map.insert({arg2, {username, arg1}});
                    save_share_map();
                    send(client_sock, "OK|공유 성공\n", 18, 0);
                }
            }
        }
        else if (cmd == "/unshare") {
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                load_share_map();
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
                    save_share_map();
                    send(client_sock, "OK|공유 해제 성공\n", 24, 0);
                } else {
                    send(client_sock, "ERR|공유 항목 없음\n", 28, 0);
                }
            }
        }
        else if (cmd == "/sharedwithme") {
            std::ostringstream oss;
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                load_share_map();
                for (auto it = share_map.lower_bound(username); it != share_map.upper_bound(username); ++it) {
                    oss << "[FROM " << it->second.first << "] " << it->second.second << "\n";
                }
            }
            std::string result = oss.str();
            if (result.empty()) result = "(공유받은 항목 없음)\n";
            send(client_sock, ("OK|" + result).c_str(), result.size()+3, 0);
        }
        else if (cmd == "/ls") {
            std::string target = arg1.empty() ? "" : "/" + arg1;
            std::string dir = data_root + username + target;
            std::string result = list_dir(dir);
            send(client_sock, ("OK|" + result).c_str(), result.size()+3, 0);
        }
        else if (cmd == "/mkdir") {
            std::string dir = data_root + username + "/" + arg1;
            if (make_dir(dir))
                send(client_sock, "OK|폴더 생성 성공\n", 26, 0);
            else
                send(client_sock, "ERR|폴더 생성 실패\n", 27, 0);
        }
        else if (cmd == "/rm") {
            std::string path = data_root + username + "/" + arg1;
            if (remove_path(path))
                send(client_sock, "OK|삭제 성공\n", 18, 0);
            else
                send(client_sock, "ERR|삭제 실패\n", 19, 0);
        }
        else if (cmd == "/mv") {
            std::string from = data_root + username + "/" + arg1;
            std::string to = data_root + username + "/" + arg2;
            if (move_path(from, to))
                send(client_sock, "OK|이동/이름변경 성공\n", 32, 0);
            else
                send(client_sock, "ERR|이동/이름변경 실패\n", 33, 0);
        }
        else if (cmd == "/upload") {
            std::string fpath = data_root + username + "/" + arg1;
            int filesize = stoi(arg2);
            size_t slash = fpath.find_last_of('/');
            if (slash != std::string::npos) {
                std::string dir = fpath.substr(0, slash);
                struct stat st;
                if (stat(dir.c_str(), &st) != 0) {
                    mkdir(dir.c_str(), 0755);
                }
            }
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
                send(client_sock, "OK|업로드 성공\n", 22, 0);
            else
                send(client_sock, "ERR|업로드 실패\n", 23, 0);
        }
        else if (cmd == "/download") {
            std::string fpath = data_root + username + "/" + arg1;
            struct stat st;
            bool found = false;
            bool shared = false;
            std::string owner;
            if (stat(fpath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                found = true;
            } else {
                std::lock_guard<std::mutex> slock(share_mutex);
                load_share_map();
                for (auto it = share_map.lower_bound(username); it != share_map.upper_bound(username); ++it) {
                    if (it->second.second == arg1) {
                        owner = it->second.first;
                        found = true;
                        shared = true;
                        break;
                    }
                }
                if (found) {
                    fpath = data_root + owner + "/" + arg1;
                    if (stat(fpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                        send(client_sock, "ERR|파일 없음\n", 19, 0);
                        continue;
                    }
                }
            }
            if (!found) {
                send(client_sock, "ERR|파일 없음\n", 19, 0);
                continue;
            }
            int filesize = st.st_size;
            std::ifstream ifs(fpath, std::ios::binary);
            if (!ifs) {
                send(client_sock, "ERR|파일 열기 실패\n", 25, 0);
                continue;
            }
            std::ostringstream oss;
            oss << "OK|" << filesize << "|";
            std::string header = oss.str();
            send(client_sock, header.c_str(), header.size(), 0);
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
            std::string userdir = data_root + username;
            search_recursive(userdir, "", keyword, results);
            {
                std::lock_guard<std::mutex> slock(share_mutex);
                load_share_map();
                for (auto it = share_map.lower_bound(username); it != share_map.upper_bound(username); ++it) {
                    std::string fname = it->second.second;
                    if (fname.find(keyword) != std::string::npos) {
                        std::string shared_from = "[공유:" + it->second.first + "] " + fname;
                        results.push_back(shared_from);
                    }
                }
            }
            std::ostringstream oss;
            if (results.empty()) {
                oss << "OK|(검색 결과 없음)\n";
            } else {
                oss << "OK|";
                for (const auto& r : results) oss << r << "\n";
            }
            send(client_sock, oss.str().c_str(), oss.str().size(), 0);
        }
        else if (cmd == "/quit") {
            break;
        }
        else {
            send(client_sock, "ERR|알 수 없는 명령\n", 28, 0);
        }
    }
    {
        std::lock_guard<std::mutex> lock(conn_mutex);
        if (!username.empty()) user_conn.erase(username);
    }
    close(client_sock);
}

int main() {
    struct stat st;
    if (stat("server_data", &st) != 0)
        mkdir("server_data", 0755);
    if (stat(data_root.c_str(), &st) != 0)
        mkdir(data_root.c_str(), 0755);
    std::ofstream touch(user_db_file, std::ios::app);
    touch.close();
    std::ofstream touch2(share_map_file, std::ios::app);
    touch2.close();

    {
        std::lock_guard<std::mutex> lock(user_mutex);
        load_user_db();
    }
    {
        std::lock_guard<std::mutex> lock(share_mutex);
        load_share_map();
    }

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock < 0) {
        std::cerr << "소켓 생성 실패\n";
        return 1;
    }
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(serv_sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "바인드 실패\n";
        return 2;
    }
    if (listen(serv_sock, 5) < 0) {
        std::cerr << "리스닝 실패\n";
        return 3;
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