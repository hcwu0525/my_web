#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <signal.h>
#include <iomanip>

// Socket 相关头文件
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

// 复用服务器端的 JSON 解析类
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
        
        // 解析metadata
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

// 文件接收信息
struct FileReceive {
    std::string filename;
    std::string filepath;
    size_t expected_size;
    size_t received;
    std::string sender;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point last_update;
    std::ofstream file_handle;
    int chunk_count;
};

class ChatClient {
private:
    int client_socket;
    bool connected;
    std::string username;
    std::thread receive_thread;
    FileReceive* current_file;

    // 发送消息（与Python版本兼容）
    bool sendMessage(const std::string& type, const std::string& data, 
                    const std::map<std::string, std::string>& metadata = {}) {
        try {
            std::string json_msg = SimpleJSON::createMessage(type, data, metadata);
            
            // 发送消息长度（4字节，网络字节序）
            uint32_t size = htonl(json_msg.size());
            if (send(client_socket, &size, sizeof(size), 0) != sizeof(size)) {
                return false;
            }
            
            // 发送消息内容
            return send(client_socket, json_msg.c_str(), json_msg.size(), 0) == static_cast<ssize_t>(json_msg.size());
        } catch (...) {
            return false;
        }
    }

    // 接收消息（与Python版本兼容）
    bool receiveMessage(SimpleJSON::ParsedMessage& msg) {
        try {
            // 接收消息长度
            uint32_t size;
            if (recv(client_socket, &size, sizeof(size), MSG_WAITALL) != sizeof(size)) {
                return false;
            }
            
            size = ntohl(size);
            if (size > 1024 * 1024) return false; // 限制消息大小1MB
            
            // 接收消息内容
            std::string json_data(size, 0);
            if (recv(client_socket, &json_data[0], size, MSG_WAITALL) != static_cast<ssize_t>(size)) {
                return false;
            }
            
            msg = SimpleJSON::parseMessage(json_data);
            return true;
        } catch (...) {
            return false;
        }
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

    // 格式化传输速度
    std::string formatSpeed(double bytesPerSec) {
        if (bytesPerSec < 1024) return std::to_string(static_cast<int>(bytesPerSec)) + " B/s";
        if (bytesPerSec < 1024 * 1024) return std::to_string(static_cast<int>(bytesPerSec / 1024)) + " KB/s";
        return std::to_string(static_cast<int>(bytesPerSec / (1024 * 1024))) + " MB/s";
    }

    // 创建进度条
    std::string createProgressBar(double progress, int width = 20) {
        int filled = static_cast<int>(progress * width / 100.0);
        std::string bar = "[";
        for (int i = 0; i < width; i++) {
            if (i < filled) {
                bar += "=";
            } else if (i == filled) {
                bar += ">";
            } else {
                bar += " ";
            }
        }
        bar += "]";
        return bar;
    }

    // 接收消息线程
    void receiveMessages() {
        while (connected) {
            SimpleJSON::ParsedMessage msg;
            if (!receiveMessage(msg)) {
                if (connected) {
                    std::cout << "\n与服务器连接断开" << std::endl;
                }
                connected = false;
                break;
            }
            
            processReceivedMessage(msg);
            
            // 只在不是文件传输消息时显示提示符
            if (connected && msg.type != MessageType::FILE_DATA && msg.type != MessageType::FILE) {
                std::cout << ">>> " << std::flush;
            }
        }
    }

    // 处理接收到的消息
    void processReceivedMessage(const SimpleJSON::ParsedMessage& msg) {
        if (msg.type == MessageType::TEXT) {
            std::cout << msg.data << std::endl;
        }
        else if (msg.type == MessageType::USER_JOIN || msg.type == MessageType::USER_LEAVE) {
            std::cout << "[系统消息] " << msg.data << std::endl;
        }
        else if (msg.type == MessageType::FILE) {
            // 开始接收文件
            std::string filename = msg.metadata.count("filename") ? msg.metadata.at("filename") : "unknown_file";
            size_t file_size = msg.metadata.count("size") ? std::stoull(msg.metadata.at("size")) : 0;
            std::string sender = msg.metadata.count("sender") ? msg.metadata.at("sender") : "Unknown";
            
            std::cout << "\n📥 接收文件: " << filename << std::endl;
            std::cout << "👤 发送者: " << sender << std::endl;
            std::cout << "📊 文件大小: " << formatSize(file_size) << std::endl;
            
            startFileReception(filename, file_size, sender);
        }
        else if (msg.type == MessageType::FILE_DATA && current_file) {
            // 接收文件数据
            receiveFileChunk(msg.data);
        }
        else if (msg.type == MessageType::FILE_COMPLETE && current_file) {
            // 文件接收完成
            completeFileReception();
        }
        else if (msg.type == MessageType::ERROR) {
            std::cout << "[错误] " << msg.data << std::endl;
        }
    }

    // 开始文件接收
    void startFileReception(const std::string& filename, size_t file_size, const std::string& sender) {
        std::string files_dir = "./files/downloads";
        system(("mkdir -p " + files_dir).c_str());
        
        // 生成唯一文件名 - 如果重名则添加数字后缀
        std::string file_path = files_dir + "/" + filename;
        
        // 如果文件已存在，添加数字后缀
        int counter = 1;
        while (access(file_path.c_str(), F_OK) == 0) {
            size_t dot_pos = filename.find_last_of('.');
            if (dot_pos != std::string::npos) {
                std::string name = filename.substr(0, dot_pos);
                std::string ext = filename.substr(dot_pos);
                file_path = files_dir + "/" + name + "_" + std::to_string(counter) + ext;
            } else {
                file_path = files_dir + "/" + filename + "_" + std::to_string(counter);
            }
            counter++;
        }
        
        current_file = new FileReceive();
        current_file->filename = filename;
        current_file->filepath = file_path;
        current_file->expected_size = file_size;
        current_file->received = 0;
        current_file->sender = sender;
        current_file->start_time = std::chrono::system_clock::now();
        current_file->last_update = std::chrono::system_clock::now();
        current_file->file_handle.open(file_path, std::ios::binary);
        current_file->chunk_count = 0;
    }

    // 接收文件块
    void receiveFileChunk(const std::string& hex_data) {
        if (!current_file) return;
        
        // 将十六进制字符串转换为字节
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex_data.length(); i += 2) {
            if (i + 1 >= hex_data.length()) break;
            std::string byte_str = hex_data.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            bytes.push_back(byte);
        }
        
        current_file->file_handle.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        current_file->received += bytes.size();
        current_file->chunk_count++;
        
        // 显示进度（每0.1秒更新一次）
        auto current_time = std::chrono::system_clock::now();
        auto duration_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - current_file->last_update);
        
