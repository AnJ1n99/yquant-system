# yquant_server

基于 C++ 的量化服务器项目。

## 项目结构

```
yquant_server/
├── common/           # 公共工具和组件
│   ├── macros.h      # 宏定义和工具函数
│   └── ringBuffer_queue.h  # 环形缓冲队列
├── src/              # 源代码（待添加）
├── tests/            # 测试代码（待添加）
├── CMakeLists.txt    # 根 CMake 配置
└── README.md         # 项目说明
```

## 环境要求

- CMake >= 3.16
- C++17 或更高版本
- GCC/Clang 或 MSVC 编译器

## 构建项目

### 1. 创建构建目录

```bash
mkdir build && cd build
```

### 2. 配置项目

```bash
# Release 构建（推荐）
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug 构建
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### 3. 编译

```bash
cmake --build .
```

### 4. 运行

```bash
# 如果有可执行文件
./yquant_server
```

## 常用构建选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `CMAKE_BUILD_TYPE` | 构建类型 (Release/Debug) | Release |
| `BUILD_TESTS` | 是否构建测试 | OFF |

## 当前功能

### common 模块

- **macros.h**: 分支预测宏、断言工具
- **ringBuffer_queue.h**: 高性能环形缓冲队列

## 开发指南

### 添加新功能

1. 在对应目录创建 `.h` 和 `.cpp` 文件
2. 更新相应的 `CMakeLists.txt`
3. 重新运行 CMake 和构建

### 添加测试

1. 创建 `tests/` 目录
2. 开启构建选项：`-DBUILD_TESTS=ON`
3. 添加测试用例

## 待开发功能

- [ ] 主程序框架
- [ ] 网络通信模块
- [ ] 数据处理引擎
- [ ] 单元测试
- [ ] 性能优化

## 许可证

待定
