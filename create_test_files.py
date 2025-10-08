#!/usr/bin/env python3
"""
生成测试文件用于文件传输测试
"""

import os
import random
import string

def create_test_file(filename, size_mb):
    """
    创建指定大小的测试文件
    
    Args:
        filename: 文件名
        size_mb: 文件大小（MB）
    """
    print(f"正在创建 {size_mb}MB 的测试文件: {filename}")
    
    with open(filename, 'w', encoding='utf-8') as f:
        # 每行大约100字符
        line_size = 100
        lines_per_mb = (1024 * 1024) // line_size
        total_lines = size_mb * lines_per_mb
        
        for i in range(total_lines):
            # 生成随机文本行
            line = ''.join(random.choices(string.ascii_letters + string.digits + ' ', k=line_size-1)) + '\n'
            f.write(line)
            
            # 每1000行显示一次进度
            if i % 1000 == 0:
                progress = (i / total_lines) * 100
                print(f"\r生成进度: {progress:.1f}%", end="", flush=True)
    
    print(f"\n✅ 测试文件创建完成: {filename}")
    print(f"📊 实际大小: {os.path.getsize(filename)} 字节")

def create_binary_test_file(filename, size_mb):
    """
    创建二进制测试文件
    
    Args:
        filename: 文件名
        size_mb: 文件大小（MB）
    """
    print(f"正在创建 {size_mb}MB 的二进制测试文件: {filename}")
    
    size_bytes = size_mb * 1024 * 1024
    chunk_size = 1024 * 1024  # 1MB chunks
    
    with open(filename, 'wb') as f:
        written = 0
        while written < size_bytes:
            chunk_size_actual = min(chunk_size, size_bytes - written)
            # 生成随机字节
            chunk = bytes(random.getrandbits(8) for _ in range(chunk_size_actual))
            f.write(chunk)
            written += chunk_size_actual
            
            progress = (written / size_bytes) * 100
            print(f"\r生成进度: {progress:.1f}%", end="", flush=True)
    
    print(f"\n✅ 二进制测试文件创建完成: {filename}")
    print(f"📊 实际大小: {os.path.getsize(filename)} 字节")

if __name__ == "__main__":
    # 确保文件目录存在
    os.makedirs("/Users/hcwu/code/my_web/files", exist_ok=True)
    
    # 创建不同大小的测试文件
    create_test_file("/Users/hcwu/code/my_web/files/test_1mb.txt", 1)
    create_test_file("/Users/hcwu/code/my_web/files/test_5mb.txt", 5)
    create_binary_test_file("/Users/hcwu/code/my_web/files/test_binary_2mb.bin", 2)
    
    print("\n🎉 所有测试文件创建完成！")
    print("可以使用这些文件测试传输功能：")