        if (current_file->expected_size > 0 && duration_since_update.count() >= 200) {
            double progress = (static_cast<double>(current_file->received) / current_file->expected_size) * 100.0;
            
            auto duration_total = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - current_file->start_time);
            double elapsed_seconds = duration_total.count() / 1000.0;
            
            if (elapsed_seconds > 0.1) {  // 避免除零和过早计算速度
                double speed = current_file->received / elapsed_seconds;
                std::string progress_bar = createProgressBar(progress);
                
                // 清除当前行并重新输出进度
                std::cout << "\r\033[K" << progress_bar << " " << std::fixed << std::setprecision(1) << progress 
                         << "% | " << formatSpeed(speed) << " | " << formatSize(current_file->received) 
                         << "/" << formatSize(current_file->expected_size) << std::flush;
                
                current_file->last_update = current_time;
            }
        }
    }

    // 完成文件接收
    void completeFileReception() {
        if (!current_file) return;
        
        current_file->file_handle.close();
        
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - current_file->start_time);
        double total_seconds = duration.count() / 1000.0;
        
        std::cout << "\n✅ 文件接收完成: " << current_file->filename << std::endl;
        std::cout << "💾 保存位置: " << current_file->filepath << std::endl;
        std::cout << "⏱️ 接收时间: " << formatTime(total_seconds) << std::endl;
        
        if (total_seconds > 0) {
            double avg_speed = current_file->received / total_seconds;
            std::cout << "🚀 平均速度: " << formatSpeed(avg_speed) << std::endl;
        }
        
        std::cout << "📦 数据块数: " << current_file->chunk_count << std::endl;
        
        delete current_file;
        current_file = nullptr;
        
        // 文件传输完成后显示新的提示符
        std::cout << ">>> " << std::flush;
    }

    // 发送文件
    void sendFile(const std::string& file_path) {
        // 检查文件
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            std::cout << "文件不存在: " << file_path << std::endl;
            return;
        }
        
        std::string filename = file_path.substr(file_path.find_last_of('/') + 1);
        size_t file_size = file_stat.st_size;
        
        std::cout << "📤 开始发送文件: " << filename << std::endl;
        std::cout << "📊 文件大小: " << formatSize(file_size) << std::endl;
        
        // 发送文件信息
        std::map<std::string, std::string> metadata;
        metadata["filename"] = filename;
        metadata["size"] = std::to_string(file_size);
        metadata["sender"] = username;
        
        if (!sendMessage(MessageType::FILE, "", metadata)) {
            std::cout << "发送文件信息失败" << std::endl;
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
            
            if (!sendMessage(MessageType::FILE_DATA, hex_data, chunk_metadata)) {
                std::cout << "发送文件数据失败" << std::endl;
                return;
            }
            
            bytes_sent += bytes_read;
            chunk_count++;
            
            // 显示进度
            auto current_time = std::chrono::system_clock::now();
            auto duration_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update);
            
            if (file_size > 0 && duration_since_update.count() >= 100) {
                double progress = (static_cast<double>(bytes_sent) / file_size) * 100.0;
                
                auto duration_total = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                double elapsed_seconds = duration_total.count() / 1000.0;
                
                if (elapsed_seconds > 0) {
                    double speed = bytes_sent / elapsed_seconds;
                    std::string progress_bar = createProgressBar(progress);
                    
                    std::cout << "\r" << progress_bar << " " << std::fixed << std::setprecision(1) << progress 
                             << "% | " << formatSpeed(speed) << " | " << formatSize(bytes_sent) 
                             << "/" << formatSize(file_size) << std::flush;
                    
                    last_update = current_time;
                }
            }
        }
        
        file.close();
        
        // 发送完成信号
        std::map<std::string, std::string> complete_metadata;
        complete_metadata["filename"] = filename;
        complete_metadata["total_size"] = std::to_string(file_size);
        complete_metadata["chunk_count"] = std::to_string(chunk_count);
        
        sendMessage(MessageType::FILE_COMPLETE, "", complete_metadata);
        
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        double total_seconds = duration.count() / 1000.0;
        
        std::cout << "\n✅ 文件发送完成: " << filename << std::endl;
        std::cout << "⏱️ 发送时间: " << formatTime(total_seconds) << std::endl;
        
        if (total_seconds > 0) {
            double avg_speed = bytes_sent / total_seconds;
            std::cout << "🚀 平均速度: " << formatSpeed(avg_speed) << std::endl;
        }
        
        std::cout << "📦 数据块数: " << chunk_count << std::endl;
    }

