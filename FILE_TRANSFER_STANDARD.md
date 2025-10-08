# 文件传输统一标准

## 📊 传输规格统一

### 分块大小
- **缓冲区大小**: 8192 字节 (8KB)
- **Python**: `SocketUtils.BUFFER_SIZE = 8192`
- **C++**: `const size_t BUFFER_SIZE = 8192`

### 传输格式
- **编码方式**: 十六进制字符串 (hex encoding)
- **数据块传输**: 使用 `FILE_DATA` 消息类型
- **完成信号**: 使用 `FILE_COMPLETE` 消息类型
- **元数据**: JSON格式，包含文件名、大小、发送者等信息

### 消息协议
```json
{
  "type": "FILE|FILE_DATA|FILE_COMPLETE",
  "data": "十六进制数据或空",
  "metadata": {
    "filename": "文件名",
    "size": "文件大小",
    "sender": "发送者",
    "bytes_sent": "已发送字节数",
    "total_size": "总大小",
    "chunk_index": "块索引"
  }
}
```

## 📁 文件命名统一

### 服务器接收文件 (files/received/)
- **格式**: `{username}_{filename}`
- **重名处理**: 添加数字后缀 `{username}_{basename}_{counter}{ext}`
- **示例**: 
  - `alice_document.txt`
  - `alice_document_1.txt` (如果重名)
  - `bob_image_2.jpg` (如果重名)

### 客户端接收文件 (files/downloads/)
- **格式**: 保持原文件名 `{filename}`
- **重名处理**: 添加数字后缀 `{basename}_{counter}{ext}`
- **示例**:
  - `document.txt`
  - `document_1.txt` (如果重名)
  - `image_2.jpg` (如果重名)

## 🔄 实现对比

| 特性 | Python版本 | C++版本 | 状态 |
|------|------------|---------|------|
| **分块大小** | 8192字节 | 8192字节 | ✅ 统一 |
| **传输编码** | hex编码 | hex编码 | ✅ 统一 |
| **服务器接收命名** | `{user}_{file}` | `{user}_{file}` | ✅ 统一 |
| **客户端接收命名** | `{file}_{num}` | `{file}_{num}` | ✅ 统一 |
| **重名处理** | 数字后缀 | 数字后缀 | ✅ 统一 |
| **进度显示** | 实时更新 | 实时更新 | ✅ 统一 |
| **速度计算** | 字节/秒 | 字节/秒 | ✅ 统一 |

## 🚀 传输流程

### 1. 文件发送流程
```
1. 发送 FILE 消息（包含文件信息）
2. 分块读取文件（8KB块）
3. 每块转换为十六进制
4. 发送 FILE_DATA 消息
5. 发送 FILE_COMPLETE 消息
```

### 2. 文件接收流程
```
1. 接收 FILE 消息（创建文件）
2. 接收 FILE_DATA 消息（写入数据）
3. 十六进制转换为字节
4. 接收 FILE_COMPLETE 消息（完成）
```

## 📝 代码示例

### Python 分块发送
```python
chunk = f.read(SocketUtils.BUFFER_SIZE)  # 8192字节
hex_data = chunk.hex()  # 转换为十六进制
self.broadcast_message(MessageType.FILE_DATA, hex_data, metadata)
```

### C++ 分块发送
```cpp
const size_t BUFFER_SIZE = 8192;
std::vector<char> buffer(BUFFER_SIZE);
file.read(buffer.data(), BUFFER_SIZE);
// 转换为十六进制字符串
snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned char>(buffer[i]));
```

## ✅ 兼容性验证

所有版本现在使用相同的：
- 分块大小 (8KB)
- 传输编码 (十六进制)
- 命名规则 (用户名前缀/数字后缀)
- 协议格式 (JSON消息)

确保完美的跨语言兼容性！ 🎯