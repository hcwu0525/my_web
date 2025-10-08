# 文件路径规范说明

## 📁 文件夹结构

```
files/
├── downloads/          # 客户端接收文件目录
├── received/           # 服务器接收文件目录
└── server/             # 服务器发送文件目录
```

## 🎯 使用规范

### 客户端接收文件
- **路径**: `files/downloads/`
- **说明**: 客户端从服务器或其他客户端接收的所有文件
- **命名**: 保持原始文件名
- **Python**: `self.downloads_dir = 'files/downloads'`
- **C++**: `std::string files_dir = "./files/downloads"`

### 服务器接收文件  
- **路径**: `files/received/`
- **说明**: 服务器从客户端接收的所有文件
- **命名**: 保持原始文件名 + 时间戳（防重名）
- **Python**: `self.files_dir = 'files/received'`
- **C++**: `std::string files_dir = "./files/received"`

### 服务器发送文件
- **路径**: `files/server/` (推荐) 或任意路径
- **说明**: 服务器管理员要发送的文件
- **命名**: 任意，建议有意义的名称
- **使用**: 服务器控制台输入 `/send files/server/filename.txt`

## 💡 使用示例

### 1. 服务器发送文件给所有客户端
```bash
# 在服务器控制台输入:
/send files/server/test_document.txt
```

### 2. 服务器发送文件给指定用户
```bash
# 在服务器控制台输入:
/send @用户名 files/server/binary_test.bin
```

### 3. 客户端发送文件
```bash
# 在客户端输入:
/send /path/to/your/file.txt
```

## 🔄 文件流向

```
客户端发送 → files/received/ (服务器接收)
服务器发送 ← files/server/ (服务器文件)
客户端接收 → files/downloads/ (客户端下载)
```

## ✅ 路径兼容性

| 场景 | Python | C++ | 状态 |
|------|---------|-----|------|
| 客户端下载 | `files/downloads/` | `files/downloads/` | ✅ 统一 |
| 服务器接收 | `files/received/` | `files/received/` | ✅ 统一 |
| 服务器发送 | 任意路径 | 任意路径 | ✅ 统一 |

所有版本现在使用相同的文件路径规范，确保跨语言兼容性！