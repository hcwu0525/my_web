#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <sstream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <iomanip>
#include <errno.h>

// Socket 相关头文件
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

// 简单的 JSON 解析和生成（不依赖外部库）
class SimpleJSON {
public:
    static std::string escape(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    static std::string unescape(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.length(); i++) {
            if (str[i] == '\\' && i + 1 < str.length()) {
                switch (str[i + 1]) {
                    case '"': result += '"'; i++; break;
                    case '\\': result += '\\'; i++; break;
                    case 'n': result += '\n'; i++; break;
                    case 'r': result += '\r'; i++; break;
                    case 't': result += '\t'; i++; break;
                    default: result += str[i]; break;
                }
            } else {
                result += str[i];
            }
        }
        return result;
    }
    
    static std::string createMessage(const std::string& type, const std::string& data, 
                                   const std::map<std::string, std::string>& metadata = {}) {
        std::string json = "{";
        json += "\"type\":\"" + escape(type) + "\",";
        json += "\"data\":\"" + escape(data) + "\",";
        json += "\"metadata\":{";
        
        bool first = true;
        for (const auto& pair : metadata) {
            if (!first) json += ",";
            json += "\"" + escape(pair.first) + "\":\"" + escape(pair.second) + "\"";
            first = false;
        }
        json += "}}";
        return json;
    }
    
    struct ParsedMessage {
        std::string type;
        std::string data;
        std::map<std::string, std::string> metadata;
    };
    
    static ParsedMessage parseMessage(const std::string& json) {
        ParsedMessage msg;
        
        // 简单解析（假设格式正确）
        size_t typeStart = json.find("\"type\":\"") + 8;
        size_t typeEnd = json.find("\"", typeStart);
        if (typeEnd != std::string::npos) {
            msg.type = unescape(json.substr(typeStart, typeEnd - typeStart));
        }
        
        size_t dataStart = json.find("\"data\":\"") + 8;
        size_t dataEnd = json.find("\",\"metadata\"", dataStart);
        if (dataEnd == std::string::npos) {
            dataEnd = json.find("\"}", dataStart);
        }
        if (dataEnd != std::string::npos) {
            msg.data = unescape(json.substr(dataStart, dataEnd - dataStart));
        }
        
        // 解析metadata（简化处理）
        size_t metaStart = json.find("\"metadata\":{");
        if (metaStart != std::string::npos) {
            metaStart += 12;
            size_t metaEnd = json.find("}}", metaStart);
            if (metaEnd != std::string::npos) {
                std::string metaStr = json.substr(metaStart, metaEnd - metaStart);
                
                // 解析键值对
                size_t pos = 0;
                while (pos < metaStr.length()) {
                    size_t keyStart = metaStr.find("\"", pos);
                    if (keyStart == std::string::npos) break;
                    keyStart++;
                    
                    size_t keyEnd = metaStr.find("\"", keyStart);
                    if (keyEnd == std::string::npos) break;
                    
                    size_t valueStart = metaStr.find("\"", keyEnd + 1);
                    if (valueStart == std::string::npos) break;
                    valueStart++;
                    
                    size_t valueEnd = metaStr.find("\"", valueStart);
                    if (valueEnd == std::string::npos) break;
                    
                    std::string key = unescape(metaStr.substr(keyStart, keyEnd - keyStart));
                    std::string value = unescape(metaStr.substr(valueStart, valueEnd - valueStart));
                    msg.metadata[key] = value;
                    
                    pos = valueEnd + 1;
                }
            }
        }
        
        return msg;
    }
};

// 消息类型常量（与Python版本一致）
class MessageType {
public:
    static const std::string TEXT;
    static const std::string FILE;
    static const std::string FILE_REQUEST;
    static const std::string FILE_DATA;
    static const std::string FILE_COMPLETE;
    static const std::string USER_JOIN;
    static const std::string USER_LEAVE;
    static const std::string ERROR;
};

const std::string MessageType::TEXT = "TEXT";
const std::string MessageType::FILE = "FILE";
const std::string MessageType::FILE_REQUEST = "FILE_REQUEST";
const std::string MessageType::FILE_DATA = "FILE_DATA";
const std::string MessageType::FILE_COMPLETE = "FILE_COMPLETE";
const std::string MessageType::USER_JOIN = "USER_JOIN";
const std::string MessageType::USER_LEAVE = "USER_LEAVE";
const std::string MessageType::ERROR = "ERROR";