public:
    ChatClient() : client_socket(-1), connected(false), current_file(nullptr) {}

    ~ChatClient() {
        disconnect();
        if (current_file) {
            if (current_file->file_handle.is_open()) {
                current_file->file_handle.close();
            }
            delete current_file;
        }
    }

    bool connect(const std::string& host, int port, const std::string& username) {
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket < 0) {
            std::cerr << "创建socket失败" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "无效地址: " << host << std::endl;
            close(client_socket);
            return false;
        }
        
        if (::connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "连接失败" << std::endl;
            close(client_socket);
            return false;
        }
        
        // 发送用户名
        if (!sendMessage(MessageType::USER_JOIN, username)) {
            std::cerr << "发送用户名失败" << std::endl;
            close(client_socket);
            return false;
        }
        
        connected = true;
        this->username = username;
        
        std::cout << "成功连接到服务器 " << host << ":" << port << std::endl;
        std::cout << "用户名: " << username << std::endl;
        return true;
    }

    void run() {
        if (!connected) return;
        
        std::cout << "\n聊天室客户端启动成功!" << std::endl;
        std::cout << "\n聊天室命令:" << std::endl;
        std::cout << "  /send <文件路径> - 发送文件" << std::endl;
        std::cout << "  /help - 显示帮助信息" << std::endl;
        std::cout << "  /quit - 退出聊天室" << std::endl;
        std::cout << "  直接输入文本发送消息" << std::endl;
        std::cout << "  @用户名 消息内容 - 发送私信\n" << std::endl;
        
        // 启动接收线程
        receive_thread = std::thread(&ChatClient::receiveMessages, this);
        
        // 处理用户输入
        std::string input;
        std::cout << ">>> " << std::flush;
        
        while (connected && std::getline(std::cin, input)) {
            if (input.empty()) {
                std::cout << ">>> " << std::flush;
                continue;
            }
            
            if (input == "/quit" || input == "exit") {
                disconnect();
                break;
            }
            
            if (input == "/help") {
                showHelp();
            }
            else if (input.substr(0, 6) == "/send ") {
                std::string file_path = input.substr(6);
                
                // 移除引号
                if (file_path.front() == '"' && file_path.back() == '"') {
                    file_path = file_path.substr(1, file_path.length() - 2);
                }
                
                sendFile(file_path);
            }
            else {
                // 发送文本消息
                sendMessage(MessageType::TEXT, input);
            }
            
            if (connected) {
                std::cout << ">>> " << std::flush;
            }
        }
        
        if (receive_thread.joinable()) {
            receive_thread.join();
        }
    }

    void disconnect() {
        if (connected) {
            connected = false;
            
            sendMessage(MessageType::USER_LEAVE, "");
            
            close(client_socket);
            std::cout << "已断开连接" << std::endl;
        }
    }

