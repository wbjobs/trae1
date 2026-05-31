# HDR图像处理工具

一个基于浏览器的HDR图像处理工具，使用React + WebAssembly (Rust) 构建。

## 功能特性

- 支持上传RAW格式照片 (.dng, .arw, .cr2, .cr3, .nef, .raf, .rw2, .orf, .pef)
- 三种色调映射算法：Reinhard、Filmic、ACES
- 图像处理参数调整：
  - 曝光 (±3EV)
  - 对比度 (0.5-2.0)
  - 饱和度 (0-2.0)
  - 高光恢复 (-1 到 +1)
  - 阴影恢复 (-1 到 +1)
  - 色温调整
  - 色调调整
- 直方图显示（处理前后对比）
- 前后对比视图
- 批量处理（最多5张图片）
- 导出格式：JPEG、PNG
- 所有处理在WASM中完成，利用SIMD加速

## 高分辨率图像处理（分块处理）

针对4500万像素以上的高分辨率图像，系统采用分块处理机制：

- **自动分块**: 图像被分割成128x128到1024x1024的瓦片进行处理
- **自适应瓦片大小**: 根据图像分辨率和可用内存自动选择最优瓦片大小
- **内存复用**: 瓦片处理时复用内存缓冲区，减少内存分配
- **内存监控**: 实时监控内存使用，提供预估和警告
- **内存警告**: 
  - 1级警告（橙色）: 预计内存使用超过1GB
  - 2级警告（红色）: 预计内存使用超过2GB
- **手动调整**: 用户可手动选择瓦片大小（128、256、512、1024或自动）

### 瓦片大小选择策略

| 图像分辨率 | 自动瓦片大小 | 预计内存使用 |
|-----------|-------------|-------------|
| < 1500万像素 | 1024x1024 | < 500MB |
| 1500-3000万像素 | 512x512 | 500MB-1GB |
| > 3000万像素 | 256x256 | 1GB-2GB |
| > 4500万像素 | 128x128 | > 2GB |

## 项目结构

```
.
├── hdr-wasm/              # Rust WASM 库
│   ├── src/
│   │   ├── lib.rs         # WASM导出API
│   │   ├── raw.rs         # RAW文件解析
│   │   ├── processing.rs  # 图像处理算法
│   │   ├── histogram.rs   # 直方图计算
│   │   └── export.rs      # 图像导出
│   └── Cargo.toml
├── src/                   # React 前端
│   ├── components/        # UI组件
│   ├── types/             # TypeScript类型定义
│   ├── wasm/              # WASM模块加载器
│   ├── App.tsx            # 主应用组件
│   └── main.tsx           # 入口文件
├── index.html
├── package.json
├── tsconfig.json
└── vite.config.ts
```

## 安装依赖

### 前端依赖

```bash
npm install
```

### Rust 工具链

确保已安装 Rust 工具链：

```bash
# 安装 wasm-pack
cargo install wasm-pack
```

## 构建和运行

### 1. 编译 Rust WASM 库

```bash
cd hdr-wasm
wasm-pack build --target web --out-dir ../public/hdr-wasm/pkg
```

### 2. 启动开发服务器

```bash
npm run dev
```

应用将在 http://localhost:3000 运行

### 3. 构建生产版本

```bash
npm run build
```

## 使用说明

1. 点击或拖放RAW格式图片到上传区域
2. 使用右侧控制面板调整参数
3. 选择色调映射算法
4. 点击"对比视图"查看处理前后效果
5. 点击"导出"按钮保存处理后的图像

## 技术栈

- **前端**: React 18, TypeScript, Vite
- **WASM**: Rust, wasm-bindgen, rawloader
- **图像处理**: 自定义色调映射和颜色处理算法

## 注意事项

- 首次使用需要编译Rust WASM代码
- 大文件处理可能需要较长时间
- 建议使用现代浏览器（Chrome、Firefox、Edge）
- 支持的RAW格式取决于rawloader库的支持情况