// 客户端信息
struct ClientInfo {
    std::string username;
    std::string address;
    std::chrono::system_clock::time_point connect_time;
};

// 文件传输信息
struct FileTransfer {
    std::string filename;
    std::string filepath;
    size_t expected_size;
    size_t received;
    std::string username;
    std::chrono::system_clock::time_point start_time;
    std::ofstream file_handle;
    int chunk_count;
};

class ChatServer {
private:
    int server_socket;
    int port;
    std::string host;
    bool running;
    std::map<int, ClientInfo> clients;
    std::mutex clients_mutex;
    std::map<int, FileTransfer> file_transfers;
    std::mutex file_transfers_mutex;
    std::vector<std::thread> client_threads;
    std::thread input_thread;

    // 发送消息（与Python版本兼容）
    bool sendMessage(int socket, const std::string& type, const std::string& data, 
                    const std::map<std::string, std::string>& metadata = {}) {
        try {
            std::string json_msg = SimpleJSON::createMessage(type, data, metadata);
            
            // 发送消息长度（4字节，网络字节序）
            uint32_t size = htonl(json_msg.size());
            ssize_t sent = 0;
            ssize_t remaining = sizeof(size);
            char* buffer = reinterpret_cast<char*>(&size);
            
            // 确保完整发送消息长度
            while (remaining > 0) {
                ssize_t bytes_sent = send(socket, buffer + sent, remaining, MSG_NOSIGNAL);
                if (bytes_sent <= 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    return false;
                }
                sent += bytes_sent;
                remaining -= bytes_sent;
            }
            
            // 确保完整发送消息内容
            sent = 0;
            remaining = json_msg.size();
            const char* msg_buffer = json_msg.c_str();
            
            while (remaining > 0) {
                ssize_t bytes_sent = send(socket, msg_buffer + sent, remaining, MSG_NOSIGNAL);
                if (bytes_sent <= 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    return false;
                }
                sent += bytes_sent;
                remaining -= bytes_sent;
            }
            
            return true;
        } catch (...) {
            return false;
        }
    }

    // 接收消息（与Python版本兼容）
    bool receiveMessage(int socket, SimpleJSON::ParsedMessage& msg) {
        try {
            // 接收消息长度
            uint32_t size;
            if (recv(socket, &size, sizeof(size), MSG_WAITALL) != sizeof(size)) {
                return false;
            }
            
            size = ntohl(size);
            if (size > 1024 * 1024) return false; // 限制消息大小1MB
            
            // 接收消息内容
            std::string json_data(size, 0);
            if (recv(socket, &json_data[0], size, MSG_WAITALL) != static_cast<ssize_t>(size)) {
                return false;
            }
            
            msg = SimpleJSON::parseMessage(json_data);
            return true;
        } catch (...) {
            return false;
        }
    }