private:
    void showHelp() {
        std::cout << "\n聊天室命令:" << std::endl;
        std::cout << "  /send <文件路径> - 发送文件" << std::endl;
        std::cout << "  /help - 显示帮助信息" << std::endl;
        std::cout << "  /quit 或 exit - 退出聊天室" << std::endl;
        std::cout << "  直接输入文本发送消息" << std::endl;
        std::cout << "  @用户名 消息内容 - 发送私信" << std::endl << std::endl;
    }
};

ChatClient* client = nullptr;

void signalHandler(int) {
    std::cout << "\n收到信号，断开连接..." << std::endl;
    if (client) client->disconnect();
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::string host = "127.0.0.1";
    int port = 8888;
    std::string username;
    
    std::cout << "=== C++ 聊天客户端（Python兼容版本） ===" << std::endl;
    
    if (argc >= 2) {
        host = argv[1];
    } else {
        std::cout << "服务器地址 [" << host << "]: ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) host = input;
    }
    
    if (argc >= 3) {
        port = std::atoi(argv[2]);
    } else {
        std::cout << "端口 [" << port << "]: ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) port = std::atoi(input.c_str());
    }
    
    if (argc >= 4) {
        username = argv[3];
    } else {
        std::cout << "用户名: ";
        std::getline(std::cin, username);
        if (username.empty()) {
            std::cerr << "用户名不能为空" << std::endl;
            return 1;
        }
    }
    
    client = new ChatClient();
    
    if (!client->connect(host, port, username)) {
        delete client;
        return 1;
    }
    
    client->run();
    
    delete client;
    return 0;
}