"""
套接字聊天客户端
支持连接服务器、发送文本消息和文件传输
"""

import socket
import threading
import sys
import os
from utils import SocketUtils, MessageType, is_valid_file_path


class ChatClient:
    def __init__(self, host='localhost', port=8888, username=None):
        """
        初始化聊天客户端
        
        Args:
            host: 服务器主机地址
            port: 服务器端口
            username: 用户名
        """
        self.host = host
        self.port = port
        self.username = username or input("请输入您的用户名: ").strip()
        
        if not self.username:
            self.username = f"User_{os.getpid()}"
        
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.connected = False
        
        # 文件接收目录
        self.downloads_dir = os.path.join(os.path.dirname(__file__), 'files', 'downloads')
        os.makedirs(self.downloads_dir, exist_ok=True)
    
    def connect(self):
        """连接到服务器"""
        try:
            self.socket.connect((self.host, self.port))
            self.connected = True
            
            # 发送用户名到服务器
            SocketUtils.send_message(self.socket, MessageType.USER_JOIN, self.username)
            
            print(f"已连接到服务器 {self.host}:{self.port}")
            print(f"用户名: {self.username}")
            print("\n聊天室命令:")
            print("  /send <文件路径> - 发送文件")
            print("  /help - 显示帮助信息")
            print("  /quit - 退出聊天室")
            print("  直接输入文本发送消息\n")
            
            return True
            
        except Exception as e:
            print(f"连接服务器失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        self.connected = False
        try:
            self.socket.close()
        except:
            pass
        print("已断开与服务器的连接")
    
    def start_receiving(self):
        """启动消息接收线程"""
        receive_thread = threading.Thread(target=self.receive_messages)
        receive_thread.daemon = True
        receive_thread.start()
    
    def receive_messages(self):
        """接收服务器消息"""
        current_file = None
        file_handle = None
        
        try:
            while self.connected:
                message = SocketUtils.receive_message(self.socket)
                if not message:
                    break
                
                msg_type = message.get("type")
                data = message.get("data", "")
                metadata = message.get("metadata", {})
                
                if msg_type == MessageType.TEXT:
                    print(data)
                
                elif msg_type == MessageType.USER_JOIN or msg_type == MessageType.USER_LEAVE:
                    print(f"[系统消息] {data}")
                
                elif msg_type == MessageType.FILE:
                    # 开始接收文件
                    import time
                    filename = metadata.get("filename", "unknown_file")
                    file_size = metadata.get("size", 0)
                    sender = metadata.get("sender", "Unknown")
                    
                    print(f"\n📥 接收文件: {filename}")
                    print(f"👤 发送者: {sender}")
                    print(f"📊 文件大小: {SocketUtils.format_file_size(file_size)}")
                    
                    # 准备接收文件
                    file_path = os.path.join(self.downloads_dir, filename)
                    
                    # 如果文件已存在，添加数字后缀
                    counter = 1
                    base_name, ext = os.path.splitext(file_path)
                    while os.path.exists(file_path):
                        file_path = f"{base_name}_{counter}{ext}"
                        counter += 1
                    
                    current_file = {
                        "path": file_path,
                        "size": file_size,
                        "received": 0,
                        "sender": sender,
                        "filename": filename,
                        "start_time": time.time(),
                        "last_update": time.time(),
                        "chunk_count": 0
                    }
                    
                    file_handle = open(file_path, 'wb')
                
                elif msg_type == MessageType.FILE_DATA and current_file and file_handle:
                    # 接收文件数据
                    import time
                    chunk_hex = data
                    chunk = bytes.fromhex(chunk_hex)
                    file_handle.write(chunk)
                    current_file["received"] += len(chunk)
                    current_file["chunk_count"] += 1
                    
                    # 显示接收进度（每0.1秒更新一次）
                    current_time = time.time()
                    if current_file["size"] > 0 and (current_time - current_file["last_update"] >= 0.1):
                        progress = (current_file["received"] / current_file["size"]) * 100
                        elapsed_time = current_time - current_file["start_time"]
                        
                        if elapsed_time > 0:
                            speed = current_file["received"] / elapsed_time
                            speed_str = SocketUtils.format_transfer_speed(speed)
                            progress_bar = SocketUtils.create_progress_bar(progress)
                            
                            print(f"\r{progress_bar} {progress:.1f}% | {speed_str} | {SocketUtils.format_file_size(current_file['received'])}/{SocketUtils.format_file_size(current_file['size'])}", 
                                  end="", flush=True)
                            
                            current_file["last_update"] = current_time
                
                elif msg_type == MessageType.FILE_COMPLETE and current_file and file_handle:
                    # 文件接收完成
                    import time
                    file_handle.close()
                    
                    end_time = time.time()
                    total_time = end_time - current_file["start_time"]
                    transfer_time = metadata.get("transfer_time", total_time)
                    
                    print()  # 换行
                    print(f"✅ 文件接收完成: {current_file['filename']}")
                    print(f"💾 保存位置: {current_file['path']}")
                    print(f"⏱️  接收时间: {SocketUtils.format_time(total_time)}")
                    
                    if total_time > 0:
                        avg_speed = current_file["received"] / total_time
                        print(f"🚀 平均速度: {SocketUtils.format_transfer_speed(avg_speed)}")
                    
                    print(f"📦 数据块数: {current_file['chunk_count']}")
                    
                    current_file = None
                    file_handle = None
                
                elif msg_type == MessageType.ERROR:
                    print(f"[错误] {data}")
                    
        except Exception as e:
            if self.connected:
                print(f"接收消息时发生错误: {e}")
        finally:
            if file_handle:
                file_handle.close()
    
    def send_text_message(self, message):
        """
        发送文本消息
        
        Args:
            message: 消息内容
        """
        try:
            SocketUtils.send_message(self.socket, MessageType.TEXT, message)
        except Exception as e:
            print(f"发送消息失败: {e}")
    
    def send_file(self, file_path):
        """
        发送文件
        
        Args:
            file_path: 文件路径
        """
        try:
            # 处理相对路径
            if not os.path.isabs(file_path):
                file_path = os.path.abspath(file_path)
            
            if not is_valid_file_path(file_path):
                print(f"文件不存在或无法访问: {file_path}")
                print(f"当前工作目录: {os.getcwd()}")
                return False
            
            print(f"开始发送文件: {os.path.basename(file_path)}")
            print(f"文件大小: {os.path.getsize(file_path)} 字节")
            
            SocketUtils.send_file(self.socket, file_path, self.username)
            print(f"✅ 文件 '{os.path.basename(file_path)}' 发送成功")
            return True
            
        except FileNotFoundError as e:
            print(f"❌ 文件未找到: {e}")
            return False
        except PermissionError as e:
            print(f"❌ 文件访问权限不足: {e}")
            return False
        except Exception as e:
            print(f"❌ 发送文件失败: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def process_command(self, command):
        """
        处理用户命令
        
        Args:
            command: 用户输入的命令
            
        Returns:
            是否继续运行
        """
        command = command.strip()
        
        if command.lower() == '/quit':
            return False
        
        elif command.lower() == '/help':
            print("\n聊天室命令:")
            print("  /send <文件路径> - 发送文件")
            print("  /help - 显示帮助信息")
            print("  /quit - 退出聊天室")
            print("  直接输入文本发送消息\n")
        
        elif command.lower().startswith('/send '):
            # 发送文件命令
            file_path = command[6:].strip()
            if file_path:
                # 去除可能的引号
                if file_path.startswith('"') and file_path.endswith('"'):
                    file_path = file_path[1:-1]
                elif file_path.startswith("'") and file_path.endswith("'"):
                    file_path = file_path[1:-1]
                
                self.send_file(file_path)
            else:
                print("请指定要发送的文件路径，例如: /send /path/to/file.txt")
        
        elif command.startswith('/'):
            print(f"未知命令: {command}，输入 /help 查看可用命令")
        
        else:
            # 发送普通文本消息
            if command:
                self.send_text_message(command)
        
        return True
    
    def run(self):
        """运行客户端"""
        if not self.connect():
            return
        
        # 启动消息接收线程
        self.start_receiving()
        
        try:
            # 主循环 - 处理用户输入
            while self.connected:
                try:
                    user_input = input()
                    if not self.process_command(user_input):
                        break
                except EOFError:
                    break
                except KeyboardInterrupt:
                    print("\n正在退出...")
                    break
                    
        except Exception as e:
            print(f"客户端运行错误: {e}")
        finally:
            self.disconnect()


def main():
    """主函数"""
    host = 'localhost'
    port = 8888
    username = None
    
    # 处理命令行参数
    if len(sys.argv) >= 2:
        try:
            port = int(sys.argv[1])
        except ValueError:
            print("端口号必须是数字")
            return
    
    if len(sys.argv) >= 3:
        host = sys.argv[2]
    
    if len(sys.argv) >= 4:
        username = sys.argv[3]
    
    # 创建并运行客户端
    client = ChatClient(host, port, username)
    
    try:
        client.run()
    except Exception as e:
        print(f"客户端错误: {e}")


if __name__ == "__main__":
    main()