    // 广播消息
    void broadcastMessage(const std::string& type, const std::string& data,
                         const std::map<std::string, std::string>& metadata = {},
                         int exclude_socket = -1) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        
        for (auto& pair : clients) {
            if (pair.first != exclude_socket) {
                sendMessage(pair.first, type, data, metadata);
            }
        }
    }

    // 向指定用户发送消息
    bool sendToUser(const std::string& username, const std::string& type, const std::string& data,
                   const std::map<std::string, std::string>& metadata = {}) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        
        for (auto& pair : clients) {
            if (pair.second.username == username) {
                return sendMessage(pair.first, type, data, metadata);
            }
        }
        return false;
    }

    // 格式化文件大小
    std::string formatSize(size_t size) {
        if (size < 1024) return std::to_string(size) + " B";
        if (size < 1024 * 1024) return std::to_string(size / 1024) + " KB";
        return std::to_string(size / (1024 * 1024)) + " MB";
    }

    // 格式化时间
    std::string formatTime(double seconds) {
        if (seconds < 1.0) return std::to_string(static_cast<int>(seconds * 1000)) + "ms";
        if (seconds < 60.0) return std::to_string(static_cast<int>(seconds)) + "s";
        int mins = static_cast<int>(seconds / 60);
        int secs = static_cast<int>(seconds) % 60;
        return std::to_string(mins) + "m" + std::to_string(secs) + "s";
    }

    // 处理客户端
    void handleClient(int client_socket, const std::string& client_address) {
        std::string username;
        
        try {
            // 等待用户名
            SimpleJSON::ParsedMessage msg;
            if (!receiveMessage(client_socket, msg) || msg.type != MessageType::USER_JOIN) {
                close(client_socket);
                return;
            }
            
            username = msg.data.empty() ? ("User_" + std::to_string(client_socket)) : msg.data;
            
            // 添加客户端
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                ClientInfo info;
                info.username = username;
                info.address = client_address;
                info.connect_time = std::chrono::system_clock::now();
                clients[client_socket] = info;
            }
            
            std::cout << "用户 '" << username << "' 加入聊天室 (来自 " << client_address << ")" << std::endl;
            
            // 广播加入消息
            std::string join_msg = "用户 '" + username + "' 加入了聊天室";
            broadcastMessage(MessageType::USER_JOIN, join_msg, {}, client_socket);
            
            // 发送欢迎消息
            int online_count;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                online_count = clients.size();
            }
            std::string welcome_msg = "欢迎加入聊天室！当前在线用户数: " + std::to_string(online_count);
            sendMessage(client_socket, MessageType::TEXT, welcome_msg);
            
            // 处理消息
            while (running) {
                SimpleJSON::ParsedMessage client_msg;
                if (!receiveMessage(client_socket, client_msg)) {
                    break;
                }
                
                processMessage(client_socket, client_msg, username);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "处理客户端错误: " << e.what() << std::endl;
        }
        
        // 断开连接
        disconnectClient(client_socket, username);
    }

    // 处理消息
    void processMessage(int client_socket, const SimpleJSON::ParsedMessage& msg, const std::string& username) {
        if (msg.type == MessageType::TEXT) {
            std::string formatted_msg = username + ": " + msg.data;
            std::cout << "[" << username << "]: " << msg.data << std::endl;
            
            // 检查是否为私信
            if (!msg.data.empty() && msg.data[0] == '@') {
                size_t space_pos = msg.data.find(' ');
                if (space_pos != std::string::npos) {
                    std::string target_user = msg.data.substr(1, space_pos - 1);
                    std::string private_msg = "[私信来自 " + username + "]: " + msg.data.substr(space_pos + 1);
                    
                    if (sendToUser(target_user, MessageType::TEXT, private_msg)) {
                        std::cout << "[私信] " << username << " -> " << target_user << ": " << msg.data.substr(space_pos + 1) << std::endl;
                    } else {
                        std::string error_msg = "用户 '" + target_user + "' 不在线";
                        sendMessage(client_socket, MessageType::ERROR, error_msg);
                    }
                    return;
                }
            }
            
            // 广播普通消息
            broadcastMessage(MessageType::TEXT, formatted_msg, {}, client_socket);
        }
        else if (msg.type == MessageType::FILE) {
            // 文件传输开始
            std::string filename = msg.metadata.count("filename") ? msg.metadata.at("filename") : "unknown_file";
            size_t file_size = msg.metadata.count("size") ? std::stoull(msg.metadata.at("size")) : 0;
            
            std::cout << "[" << username << "] 开始发送文件: " << filename 
                      << " (" << formatSize(file_size) << ")" << std::endl;
            
            prepareFileReception(client_socket, filename, file_size, username);
            
            // 转发给其他客户端
            broadcastMessage(MessageType::FILE, msg.data, msg.metadata, client_socket);
        }
        else if (msg.type == MessageType::FILE_DATA) {
            // 文件数据
            saveFileChunk(client_socket, msg.data);
            
            // 转发给其他客户端
            broadcastMessage(MessageType::FILE_DATA, msg.data, msg.metadata, client_socket);
        }
        else if (msg.type == MessageType::FILE_COMPLETE) {
            // 文件传输完成
            std::string filename = msg.metadata.count("filename") ? msg.metadata.at("filename") : "unknown_file";
            completeFileReception(client_socket);
            
            std::cout << "✅ 用户 '" << username << "' 完成文件发送: " << filename << std::endl;
            
            // 广播完成消息
            std::string complete_msg = "用户 '" + username + "' 分享了文件: " + filename;
            broadcastMessage(MessageType::TEXT, complete_msg, {}, client_socket);
            
            // 转发完成信号
            broadcastMessage(MessageType::FILE_COMPLETE, msg.data, msg.metadata, client_socket);
        }
    }

    // 准备文件接收
    void prepareFileReception(int client_socket, const std::string& filename, size_t file_size, const std::string& username) {
        std::string files_dir = "./files/received";
        system(("mkdir -p " + files_dir).c_str());
        
        // 生成唯一文件名 - 使用 {username}_{filename} 格式
        std::string base_filename = username + "_" + filename;
        std::string file_path = files_dir + "/" + base_filename;
        
        // 如果文件已存在，添加数字后缀
        int counter = 1;
        while (access(file_path.c_str(), F_OK) == 0) {
            size_t dot_pos = filename.find_last_of('.');
            if (dot_pos != std::string::npos) {
                std::string name = filename.substr(0, dot_pos);
                std::string ext = filename.substr(dot_pos);
                base_filename = username + "_" + name + "_" + std::to_string(counter) + ext;
            } else {
                base_filename = username + "_" + filename + "_" + std::to_string(counter);
            }
            file_path = files_dir + "/" + base_filename;
            counter++;
        }
        
        std::lock_guard<std::mutex> lock(file_transfers_mutex);
        FileTransfer& transfer = file_transfers[client_socket];
        transfer.filename = filename;
        transfer.filepath = file_path;
        transfer.expected_size = file_size;
        transfer.received = 0;
        transfer.username = username;
        transfer.start_time = std::chrono::system_clock::now();
        transfer.file_handle.open(file_path, std::ios::binary);
        transfer.chunk_count = 0;
        
        std::cout << "📥 开始接收文件: " << filename << " -> " << file_path << std::endl;
    }

    // 保存文件块
    void saveFileChunk(int client_socket, const std::string& hex_data) {
        std::lock_guard<std::mutex> lock(file_transfers_mutex);
        
        auto it = file_transfers.find(client_socket);
        if (it == file_transfers.end()) return;
        
        FileTransfer& transfer = it->second;
        
        // 将十六进制字符串转换为字节
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex_data.length(); i += 2) {
            std::string byte_str = hex_data.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            bytes.push_back(byte);
        }
        
        transfer.file_handle.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        transfer.received += bytes.size();
        transfer.chunk_count++;
        
        // 显示进度
        if (transfer.expected_size > 0) {
            int progress = (transfer.received * 100) / transfer.expected_size;
            std::cout << "\r接收进度: " << progress << "%" << std::flush;
        }
    }

    // 完成文件接收
    void completeFileReception(int client_socket) {
        std::lock_guard<std::mutex> lock(file_transfers_mutex);
        
        auto it = file_transfers.find(client_socket);
        if (it == file_transfers.end()) return;
        
        FileTransfer& transfer = it->second;
        transfer.file_handle.close();
        
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - transfer.start_time);
        double seconds = duration.count() / 1000.0;
        
        std::cout << "\n✅ 文件接收完成: " << transfer.filename << std::endl;
        std::cout << "💾 保存位置: " << transfer.filepath << std::endl;
        std::cout << "⏱️ 传输时间: " << formatTime(seconds) << std::endl;
        std::cout << "📦 数据块数: " << transfer.chunk_count << std::endl;
        
        file_transfers.erase(it);
    }

    // 断开客户端
    void disconnectClient(int client_socket, const std::string& username) {
        // 清理文件传输
        {
            std::lock_guard<std::mutex> lock(file_transfers_mutex);
            auto it = file_transfers.find(client_socket);
            if (it != file_transfers.end()) {
                if (it->second.file_handle.is_open()) {
                    it->second.file_handle.close();
                }
                file_transfers.erase(it);
            }
        }
        
        // 移除客户端
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.erase(client_socket);
        }
        
        close(client_socket);
        
        if (!username.empty()) {
            std::cout << "用户 '" << username << "' 离开聊天室" << std::endl;
            
            std::string leave_msg = "用户 '" + username + "' 离开了聊天室";
            broadcastMessage(MessageType::USER_LEAVE, leave_msg);
        }
    }

    // 处理服务器输入
    void handleInput() {
        std::string input;
        while (running && std::getline(std::cin, input)) {
            if (input == "/quit") {
                running = false;
                break;
            } else if (input == "/list") {
                showUserList();
            } else if (input.substr(0, 5) == "/msg ") {
                sendServerMessage(input.substr(5));
            } else if (input.substr(0, 6) == "/send ") {
                sendServerFile(input.substr(6));
            } else if (input == "/help") {
                showHelp();
            } else {
                std::cout << "未知命令。输入 /help 查看帮助。" << std::endl;
            }
        }
    }

    // 显示用户列表
    void showUserList() {
        std::lock_guard<std::mutex> lock(clients_mutex);
        
        if (clients.empty()) {
            std::cout << "当前没有在线用户" << std::endl;
            return;
        }
        
        std::cout << "\n在线用户列表 (共 " << clients.size() << " 人):" << std::endl;
        int count = 1;
        for (const auto& pair : clients) {
            const ClientInfo& info = pair.second;
            std::cout << count++ << ". " << info.username 
                      << " (来自 " << info.address << ")" << std::endl;
        }
        std::cout << std::endl;
    }

    // 发送服务器消息
    void sendServerMessage(const std::string& message) {
        if (message.empty()) return;
        
        std::string target_user;
        std::string content = message;
        
        // 检查是否为私信
        if (message.front() == '@') {
            size_t space_pos = message.find(' ');
            if (space_pos != std::string::npos) {
                target_user = message.substr(1, space_pos - 1);
                content = message.substr(space_pos + 1);
            }
        }
        
        std::string formatted_msg = "[服务器]: " + content;
        
        if (target_user.empty()) {
            // 广播消息
            broadcastMessage(MessageType::TEXT, formatted_msg);
            std::cout << "服务器消息已广播: " << content << std::endl;
        } else {
            // 私信
            if (sendToUser(target_user, MessageType::TEXT, "[服务器私信]: " + content)) {
                std::cout << "已向用户 '" << target_user << "' 发送私信: " << content << std::endl;
            } else {
                std::cout << "用户 '" << target_user << "' 不在线" << std::endl;
            }
        }
    }

    // 发送服务器文件
    void sendServerFile(const std::string& command) {
        std::string target_user;
        std::string file_path = command;
        
        // 检查是否为私发
        if (command.front() == '@') {
            size_t space_pos = command.find(' ');
            if (space_pos != std::string::npos) {
                target_user = command.substr(1, space_pos - 1);
                file_path = command.substr(space_pos + 1);
            }
        }
        
        // 检查文件
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            std::cout << "文件不存在: " << file_path << std::endl;
            return;
        }
        
        if (target_user.empty()) {
            sendFileToAll(file_path);
        } else {
            sendFileToUser(target_user, file_path);
        }
    }

    // 向所有用户发送文件
    void sendFileToAll(const std::string& file_path) {
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            std::cout << "文件不存在: " << file_path << std::endl;
            return;
        }

        std::string filename = file_path.substr(file_path.find_last_of('/') + 1);
        size_t file_size = file_stat.st_size;

        std::cout << "📤 开始向所有用户发送文件: " << filename 
                  << " (" << formatSize(file_size) << ")" << std::endl;

        // 发送文件信息给所有客户端
        std::map<std::string, std::string> file_metadata;
        file_metadata["filename"] = filename;
        file_metadata["size"] = std::to_string(file_size);
        file_metadata["sender"] = "服务器";

        broadcastMessage(MessageType::FILE, "", file_metadata);

        // 发送文件数据
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "无法打开文件: " << file_path << std::endl;
            return;
        }

        auto start_time = std::chrono::system_clock::now();
        auto last_update = start_time;
        size_t bytes_sent = 0;
        int chunk_count = 0;
        const size_t BUFFER_SIZE = 8192;

        while (bytes_sent < file_size) {
            std::vector<char> buffer(BUFFER_SIZE);
            file.read(buffer.data(), BUFFER_SIZE);
            size_t bytes_read = file.gcount();

            if (bytes_read == 0) break;

            // 转换为十六进制字符串
            std::string hex_data;
            for (size_t i = 0; i < bytes_read; i++) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned char>(buffer[i]));
                hex_data += hex;
            }

            // 发送数据块给所有客户端
            std::map<std::string, std::string> chunk_metadata;
            chunk_metadata["bytes_sent"] = std::to_string(bytes_sent);
            chunk_metadata["total_size"] = std::to_string(file_size);
            chunk_metadata["chunk_index"] = std::to_string(chunk_count);

            broadcastMessage(MessageType::FILE_DATA, hex_data, chunk_metadata);

            bytes_sent += bytes_read;
            chunk_count++;

            // 显示进度
            auto current_time = std::chrono::system_clock::now();
            auto duration_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update);

            if (file_size > 0 && duration_since_update.count() >= 200) {
                double progress = (static_cast<double>(bytes_sent) / file_size) * 100.0;
                std::cout << "\r发送进度: " << std::fixed << std::setprecision(1) << progress 
                         << "% | " << formatSize(bytes_sent) << "/" << formatSize(file_size) << std::flush;
                last_update = current_time;
            }
        }

        file.close();

        // 发送完成信号
        std::map<std::string, std::string> complete_metadata;
        complete_metadata["filename"] = filename;
        complete_metadata["total_size"] = std::to_string(file_size);
        complete_metadata["chunk_count"] = std::to_string(chunk_count);

        broadcastMessage(MessageType::FILE_COMPLETE, "", complete_metadata);

        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        double total_seconds = duration.count() / 1000.0;

        std::cout << "\n✅ 文件广播完成: " << filename << std::endl;
        std::cout << "⏱️ 发送时间: " << formatTime(total_seconds) << std::endl;
        std::cout << "📦 数据块数: " << chunk_count << std::endl;
    }

    // 向指定用户发送文件
    void sendFileToUser(const std::string& username, const std::string& file_path) {
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            std::cout << "文件不存在: " << file_path << std::endl;
            return;
        }

        std::string filename = file_path.substr(file_path.find_last_of('/') + 1);
        size_t file_size = file_stat.st_size;

        std::cout << "📤 开始向用户 '" << username << "' 发送文件: " << filename 
                  << " (" << formatSize(file_size) << ")" << std::endl;

        // 发送文件信息
        std::map<std::string, std::string> file_metadata;
        file_metadata["filename"] = filename;
        file_metadata["size"] = std::to_string(file_size);
        file_metadata["sender"] = "服务器";

        if (!sendToUser(username, MessageType::FILE, "", file_metadata)) {
            std::cout << "❌ 向用户 '" << username << "' 发送文件信息失败" << std::endl;
            return;
        }

        // 发送文件数据
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "无法打开文件: " << file_path << std::endl;
            return;
        }

        auto start_time = std::chrono::system_clock::now();
        auto last_update = start_time;
        size_t bytes_sent = 0;
        int chunk_count = 0;
        const size_t BUFFER_SIZE = 8192;

        while (bytes_sent < file_size) {
            std::vector<char> buffer(BUFFER_SIZE);
            file.read(buffer.data(), BUFFER_SIZE);
            size_t bytes_read = file.gcount();

            if (bytes_read == 0) break;

            // 转换为十六进制字符串
            std::string hex_data;
            for (size_t i = 0; i < bytes_read; i++) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned char>(buffer[i]));
                hex_data += hex;
            }

            // 发送数据块
            std::map<std::string, std::string> chunk_metadata;
            chunk_metadata["bytes_sent"] = std::to_string(bytes_sent);
            chunk_metadata["total_size"] = std::to_string(file_size);
            chunk_metadata["chunk_index"] = std::to_string(chunk_count);

            if (!sendToUser(username, MessageType::FILE_DATA, hex_data, chunk_metadata)) {
                std::cout << "❌ 向用户 '" << username << "' 发送文件数据失败" << std::endl;
                file.close();
                return;
            }

            bytes_sent += bytes_read;
            chunk_count++;

            // 显示进度
            auto current_time = std::chrono::system_clock::now();
            auto duration_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update);

            if (file_size > 0 && duration_since_update.count() >= 200) {
                double progress = (static_cast<double>(bytes_sent) / file_size) * 100.0;
                std::cout << "\r发送进度: " << std::fixed << std::setprecision(1) << progress 
                         << "% | " << formatSize(bytes_sent) << "/" << formatSize(file_size) << std::flush;
                last_update = current_time;
            }
        }

        file.close();

        // 发送完成信号
        std::map<std::string, std::string> complete_metadata;
        complete_metadata["filename"] = filename;
        complete_metadata["total_size"] = std::to_string(file_size);
        complete_metadata["chunk_count"] = std::to_string(chunk_count);

        if (!sendToUser(username, MessageType::FILE_COMPLETE, "", complete_metadata)) {
            std::cout << "❌ 向用户 '" << username << "' 发送完成信号失败" << std::endl;
            return;
        }

        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        double total_seconds = duration.count() / 1000.0;

        std::cout << "\n✅ 文件发送完成: " << filename << std::endl;
        std::cout << "⏱️ 发送时间: " << formatTime(total_seconds) << std::endl;
        std::cout << "📦 数据块数: " << chunk_count << std::endl;
    }

    // 显示帮助
    void showHelp() {
        std::cout << "\n服务器管理命令:" << std::endl;
        std::cout << "  /msg <消息内容> - 向所有客户端广播消息" << std::endl;
        std::cout << "  /msg @用户名 <消息内容> - 向指定用户发送私信" << std::endl;
        std::cout << "  /send <文件路径> - 向所有客户端广播文件" << std::endl;
        std::cout << "  /send @用户名 <文件路径> - 向指定用户发送文件" << std::endl;
        std::cout << "  /list - 显示在线用户列表" << std::endl;
        std::cout << "  /help - 显示帮助信息" << std::endl;
        std::cout << "  /quit - 关闭服务器" << std::endl << std::endl;
    }

