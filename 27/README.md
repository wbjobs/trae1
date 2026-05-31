# PointCloud WebRTC

一个基于Tauri的桌面应用，用于两个客户端之间通过WebRTC DataChannel传输点云数据（PLY格式）。

## 功能特性

- **点云加载**: 支持加载本地PLY文件（最大支持500万个点）
- **点云处理**: 
  - 体素滤波降采样
  - 自适应降采样（按目标点数）
  - Draco压缩（量化+RLE编码）
- **实时渲染**: 
  - Three.js 3D渲染
  - 鼠标旋转/缩放交互
  - 按高度颜色映射
  - 原始颜色显示
- **网络传输**: 
  - WebRTC DataChannel点对点传输
  - 信令服务器
  - 实时网速和进度显示

## 技术栈

### 后端 (Rust)
- Tauri 2.0
- nalgebra (矩阵运算)
- WebRTC (webrtc crate)
- Tokio (异步运行时)
- Serde (序列化)

### 前端 (React + TypeScript)
- React 18
- TypeScript
- Three.js (3D渲染)
- Vite (构建工具)
- Tauri API

## 项目结构

```
pointcloud-webrtc/
├── src/                          # React前端
│   ├── components/
│   │   ├── PointCloudViewer.tsx  # Three.js点云渲染器
│   ├── FileLoader.tsx           # PLY文件加载组件
│   ├── DownsamplingControls.tsx # 降采样控制
│   ├── ConnectionPanel.tsx      # 连接控制面板
│   └── TransferControls.tsx     # 传输控制
│   ├── hooks/                    # React hooks
│   ├── App.tsx                   # 主应用组件
│   ├── main.tsx                  # 入口文件
│   ├── types.ts                  # TypeScript类型定义
│   ├── tauriCommands.ts          # Tauri命令封装
│   └── styles.css                # 样式
├── src-tauri/                    # Rust后端
│   ├── src/
│   │   ├── main.rs               # Rust入口
│   │   ├── lib.rs                # Tauri命令定义
│   │   ├── point_cloud.rs        # 点云数据结构
│   │   ├── ply_loader.rs         # PLY文件解析
│   │   ├── downsampling.rs       # 体素滤波降采样
│   │   ├── compression.rs        # Draco压缩
│   │   └── webrtc_signaling.rs   # WebRTC信令
│   ├── Cargo.toml                # Rust依赖
│   ├── tauri.conf.json           # Tauri配置
│   └── capabilities/             # 权限配置
├── index.html                    # HTML入口
├── package.json                  # Node依赖
├── tsconfig.json                 # TypeScript配置
└── vite.config.ts                # Vite配置
```

## 构建和运行

### 前置要求

1. **Rust**: 安装 [Rust](https://www.rust-lang.org/tools/install)
2. **Node.js**: 安装 [Node.js](https://nodejs.org/) (v18+)
3. **系统依赖**:
   - Windows: Visual Studio C++ Build Tools
   - macOS: Xcode Command Line Tools
   - Linux: libwebkit2gtk-4.1-dev, libgtk-3-dev, etc.

### 安装依赖

```bash
npm install
```

### 开发模式

```bash
npm run tauri dev
```

### 构建生产版本

```bash
npm run tauri build
```

## 使用指南

### 发送端 (Sender)

1. 启动应用，选择 "Sender" 角色
2. 点击 "Select PLY File" 选择本地PLY文件
3. 点击 "Load Point Cloud" 加载点云
4. 可选：使用降采样功能减少点数
5. 点击 "Start Server" 启动信令服务器
6. 点击 "Compress & Prepare" 压缩点云
7. 等待接收端连接

### 接收端 (Receiver)

1. 启动应用，选择 "Receiver" 角色
2. 输入发送端的地址和端口
3. 点击 "Connect" 连接到发送端
4. 接收点云数据并自动渲染

### 渲染控制

- **鼠标拖拽**: 旋转视角
- **滚轮**: 缩放
- **高度颜色映射**: 按Z轴高度显示颜色（蓝-绿-黄-红）
- **原始颜色**: 显示PLY文件中的原始颜色

## PLY文件格式支持

- ASCII格式
- Binary Little Endian格式
- 支持的属性:
  - x, y, z (顶点坐标)
  - red, green, blue, alpha (颜色)
  - nx, ny, nz (法线)

## 压缩格式

点云压缩采用两级压缩：
1. **量化**: 将浮点数坐标量化为14位整数
2. **RLE编码**: 对重复字节进行游程编码

## 注意事项

- 最大支持500万个点的PLY文件
- 大文件加载可能需要较长时间
- 网络传输速度取决于网络带宽
- 建议在局域网内使用以获得最佳体验

## 许可证

MIT License
