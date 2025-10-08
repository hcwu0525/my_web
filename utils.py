"""
套接字编程工具模块
包含文件传输和消息处理的共用功能
"""

import json
import struct
import os
from typing import Dict, Any, Optional


class MessageType:
    """消息类型常量"""
    TEXT = "TEXT"
    FILE = "FILE"
    FILE_REQUEST = "FILE_REQUEST"
    FILE_DATA = "FILE_DATA"
    FILE_COMPLETE = "FILE_COMPLETE"
    USER_JOIN = "USER_JOIN"
    USER_LEAVE = "USER_LEAVE"
    ERROR = "ERROR"


class SocketUtils:
    """套接字工具类（增强版）"""
    
    BUFFER_SIZE = 8192  # 默认缓冲区大小8KB
    MAX_BUFFER_SIZE = 64 * 1024  # 最大缓冲区大小64KB
    MIN_BUFFER_SIZE = 4 * 1024   # 最小缓冲区大小4KB
    
    @staticmethod
    def get_optimal_buffer_size(file_size: int) -> int:
        """
        根据文件大小获取最优缓冲区大小
        
        Args:
            file_size: 文件大小
            
        Returns:
            最优缓冲区大小
        """
        if file_size < 1024 * 1024:  # < 1MB
            return SocketUtils.MIN_BUFFER_SIZE
        elif file_size < 10 * 1024 * 1024:  # < 10MB
            return SocketUtils.BUFFER_SIZE
        elif file_size < 100 * 1024 * 1024:  # < 100MB
            return 32 * 1024  # 32KB
        else:  # >= 100MB
            return SocketUtils.MAX_BUFFER_SIZE
    
    @staticmethod
    def send_message(sock, message_type: str, data: Any, metadata: Optional[Dict] = None):
        """
        发送消息到套接字
        
        Args:
            sock: 套接字对象
            message_type: 消息类型
            data: 消息数据
            metadata: 元数据
        """
        try:
            message = {
                "type": message_type,
                "data": data,
                "metadata": metadata or {}
            }
            
            # 将消息序列化为JSON
            json_message = json.dumps(message, ensure_ascii=False)
            message_bytes = json_message.encode('utf-8')
            
            # 发送消息长度（4字节）
            message_length = len(message_bytes)
            sock.send(struct.pack('!I', message_length))
            
            # 发送消息内容
            sock.send(message_bytes)
            
        except Exception as e:
            print(f"发送消息失败: {e}")
            raise
    
    @staticmethod
    def receive_message(sock) -> Optional[Dict[str, Any]]:
        """
        从套接字接收消息
        
        Args:
            sock: 套接字对象
            
        Returns:
            解析后的消息字典，如果连接断开返回None
        """
        try:
            # 接收消息长度（4字节）
            length_data = SocketUtils._receive_all(sock, 4)
            if not length_data:
                return None
            
            message_length = struct.unpack('!I', length_data)[0]
            
            # 接收消息内容
            message_data = SocketUtils._receive_all(sock, message_length)
            if not message_data:
                return None
            
            # 解析JSON消息
            json_message = message_data.decode('utf-8')
            message = json.loads(json_message)
            
            return message
            
        except Exception as e:
            print(f"接收消息失败: {e}")
            return None
    
    @staticmethod
    def _receive_all(sock, length: int) -> bytes:
        """
        确保接收指定长度的数据
        
        Args:
            sock: 套接字对象
            length: 要接收的数据长度
            
        Returns:
            接收到的数据
        """
        data = b''
        while len(data) < length:
            packet = sock.recv(length - len(data))
            if not packet:
                break
            data += packet
        return data
    
    @staticmethod
    def send_file(sock, file_path: str, username: str = "", show_progress: bool = True):
        """
        发送文件到套接字（带进度显示和传输统计）
        
        Args:
            sock: 套接字对象
            file_path: 文件路径
            username: 发送者用户名
            show_progress: 是否显示进度
        """
        import time
        
        try:
            if not os.path.exists(file_path):
                raise FileNotFoundError(f"文件不存在: {file_path}")
            
            filename = os.path.basename(file_path)
            file_size = os.path.getsize(file_path)
            
            # 发送文件信息
            file_info = {
                "filename": filename,
                "size": file_size,
                "sender": username
            }
            
            SocketUtils.send_message(sock, MessageType.FILE, "", file_info)
            
            # 记录开始时间
            start_time = time.time()
            last_update_time = start_time
            
            if show_progress:
                print(f"📤 开始发送文件: {filename}")
                print(f"📊 文件大小: {SocketUtils.format_file_size(file_size)}")
            
            # 发送文件数据
            with open(file_path, 'rb') as f:
                bytes_sent = 0
                chunk_count = 0
                
                while bytes_sent < file_size:
                    chunk = f.read(SocketUtils.BUFFER_SIZE)
                    if not chunk:
                        break
                    
                    # 发送文件数据块
                    SocketUtils.send_message(sock, MessageType.FILE_DATA, chunk.hex(), {
                        "bytes_sent": bytes_sent,
                        "total_size": file_size,
                        "chunk_index": chunk_count
                    })
                    
                    bytes_sent += len(chunk)
                    chunk_count += 1
                    
                    # 显示进度（每0.1秒更新一次或传输完成）
                    current_time = time.time()
                    if show_progress and (current_time - last_update_time >= 0.1 or bytes_sent >= file_size):
                        elapsed_time = current_time - start_time
                        if elapsed_time > 0:
                            speed = bytes_sent / elapsed_time
                            progress = (bytes_sent / file_size) * 100
                            
                            speed_str = SocketUtils.format_transfer_speed(speed)
                            progress_bar = SocketUtils.create_progress_bar(progress)
                            
                            print(f"\r{progress_bar} {progress:.1f}% | {speed_str} | {SocketUtils.format_file_size(bytes_sent)}/{SocketUtils.format_file_size(file_size)}", 
                                  end="", flush=True)
                            
                            last_update_time = current_time
            
            # 发送文件传输完成信号
            end_time = time.time()
            total_time = end_time - start_time
            
            SocketUtils.send_message(sock, MessageType.FILE_COMPLETE, "", {
                "filename": filename,
                "total_size": file_size,
                "transfer_time": total_time,
                "chunk_count": chunk_count
            })
            
            if show_progress:
                print()  # 换行
                avg_speed = file_size / total_time if total_time > 0 else 0
                print(f"✅ 文件发送完成: {filename}")
                print(f"⏱️  传输时间: {SocketUtils.format_time(total_time)}")
                print(f"🚀 平均速度: {SocketUtils.format_transfer_speed(avg_speed)}")
                print(f"📦 数据块数: {chunk_count}")
            
        except Exception as e:
            if show_progress:
                print(f"\n❌ 发送文件失败: {e}")
            raise
    
    @staticmethod
    def receive_file(sock, save_dir: str = "./files") -> Optional[str]:
        """
        从套接字接收文件
        
        Args:
            sock: 套接字对象
            save_dir: 保存目录
            
        Returns:
            保存的文件路径，失败返回None
        """
        try:
            # 确保保存目录存在
            os.makedirs(save_dir, exist_ok=True)
            
            file_info = None
            file_path = None
            file_handle = None
            bytes_received = 0
            
            while True:
                message = SocketUtils.receive_message(sock)
                if not message:
                    break
                
                msg_type = message.get("type")
                
                if msg_type == MessageType.FILE:
                    # 接收文件信息
                    file_info = message.get("metadata", {})
                    filename = file_info.get("filename", "unknown_file")
                    file_path = os.path.join(save_dir, filename)
                    
                    # 如果文件已存在，添加数字后缀
                    counter = 1
                    base_name, ext = os.path.splitext(file_path)
                    while os.path.exists(file_path):
                        file_path = f"{base_name}_{counter}{ext}"
                        counter += 1
                    
                    file_handle = open(file_path, 'wb')
                    print(f"开始接收文件: {filename}")
                
                elif msg_type == MessageType.FILE_DATA and file_handle:
                    # 接收文件数据
                    chunk_hex = message.get("data", "")
                    chunk = bytes.fromhex(chunk_hex)
                    file_handle.write(chunk)
                    bytes_received += len(chunk)
                    
                    # 显示进度
                    total_size = file_info.get("size", 0)
                    if total_size > 0:
                        progress = (bytes_received / total_size) * 100
                        print(f"\r接收进度: {progress:.1f}%", end="", flush=True)
                
                elif msg_type == MessageType.FILE_COMPLETE:
                    # 文件传输完成
                    if file_handle:
                        file_handle.close()
                        print(f"\n文件接收完成: {file_path}")
                        return file_path
                    break
            
            return None
            
        except Exception as e:
            print(f"接收文件失败: {e}")
            if file_handle:
                file_handle.close()
            return None
    
    @staticmethod
    def format_file_size(size_bytes: int) -> str:
        """
        格式化文件大小显示
        
        Args:
            size_bytes: 文件大小（字节）
            
        Returns:
            格式化后的文件大小字符串
        """
        if size_bytes < 1024:
            return f"{size_bytes} B"
        elif size_bytes < 1024 * 1024:
            return f"{size_bytes / 1024:.1f} KB"
        elif size_bytes < 1024 * 1024 * 1024:
            return f"{size_bytes / (1024 * 1024):.1f} MB"
        else:
            return f"{size_bytes / (1024 * 1024 * 1024):.1f} GB"
    
    @staticmethod
    def format_transfer_speed(speed_bps: float) -> str:
        """
        格式化传输速度显示
        
        Args:
            speed_bps: 传输速度（字节/秒）
            
        Returns:
            格式化后的传输速度字符串
        """
        if speed_bps < 1024:
            return f"{speed_bps:.1f} B/s"
        elif speed_bps < 1024 * 1024:
            return f"{speed_bps / 1024:.1f} KB/s"
        elif speed_bps < 1024 * 1024 * 1024:
            return f"{speed_bps / (1024 * 1024):.1f} MB/s"
        else:
            return f"{speed_bps / (1024 * 1024 * 1024):.1f} GB/s"
    
    @staticmethod
    def format_time(seconds: float) -> str:
        """
        格式化时间显示
        
        Args:
            seconds: 时间（秒）
            
        Returns:
            格式化后的时间字符串
        """
        if seconds < 1:
            return f"{seconds * 1000:.0f} ms"
        elif seconds < 60:
            return f"{seconds:.2f} s"
        elif seconds < 3600:
            minutes = int(seconds // 60)
            secs = seconds % 60
            return f"{minutes}m {secs:.1f}s"
        else:
            hours = int(seconds // 3600)
            minutes = int((seconds % 3600) // 60)
            secs = seconds % 60
            return f"{hours}h {minutes}m {secs:.1f}s"
    
    @staticmethod
    def create_progress_bar(percentage: float, width: int = 30) -> str:
        """
        创建进度条
        
        Args:
            percentage: 进度百分比 (0-100)
            width: 进度条宽度
            
        Returns:
            进度条字符串
        """
        filled = int(width * percentage / 100)
        bar = '█' * filled + '░' * (width - filled)
        return f"[{bar}]"


def format_message(username: str, message: str) -> str:
    """
    格式化聊天消息
    
    Args:
        username: 用户名
        message: 消息内容
        
    Returns:
        格式化后的消息
    """
    import datetime
    timestamp = datetime.datetime.now().strftime("%H:%M:%S")
    return f"[{timestamp}] {username}: {message}"


def is_valid_file_path(file_path: str) -> bool:
    """
    验证文件路径是否有效
    
    Args:
        file_path: 文件路径
        
    Returns:
        是否为有效的文件路径
    """
    return os.path.exists(file_path) and os.path.isfile(file_path)

