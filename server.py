"""
套接字聊天服务器
支持多客户端连接、文本消息广播和文件传输
"""

import socket
import threading
import sys
import os
from datetime import datetime
from utils import SocketUtils, MessageType, format_message


class ChatServer:
    def __init__(self, host='localhost', port=8888):
        """
        初始化聊天服务器
        
        Args:
            host: 服务器主机地址
            port: 服务器端口
        """
        self.host = host
        self.port = port
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        # 客户端管理
        self.clients = {}  # {socket: {"username": str, "address": tuple}}
        self.clients_lock = threading.Lock()
        
        # 文件接收管理
        self.file_transfers = {}  # {socket: {"file_handle": file, "filename": str, "received": int}}
        
        # 文件存储目录
        self.files_dir = os.path.join(os.path.dirname(__file__), 'files', 'received')
        os.makedirs(self.files_dir, exist_ok=True)
        
        # 服务器发送文件目录
        self.server_files_dir = os.path.join(os.path.dirname(__file__), 'files', 'server')
        os.makedirs(self.server_files_dir, exist_ok=True)
        
        self.running = False
    
    def start(self):
        """启动服务器"""
        try:
            self.socket.bind((self.host, self.port))
            self.socket.listen(5)
            self.running = True
            
            print(f"聊天服务器已启动，监听 {self.host}:{self.port}")
            print("等待客户端连接...")
            print("\n服务器管理命令:")
            print("  /msg <消息内容> - 向所有客户端广播消息")
            print("  /msg @用户名 <消息内容> - 向指定用户发送私信")
            print("  /send <文件路径> - 向所有客户端广播文件")
            print("  /send @用户名 <文件路径> - 向指定用户发送文件")
            print("  /list - 显示在线用户列表")
            print("  /user <用户名> - 显示用户详细信息")
            print("  /help - 显示帮助信息")
            print("  /quit - 关闭服务器")
            print("按 Ctrl+C 停止服务器\n")
            
            # 启动服务器输入处理线程
            input_thread = threading.Thread(target=self.handle_server_input)
            input_thread.daemon = True
            input_thread.start()
            
            while self.running:
                try:
                    client_socket, address = self.socket.accept()
                    print(f"新客户端连接: {address}")
                    
                    # 为每个客户端创建处理线程
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, address)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                    
                except socket.error:
                    if self.running:
                        print("接受连接时发生错误")
                    break
                        
        except Exception as e:
            print(f"启动服务器失败: {e}")
        finally:
            self.stop()
    
    def stop(self):
        """停止服务器"""
        self.running = False
        
        # 关闭所有客户端连接
        with self.clients_lock:
            for client_socket in list(self.clients.keys()):
                try:
                    client_socket.close()
                except:
                    pass
            self.clients.clear()
        
        # 关闭服务器套接字
        try:
            self.socket.close()
        except:
            pass
        
        print("服务器已关闭")
    
    def handle_client(self, client_socket, address):
        """
        处理客户端连接
        
        Args:
            client_socket: 客户端套接字
            address: 客户端地址
        """
        username = None
        
        try:
            # 等待客户端发送用户名
            message = SocketUtils.receive_message(client_socket)
            if not message or message.get("type") != MessageType.USER_JOIN:
                print(f"客户端 {address} 未发送有效的用户名")
                return
            
            username = message.get("data", f"User_{address[1]}")
            
            # 添加客户端到管理列表
            with self.clients_lock:
                self.clients[client_socket] = {
                    "username": username,
                    "address": address
                }
            
            print(f"用户 '{username}' 已加入聊天室 (来自 {address})")
            
            # 广播用户加入消息
            self.broadcast_message(
                MessageType.USER_JOIN,
                f"用户 '{username}' 加入了聊天室",
                exclude_socket=client_socket
            )
            
            # 发送欢迎消息给新用户
            welcome_msg = f"欢迎加入聊天室！当前在线用户数: {len(self.clients)}"
            SocketUtils.send_message(client_socket, MessageType.TEXT, welcome_msg)
            
            # 处理客户端消息
            while self.running:
                message = SocketUtils.receive_message(client_socket)
                if not message:
                    break
                
                self.process_message(client_socket, message, username)
                
        except Exception as e:
            print(f"处理客户端 {address} 时发生错误: {e}")
        finally:
            # 客户端断开连接
            self.disconnect_client(client_socket, username)
    
    def process_message(self, sender_socket, message, username):
        """
        处理客户端发送的消息
        
        Args:
            sender_socket: 发送者套接字
            message: 消息内容
            username: 发送者用户名
        """
        try:
            msg_type = message.get("type")
            data = message.get("data", "")
            metadata = message.get("metadata", {})
            
            if msg_type == MessageType.TEXT:
                # 处理文本消息
                formatted_msg = format_message(username, data)
                print(formatted_msg)
                
                # 广播给其他客户端
                self.broadcast_message(
                    MessageType.TEXT,
                    formatted_msg,
                    exclude_socket=sender_socket
                )
            
            elif msg_type == MessageType.FILE:
                # 处理文件传输开始
                filename = metadata.get("filename", "unknown_file")
                file_size = metadata.get("size", 0)
                
                print(f"用户 '{username}' 开始发送文件: {filename} ({file_size} 字节)")
                
                # 在服务器端保存文件的准备工作
                self.prepare_file_reception(sender_socket, filename, file_size, username)
                
                # 转发文件信息给其他客户端
                self.broadcast_message(
                    MessageType.FILE,
                    data,
                    metadata,
                    exclude_socket=sender_socket
                )
            
            elif msg_type == MessageType.FILE_DATA:
                # 保存文件数据到服务器
                self.save_file_chunk(sender_socket, data)
                
                # 转发文件数据
                self.broadcast_message(
                    MessageType.FILE_DATA,
                    data,
                    metadata,
                    exclude_socket=sender_socket
                )
            
            elif msg_type == MessageType.FILE_COMPLETE:
                # 文件传输完成
                filename = metadata.get("filename", "unknown_file")
                saved_path = self.complete_file_reception(sender_socket)
                
                if saved_path:
                    print(f"✅ 用户 '{username}' 完成文件发送: {filename}")
                    print(f"📁 文件已保存到: {saved_path}")
                else:
                    print(f"❌ 用户 '{username}' 文件发送失败: {filename}")
                
                # 转发完成信号
                self.broadcast_message(
                    MessageType.FILE_COMPLETE,
                    data,
                    metadata,
                    exclude_socket=sender_socket
                )
            
        except Exception as e:
            print(f"处理消息时发生错误: {e}")
    
    def broadcast_message(self, msg_type, data, metadata=None, exclude_socket=None):
        """
        广播消息给所有客户端
        
        Args:
            msg_type: 消息类型
            data: 消息数据
            metadata: 消息元数据
            exclude_socket: 排除的套接字（不发送给该套接字）
        """
        with self.clients_lock:
            disconnected_clients = []
            
            for client_socket in self.clients:
                if client_socket == exclude_socket:
                    continue
                
                try:
                    SocketUtils.send_message(client_socket, msg_type, data, metadata)
                except Exception as e:
                    print(f"发送消息给客户端失败: {e}")
                    disconnected_clients.append(client_socket)
            
            # 移除断开连接的客户端
            for client_socket in disconnected_clients:
                username = self.clients.get(client_socket, {}).get("username", "Unknown")
                self.disconnect_client(client_socket, username)
    
    def disconnect_client(self, client_socket, username):
        """
        断开客户端连接
        
        Args:
            client_socket: 客户端套接字
            username: 用户名
        """
        try:
            with self.clients_lock:
                if client_socket in self.clients:
                    del self.clients[client_socket]
            
            client_socket.close()
            
            if username:
                print(f"用户 '{username}' 已离开聊天室")
                
                # 广播用户离开消息
                self.broadcast_message(
                    MessageType.USER_LEAVE,
                    f"用户 '{username}' 离开了聊天室"
                )
        except Exception as e:
            print(f"断开客户端连接时发生错误: {e}")
    
    def get_online_users(self):
        """获取在线用户列表"""
        with self.clients_lock:
            return [client_info["username"] for client_info in self.clients.values()]
    
    def find_user_socket(self, username):
        """
        根据用户名查找对应的套接字
        
        Args:
            username: 用户名
            
        Returns:
            对应的套接字，如果未找到返回None
        """
        with self.clients_lock:
            for socket, client_info in self.clients.items():
                if client_info["username"] == username:
                    return socket
        return None
    
    def send_to_user(self, username, msg_type, data, metadata=None):
        """
        向指定用户发送消息
        
        Args:
            username: 目标用户名
            msg_type: 消息类型
            data: 消息数据
            metadata: 元数据
            
        Returns:
            是否发送成功
        """
        user_socket = self.find_user_socket(username)
        if not user_socket:
            return False
        
        try:
            SocketUtils.send_message(user_socket, msg_type, data, metadata)
            return True
        except Exception as e:
            print(f"向用户 {username} 发送消息失败: {e}")
            return False
    
    def handle_server_input(self):
        """处理服务器管理员输入"""
        try:
            while self.running:
                try:
                    command = input().strip()
                    if command:
                        self.process_server_command(command)
                except EOFError:
                    break
                except KeyboardInterrupt:
                    self.stop()
                    break
        except Exception as e:
            print(f"服务器输入处理错误: {e}")
    
    def process_server_command(self, command):
        """
        处理服务器命令
        
        Args:
            command: 服务器输入的命令
        """
        try:
            if command.lower() == '/quit':
                print("正在关闭服务器...")
                self.stop()
                
            elif command.lower() == '/help':
                print("\n服务器管理命令:")
                print("  /msg <消息内容> - 向所有客户端广播消息")
                print("  /msg @用户名 <消息内容> - 向指定用户发送私信")
                print("  /send <文件路径> - 向所有客户端广播文件")
                print("  /send @用户名 <文件路径> - 向指定用户发送文件")
                print("  /list - 显示在线用户列表")
                print("  /user <用户名> - 显示用户详细信息")
                print("  /help - 显示帮助信息")
                print("  /quit - 关闭服务器\n")
                
            elif command.lower() == '/list':
                self.show_online_users()
                
            elif command.lower().startswith('/user '):
                # 显示特定用户信息
                username = command[6:].strip()
                if username:
                    self.show_user_info(username)
                else:
                    print("请指定要查看的用户名: /user 用户名")
                
            elif command.lower().startswith('/msg '):
                # 发送消息给所有客户端或指定用户
                message = command[5:].strip()
                if message:
                    # 检查是否是定向消息 (@username message)
                    if message.startswith('@'):
                        parts = message.split(' ', 1)
                        if len(parts) >= 2:
                            target_user = parts[0][1:]  # 去掉@符号
                            actual_message = parts[1]
                            
                            # 发送给指定用户
                            server_msg = format_message("服务器", f"[私信] {actual_message}")
                            if self.send_to_user(target_user, MessageType.TEXT, server_msg):
                                print(f"✅ 已向用户 '{target_user}' 发送私信: {actual_message}")
                            else:
                                print(f"❌ 用户 '{target_user}' 不在线或不存在")
                        else:
                            print("私信格式: /msg @用户名 消息内容")
                    else:
                        # 广播消息
                        server_msg = format_message("服务器", message)
                        print(server_msg)
                        self.broadcast_message(MessageType.TEXT, server_msg)
                else:
                    print("请输入要发送的消息内容")
                    print("格式: /msg 消息内容 (广播)")
                    print("格式: /msg @用户名 消息内容 (私信)")
                    
            elif command.lower().startswith('/send '):
                # 发送文件给所有客户端或指定用户
                params = command[6:].strip()
                if params:
                    # 检查是否是定向发送 (@username filepath)
                    if params.startswith('@'):
                        parts = params.split(' ', 1)
                        if len(parts) >= 2:
                            target_user = parts[0][1:]  # 去掉@符号
                            file_path = parts[1].strip()
                            
                            # 去除可能的引号
                            if file_path.startswith('"') and file_path.endswith('"'):
                                file_path = file_path[1:-1]
                            elif file_path.startswith("'") and file_path.endswith("'"):
                                file_path = file_path[1:-1]
                            
                            # 发送给指定用户
                            self.send_file_to_user(target_user, file_path)
                        else:
                            print("定向发送格式: /send @用户名 文件路径")
                    else:
                        # 广播文件
                        file_path = params
                        # 去除可能的引号
                        if file_path.startswith('"') and file_path.endswith('"'):
                            file_path = file_path[1:-1]
                        elif file_path.startswith("'") and file_path.endswith("'"):
                            file_path = file_path[1:-1]
                        
                        self.send_file_to_all_clients(file_path)
                else:
                    print("请指定要发送的文件路径")
                    print("格式: /send 文件路径 (广播)")
                    print("格式: /send @用户名 文件路径 (定向发送)")
                    
            elif command.startswith('/'):
                print(f"未知命令: {command}，输入 /help 查看可用命令")
                
        except Exception as e:
            print(f"处理服务器命令时发生错误: {e}")
    
    def send_file_to_all_clients(self, file_path):
        """
        向所有客户端发送文件
        
        Args:
            file_path: 文件路径
        """
        try:
            if not os.path.exists(file_path):
                print(f"文件不存在: {file_path}")
                return
            
            if not os.path.isfile(file_path):
                print(f"指定路径不是文件: {file_path}")
                return
            
            filename = os.path.basename(file_path)
            file_size = os.path.getsize(file_path)
            
            print(f"开始向所有客户端发送文件: {filename} ({file_size} 字节)")
            
            # 发送文件信息
            file_info = {
                "filename": filename,
                "size": file_size,
                "sender": "服务器"
            }
            
            self.broadcast_message(MessageType.FILE, "", file_info)
            
            # 发送文件数据
            import time
            start_time = time.time()
            last_update_time = start_time
            
            with open(file_path, 'rb') as f:
                bytes_sent = 0
                chunk_count = 0
                
                while bytes_sent < file_size:
                    chunk = f.read(SocketUtils.BUFFER_SIZE)
                    if not chunk:
                        break
                    
                    # 发送文件数据块
                    self.broadcast_message(MessageType.FILE_DATA, chunk.hex(), {
                        "bytes_sent": bytes_sent,
                        "total_size": file_size,
                        "chunk_index": chunk_count
                    })
                    
                    bytes_sent += len(chunk)
                    chunk_count += 1
                    
                    # 显示进度
                    current_time = time.time()
                    if file_size > 0 and (current_time - last_update_time >= 0.1 or bytes_sent >= file_size):
                        progress = (bytes_sent / file_size) * 100
                        elapsed_time = current_time - start_time
                        
                        if elapsed_time > 0:
                            speed = bytes_sent / elapsed_time
                            speed_str = SocketUtils.format_transfer_speed(speed)
                            progress_bar = SocketUtils.create_progress_bar(progress)
                            
                            print(f"\r{progress_bar} {progress:.1f}% | {speed_str}", end="", flush=True)
                            
                            last_update_time = current_time
            
            # 发送文件传输完成信号
            self.broadcast_message(MessageType.FILE_COMPLETE, "", {
                "filename": filename,
                "total_size": file_size
            })
            
            print(f"\n文件 '{filename}' 发送完成")
            
        except Exception as e:
            print(f"发送文件失败: {e}")
    
    def send_file_to_user(self, username, file_path):
        """
        向指定用户发送文件
        
        Args:
            username: 目标用户名
            file_path: 文件路径
        """
        try:
            # 检查用户是否在线
            user_socket = self.find_user_socket(username)
            if not user_socket:
                print(f"❌ 用户 '{username}' 不在线或不存在")
                return
            
            if not os.path.exists(file_path):
                print(f"❌ 文件不存在: {file_path}")
                return
            
            if not os.path.isfile(file_path):
                print(f"❌ 指定路径不是文件: {file_path}")
                return
            
            filename = os.path.basename(file_path)
            file_size = os.path.getsize(file_path)
            
            print(f"📤 开始向用户 '{username}' 发送文件: {filename} ({file_size} 字节)")
            
            # 发送文件信息
            file_info = {
                "filename": filename,
                "size": file_size,
                "sender": "服务器"
            }
            
            if not self.send_to_user(username, MessageType.FILE, "", file_info):
                print(f"❌ 向用户 '{username}' 发送文件信息失败")
                return
            
            # 发送文件数据
            with open(file_path, 'rb') as f:
                bytes_sent = 0
                while bytes_sent < file_size:
                    chunk = f.read(SocketUtils.BUFFER_SIZE)
                    if not chunk:
                        break
                    
                    # 发送文件数据块
                    if not self.send_to_user(username, MessageType.FILE_DATA, chunk.hex(), {
                        "bytes_sent": bytes_sent,
                        "total_size": file_size
                    }):
                        print(f"❌ 向用户 '{username}' 发送文件数据失败")
                        return
                    
                    bytes_sent += len(chunk)
                    
                    # 显示进度
                    if file_size > 0:
                        progress = (bytes_sent / file_size) * 100
                        print(f"\r向 {username} 发送进度: {progress:.1f}%", end="", flush=True)
            
            # 发送文件传输完成信号
            if self.send_to_user(username, MessageType.FILE_COMPLETE, "", {
                "filename": filename,
                "total_size": file_size
            }):
                print(f"\n✅ 文件 '{filename}' 已成功发送给用户 '{username}'")
            else:
                print(f"\n❌ 向用户 '{username}' 发送文件完成信号失败")
            
        except Exception as e:
            print(f"❌ 向用户发送文件失败: {e}")
    
    def show_online_users(self):
        """显示在线用户详细信息"""
        with self.clients_lock:
            users_info = []
            for socket, client_info in self.clients.items():
                users_info.append({
                    'username': client_info['username'],
                    'address': client_info['address'],
                    'socket': socket
                })
        
        print(f"\n📋 在线用户列表 ({len(users_info)}):")
        if users_info:
            for i, user_info in enumerate(users_info, 1):
                address = f"{user_info['address'][0]}:{user_info['address'][1]}"
                print(f"  {i}. {user_info['username']} ({address})")
        else:
            print("  暂无在线用户")
        print()
    
    def show_user_info(self, username):
        """
        显示指定用户的详细信息
        
        Args:
            username: 用户名
        """
        user_socket = self.find_user_socket(username)
        if user_socket:
            with self.clients_lock:
                client_info = self.clients.get(user_socket)
                if client_info:
                    print(f"\n👤 用户信息:")
                    print(f"  用户名: {client_info['username']}")
                    print(f"  IP地址: {client_info['address'][0]}")
                    print(f"  端口: {client_info['address'][1]}")
                    print(f"  连接状态: 在线")
                    
                    # 检查是否有正在进行的文件传输
                    if user_socket in self.file_transfers:
                        transfer_info = self.file_transfers[user_socket]
                        progress = (transfer_info['received'] / transfer_info['expected_size']) * 100 if transfer_info['expected_size'] > 0 else 0
                        print(f"  文件传输: 正在接收 {transfer_info['filename']} ({progress:.1f}%)")
                    
                    print()
        else:
            print(f"❌ 用户 '{username}' 不在线或不存在")
    
    def find_users_by_pattern(self, pattern):
        """
        根据模式搜索用户
        
        Args:
            pattern: 搜索模式
            
        Returns:
            匹配的用户名列表
        """
        with self.clients_lock:
            matching_users = []
            for client_info in self.clients.values():
                username = client_info['username']
                if pattern.lower() in username.lower():
                    matching_users.append(username)
            return matching_users
    
    def prepare_file_reception(self, client_socket, filename, file_size, username):
        """
        准备接收文件
        
        Args:
            client_socket: 客户端套接字
            filename: 文件名
            file_size: 文件大小
            username: 发送者用户名
        """
        try:
            import time
            # 确保接收目录存在
            os.makedirs(self.files_dir, exist_ok=True)
            
            # 生成唯一的文件路径
            base_name, ext = os.path.splitext(filename)
            file_path = os.path.join(self.files_dir, f"{username}_{filename}")
            
            # 如果文件已存在，添加数字后缀
            counter = 1
            while os.path.exists(file_path):
                file_path = os.path.join(self.files_dir, f"{username}_{base_name}_{counter}{ext}")
                counter += 1
            
            # 打开文件准备写入
            file_handle = open(file_path, 'wb')
            
            print(f"📥 开始接收文件: {filename}")
            print(f"👤 发送者: {username}")
            print(f"📊 文件大小: {SocketUtils.format_file_size(file_size)}")
            
            self.file_transfers[client_socket] = {
                "file_handle": file_handle,
                "filename": filename,
                "file_path": file_path,
                "expected_size": file_size,
                "received": 0,
                "username": username,
                "start_time": time.time(),
                "last_update": time.time(),
                "chunk_count": 0
            }
            
        except Exception as e:
            print(f"准备文件接收失败: {e}")
    
    def save_file_chunk(self, client_socket, hex_data):
        """
        保存文件数据块
        
        Args:
            client_socket: 客户端套接字
            hex_data: 十六进制编码的文件数据
        """
        try:
            import time
            if client_socket not in self.file_transfers:
                return
            
            transfer_info = self.file_transfers[client_socket]
            file_handle = transfer_info["file_handle"]
            
            # 将十六进制数据转换为字节
            chunk = bytes.fromhex(hex_data)
            file_handle.write(chunk)
            
            transfer_info["received"] += len(chunk)
            transfer_info["chunk_count"] += 1
            
            # 显示接收进度（每0.1秒更新一次）
            current_time = time.time()
            if (transfer_info["expected_size"] > 0 and 
                current_time - transfer_info["last_update"] >= 0.1):
                
                progress = (transfer_info["received"] / transfer_info["expected_size"]) * 100
                elapsed_time = current_time - transfer_info["start_time"]
                
                if elapsed_time > 0:
                    speed = transfer_info["received"] / elapsed_time
                    speed_str = SocketUtils.format_transfer_speed(speed)
                    progress_bar = SocketUtils.create_progress_bar(progress)
                    
                    print(f"\r{progress_bar} {progress:.1f}% | {speed_str} | 来自 {transfer_info['username']}", 
                          end="", flush=True)
                    
                    transfer_info["last_update"] = current_time
            
        except Exception as e:
            print(f"保存文件数据块失败: {e}")
    
    def complete_file_reception(self, client_socket):
        """
        完成文件接收
        
        Args:
            client_socket: 客户端套接字
            
        Returns:
            保存的文件路径，失败返回None
        """
        try:
            import time
            if client_socket not in self.file_transfers:
                return None
            
            transfer_info = self.file_transfers[client_socket]
            file_handle = transfer_info["file_handle"]
            file_path = transfer_info["file_path"]
            
            # 计算传输统计
            end_time = time.time()
            total_time = end_time - transfer_info["start_time"]
            
            # 关闭文件
            file_handle.close()
            
            # 显示传输统计
            print()  # 换行，结束进度显示
            print(f"✅ 文件接收完成: {transfer_info['filename']}")
            print(f"💾 保存位置: {file_path}")
            print(f"⏱️  接收时间: {SocketUtils.format_time(total_time)}")
            
            if total_time > 0:
                avg_speed = transfer_info["received"] / total_time
                print(f"🚀 平均速度: {SocketUtils.format_transfer_speed(avg_speed)}")
            
            print(f"📦 数据块数: {transfer_info['chunk_count']}")
            
            # 清理传输信息
            del self.file_transfers[client_socket]
            
            return file_path
            
        except Exception as e:
            print(f"完成文件接收失败: {e}")
            return None


def main():
    """主函数"""
    host = 'localhost'
    port = 8888
    
    # 处理命令行参数
    if len(sys.argv) >= 2:
        try:
            port = int(sys.argv[1])
        except ValueError:
            print("端口号必须是数字")
            return
    
    if len(sys.argv) >= 3:
        host = sys.argv[2]
    
    # 创建并启动服务器
    server = ChatServer(host, port)
    
    try:
        server.start()
    except KeyboardInterrupt:
        print("\n正在关闭服务器...")
        server.stop()
    except Exception as e:
        print(f"服务器运行错误: {e}")
        server.stop()


if __name__ == "__main__":
    main()