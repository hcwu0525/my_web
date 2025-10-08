#!/usr/bin/env python3
"""
简单的文件传输测试
"""

import socket
import os
import sys
import json
import struct

def send_message(sock, message_type, data, metadata=None):
    """发送消息"""
    try:
        message = {
            "type": message_type,
            "data": data,
            "metadata": metadata or {}
        }
        
        json_message = json.dumps(message, ensure_ascii=False)
        message_bytes = json_message.encode('utf-8')
        
        message_length = len(message_bytes)
        sock.send(struct.pack('!I', message_length))
        sock.send(message_bytes)
        
    except Exception as e:
        print(f"发送消息失败: {e}")
        raise

def test_simple_connection():
    """测试简单连接"""
    try:
        # 尝试连接到服务器
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.settimeout(5)  # 5秒超时
        
        print("尝试连接到 localhost:8888...")
        client_socket.connect(('localhost', 8888))
        print("✅ 连接成功")
        
        # 发送用户加入消息
        send_message(client_socket, "USER_JOIN", "TestUser")
        print("✅ 用户加入消息发送成功")
        
        # 测试发送简单文本消息
        send_message(client_socket, "TEXT", "Hello from test client!")
        print("✅ 文本消息发送成功")
        
        # 测试文件发送
        test_file = "/Users/hcwu/code/my_web/socket_chat/files/test_file.txt"
        if os.path.exists(test_file):
            print(f"开始发送文件: {test_file}")
            
            # 读取文件
            with open(test_file, 'rb') as f:
                file_content = f.read()
            
            filename = os.path.basename(test_file)
            file_size = len(file_content)
            
            # 发送文件信息
            file_info = {
                "filename": filename,
                "size": file_size,
                "sender": "TestUser"
            }
            send_message(client_socket, "FILE", "", file_info)
            print("✅ 文件信息发送成功")
            
            # 发送文件数据
            send_message(client_socket, "FILE_DATA", file_content.hex(), {
                "bytes_sent": 0,
                "total_size": file_size
            })
            print("✅ 文件数据发送成功")
            
            # 发送完成信号
            send_message(client_socket, "FILE_COMPLETE", "", {
                "filename": filename,
                "total_size": file_size
            })
            print("✅ 文件发送完成")
        else:
            print(f"❌ 测试文件不存在: {test_file}")
        
        client_socket.close()
        print("✅ 测试完成")
        return True
        
    except ConnectionRefusedError:
        print("❌ 连接被拒绝 - 服务器可能未启动")
        return False
    except socket.timeout:
        print("❌ 连接超时")
        return False
    except Exception as e:
        print(f"❌ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    print("开始简单连接和文件传输测试...")
    success = test_simple_connection()
    if success:
        print("\n🎉 所有测试通过!")
    else:
        print("\n💥 测试失败，请检查服务器是否正在运行")
        print("启动服务器命令: python server.py")