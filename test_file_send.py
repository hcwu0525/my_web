#!/usr/bin/env python3
"""
测试客户端文件发送功能
"""

import os
import sys
import socket
import time
import threading
sys.path.append('/Users/hcwu/code/my_web/socket_chat')

from utils import SocketUtils, MessageType

def test_client_file_send():
    """测试客户端文件发送"""
    print("开始测试客户端文件发送功能...")
    
    # 测试文件路径
    test_file = "/Users/hcwu/code/my_web/socket_chat/files/test_file.txt"
    
    # 检查测试文件是否存在
    if not os.path.exists(test_file):
        print(f"测试文件不存在: {test_file}")
        return False
    
    try:
        # 创建客户端套接字
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect(('localhost', 8888))
        
        print("已连接到服务器")
        
        # 发送用户加入消息
        SocketUtils.send_message(client_socket, MessageType.USER_JOIN, "TestUser")
        print("已发送用户加入消息")
        
        # 等待一下
        time.sleep(0.5)
        
        # 发送文件
        print(f"开始发送文件: {test_file}")
        SocketUtils.send_file(client_socket, test_file, "TestUser")
        print("文件发送完成")
        
        # 等待服务器响应
        time.sleep(1)
        
        client_socket.close()
        print("测试完成")
        return True
        
    except Exception as e:
        print(f"测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = test_client_file_send()
    if success:
        print("✅ 文件发送测试通过")
    else:
        print("❌ 文件发送测试失败")