public:
    ChatServer(const std::string& host = "127.0.0.1", int port = 8888) 
        : server_socket(-1), port(port), host(host), running(false) {}

    ~ChatServer() {
        stop();
    }

    bool start() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            std::cerr << "创建socket失败" << std::endl;
            return false;
        }
        
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (host == "localhost" || host == "127.0.0.1") {
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else {
            server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        
        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "绑定地址失败" << std::endl;
            return false;
        }
        
        if (listen(server_socket, 5) < 0) {
            std::cerr << "监听失败" << std::endl;
            return false;
        }
        
        running = true;
        
        std::cout << "聊天服务器已启动，监听 " << host << ":" << port << std::endl;
        std::cout << "等待客户端连接..." << std::endl;
        std::cout << "\n服务器管理命令:" << std::endl;
        std::cout << "  /msg <消息内容> - 向所有客户端广播消息" << std::endl;
        std::cout << "  /msg @用户名 <消息内容> - 向指定用户发送私信" << std::endl;
        std::cout << "  /send <文件路径> - 向所有客户端广播文件" << std::endl;
        std::cout << "  /send @用户名 <文件路径> - 向指定用户发送文件" << std::endl;
        std::cout << "  /list - 显示在线用户列表" << std::endl;
        std::cout << "  /help - 显示帮助信息" << std::endl;
        std::cout << "  /quit - 关闭服务器" << std::endl;
        std::cout << "按 Ctrl+C 停止服务器\n" << std::endl;
        
        return true;
    }

    void run() {
        if (!start()) return;
        
        // 启动输入处理线程
        input_thread = std::thread(&ChatServer::handleInput, this);
        
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                if (running) std::cerr << "接受连接失败" << std::endl;
                continue;
            }
            
            std::string client_address = inet_ntoa(client_addr.sin_addr);
            std::cout << "新连接: " << client_address << ":" << ntohs(client_addr.sin_port) << std::endl;
            
            client_threads.emplace_back(&ChatServer::handleClient, this, client_socket, client_address);
        }
        
        // 清理
        for (auto& thread : client_threads) {
            if (thread.joinable()) thread.join();
        }
        if (input_thread.joinable()) input_thread.join();
    }

    void stop() {
        running = false;
        if (server_socket >= 0) {
            close(server_socket);
        }
    }
};

ChatServer* server = nullptr;

void signalHandler(int) {
    std::cout << "\n收到信号，关闭服务器..." << std::endl;
    if (server) server->stop();
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::string host = "127.0.0.1";
    int port = 8888;
    
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);
    
    std::cout << "=== C++ 聊天服务器（Python兼容版本） ===" << std::endl;
    
    server = new ChatServer(host, port);
    server->run();
    
    delete server;
    return 0;
}