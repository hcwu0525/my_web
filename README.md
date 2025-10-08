<<<<<<< HEAD
# my_web
=======
# 套接字聊天程序

一个基于Python套接字编程的聊天室应用，支持多客户端连接、实时文本消息传输和文件传输功能。

## 功能特性

- ✅ **多客户端支持**: 服务器可同时处理多个客户端连接
- ✅ **实时聊天**: 支持实时文本消息广播和私信
- ✅ **双向文件传输**: 客户端和服务器都可以发送文件
- ✅ **定向发送**: 服务器可以向特定用户发送消息和文件
- ✅ **服务器管理**: 完整的服务器管理功能，支持广播和定向发送
- ✅ **用户管理**: 用户加入/离开提醒，详细的用户信息查看
- ✅ **命令系统**: 客户端和服务器都有完整的命令行界面
- ✅ **错误处理**: 完善的异常处理和错误恢复

## 项目结构

```
socket_chat/
├── server.py          # 服务端程序
├── client.py          # 客户端程序
├── utils.py           # 共享工具模块
├── files/             # 文件存储目录
│   ├── received/      # 服务器接收的文件
│   ├── downloads/     # 客户端下载的文件
│   ├── server/        # 服务器发送的文件
│   └── test_file.txt  # 测试文件
└── README.md          # 说明文档
```

## 环境要求

- Python 3.6 或更高版本
- 无需额外依赖包（仅使用Python标准库）

## 使用方法

### 1. 启动服务器

```bash
# 使用默认设置 (localhost:8888)
python server.py

# 指定端口
python server.py 9999

# 指定主机和端口
python server.py 9999 0.0.0.0
```

服务器启动后会显示：
```
聊天服务器已启动，监听 localhost:8888
等待客户端连接...

服务器管理命令:
  /msg <消息内容> - 向所有客户端广播消息
  /msg @用户名 <消息内容> - 向指定用户发送私信
  /send <文件路径> - 向所有客户端广播文件
  /send @用户名 <文件路径> - 向指定用户发送文件
  /list - 显示在线用户列表
  /user <用户名> - 显示用户详细信息
  /help - 显示帮助信息
  /quit - 关闭服务器
按 Ctrl+C 停止服务器
```

### 2. 启动客户端

```bash
# 连接默认服务器 (localhost:8888)
python client.py

# 连接指定端口的服务器
python client.py 9999

# 连接指定主机和端口的服务器
python client.py 9999 192.168.1.100

# 连接时指定用户名
python client.py 8888 localhost MyUsername
```

客户端连接成功后会显示：
```
已连接到服务器 localhost:8888
用户名: MyUsername

聊天室命令:
  /send <文件路径> - 发送文件
  /help - 显示帮助信息
  /quit - 退出聊天室
  直接输入文本发送消息
```

### 3. 使用聊天功能

#### 发送文本消息
直接输入文本并按回车键：
```
Hello, everyone!
```

#### 发送文件
使用 `/send` 命令：
```
/send /path/to/your/file.txt
/send "C:\\Users\\username\\Documents\\image.jpg"
```

#### 其他命令
- `/help` - 显示帮助信息
- `/quit` - 退出聊天室

### 4. 服务器管理功能

#### 广播消息给所有客户端
```
/msg 这是来自服务器的重要通知！
```

#### 发送私信给特定用户
```
/msg @Alice 请注意查看新的文档
/msg @Bob 你的任务已经完成
```

#### 广播文件给所有客户端
```
/send files/server/server_announcement.txt
/send "/path/to/important/document.pdf"
```

#### 发送文件给特定用户
```
/send @Alice files/personal_report.pdf
/send @Bob "/path/to/user_guide.txt"
```

#### 用户管理
```
/list                    # 显示所有在线用户
/user Alice              # 显示用户Alice的详细信息
```

#### 其他管理命令
- `/help` - 显示服务器管理帮助
- `/quit` - 关闭服务器

## 技术实现

### 消息协议

程序使用自定义的JSON消息协议：

```python
{
    "type": "消息类型",
    "data": "消息数据", 
    "metadata": {
        # 元数据信息
    }
}
```

支持的消息类型：
- `TEXT` - 文本消息
- `FILE` - 文件信息
- `FILE_DATA` - 文件数据块
- `FILE_COMPLETE` - 文件传输完成
- `USER_JOIN` - 用户加入
- `USER_LEAVE` - 用户离开
- `ERROR` - 错误消息

### 文件传输

文件传输采用分块传输方式：
1. 发送文件基本信息（文件名、大小等）
2. 分块发送文件数据（默认4KB块大小）
3. 发送传输完成信号

### 多线程处理

- **服务器**: 为每个客户端连接创建独立的处理线程
- **客户端**: 使用单独的线程接收服务器消息，主线程处理用户输入

## 示例使用场景

### 场景1：基本聊天

1. 启动服务器：
   ```bash
   python server.py
   ```

2. 启动多个客户端：
   ```bash
   # 终端1
   python client.py
   # 输入用户名: Alice
   
   # 终端2  
   python client.py
   # 输入用户名: Bob
   ```

3. 开始聊天：
   ```
   # Alice 发送消息
   Hello Bob!
   
   # Bob 收到消息并回复
   [14:30:25] Alice: Hello Bob!
   Hi Alice! How are you?
   
   # Alice 收到回复
   [14:30:28] Bob: Hi Alice! How are you?
   ```

### 场景2：文件传输

```bash
# Alice 发送文件给其他用户
/send /Users/alice/document.pdf

# 其他用户会看到
[文件传输] 用户 'Alice' 正在发送文件: document.pdf (2048576 字节)
[文件接收] 进度: 100.0%
[文件传输] 文件接收完成: document.pdf
保存位置: ./files/downloads/document.pdf
```

### 场景3：服务器管理和定向发送

```bash
# 服务器广播系统消息
/msg 系统将在5分钟后进行维护，请保存工作

# 所有客户端会收到
[16:45:30] 服务器: 系统将在5分钟后进行维护，请保存工作

# 服务器发送私信给特定用户
/msg @Alice 请检查你的个人设置

# 只有Alice会收到
[16:46:10] 服务器: [私信] 请检查你的个人设置

# 服务器发送文件给所有客户端
/send files/server/server_announcement.txt

# 所有客户端会看到
[文件传输] 用户 '服务器' 正在发送文件: server_announcement.txt (256 字节)
[文件接收] 进度: 100.0%
[文件传输] 文件接收完成: server_announcement.txt
保存位置: ./files/downloads/server_announcement.txt

# 服务器发送文件给特定用户
/send @Bob files/personal_document.pdf

# 只有Bob会收到文件
[文件传输] 用户 '服务器' 正在发送文件: personal_document.pdf (1024 字节)
[文件接收] 进度: 100.0%
[文件传输] 文件接收完成: personal_document.pdf
保存位置: ./files/downloads/personal_document.pdf

# 查看在线用户详细信息
/list

# 服务器显示
📋 在线用户列表 (3):
  1. Alice (192.168.1.100:52341)
  2. Bob (192.168.1.101:52342)
  3. Charlie (192.168.1.102:52343)

# 查看特定用户信息
/user Alice

# 服务器显示
👤 用户信息:
  用户名: Alice
  IP地址: 192.168.1.100
  端口: 52341
  连接状态: 在线
```

## 常见问题

### Q: 如何在局域网内使用？

A: 启动服务器时指定网络接口：
```bash
python server.py 8888 0.0.0.0
```
然后客户端连接服务器的实际IP地址。

### Q: 传输大文件时速度很慢？

A: 可以修改 `utils.py` 中的 `BUFFER_SIZE` 常量来调整缓冲区大小：
```python
BUFFER_SIZE = 8192  # 增加到8KB
```

### Q: 如何限制连接的客户端数量？

A: 修改 `server.py` 中的 `listen()` 参数：
```python
self.socket.listen(10)  # 最多10个客户端
```

### Q: 客户端异常断开连接怎么办？

A: 服务器会自动检测并清理断开的连接，其他客户端会收到用户离开的通知。

## 安全注意事项

- 本程序仅用于学习和测试目的
- 消息传输未加密，不适用于生产环境
- 建议在受信任的网络环境中使用
- 文件传输时请注意文件大小和磁盘空间

## 新功能说明

### 服务器管理功能

现在服务器不仅可以转发客户端之间的消息，还具备了主动管理能力：

1. **服务器消息广播**: 使用 `/msg` 命令向所有客户端发送系统消息
2. **服务器文件分发**: 使用 `/send` 命令向所有客户端发送文件
3. **用户管理**: 使用 `/list` 命令查看当前在线用户
4. **实时管理**: 服务器管理员可以在运行时执行各种管理操作

### 使用场景

- **系统通知**: 服务器可以发送维护通知、重要公告等
- **文档分发**: 服务器可以向所有用户分发重要文档、更新文件等
- **用户监控**: 实时查看在线用户状态
- **紧急广播**: 在紧急情况下快速通知所有用户

## 扩展建议

1. **消息加密**: 添加SSL/TLS支持
2. **用户认证**: 添加登录验证机制
3. **消息历史**: 保存聊天记录到数据库
4. **图形界面**: 使用tkinter或其他GUI库
5. **文件类型限制**: 添加文件类型和大小限制
6. **房间系统**: 支持多个聊天房间
7. **定向发送**: 服务器可以向特定用户发送消息或文件
8. **管理权限**: 添加管理员权限控制

## 许可证

MIT License - 可自由使用和修改

## 作者

Created with Python socket programming
>>>>>>> 6a662c0 (1120)
