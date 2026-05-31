# 接口压测配置管理平台

基于 SpringBoot + JMeter + Vue3 的接口性能测试平台，支持自定义压测参数、并发压测、数据可视化、测试报告自动生成等功能。

## 技术栈

### 后端
- Java 17
- Spring Boot 3.2
- Apache JMeter 5.6
- Spring Data JPA
- H2 Database
- WebSocket (STOMP)

### 前端
- Vue 3
- Element Plus
- ECharts
- Pinia
- Axios

## 项目结构

```
.
├── backend/                 # 后端项目
│   ├── pom.xml
│   └── src/main/
│       ├── java/com/loadtest/
│       │   ├── config/     # 配置类
│       │   ├── controller/ # REST控制器
│       │   ├── dto/        # 数据传输对象
│       │   ├── entity/   # 实体类
│       │   ├── repository/ # 数据访问层
│       │   ├── service/    # 业务逻辑层
│       │   └── websocket/ # WebSocket服务
│       └── resources/
│           └── application.yml
└── frontend/               # 前端项目
    ├── package.json
    ├── vite.config.js
    └── src/
        ├── api/            # API接口
        ├── layouts/      # 布局组件
        ├── router/       # 路由配置
        ├── styles/       # 样式文件
        ├── utils/        # 工具类
        └── views/        # 页面组件
```

## 快速开始

### 环境要求

- JDK 17+
- Node.js 18+
- Apache JMeter 5.6+ (需设置 JMETER_HOME 环境变量)
- Maven 3.8+

### 启动后端

```bash
cd backend

# 安装依赖并构建
mvn clean install -DskipTests

# 运行
mvn spring-boot:run
```

后端服务启动在 http://localhost:8088/api

### 启动前端

```bash
cd frontend

# 安装依赖
npm install

# 开发模式运行
npm run dev

# 构建生产版本
npm run build
```

前端开发服务器运行在 http://localhost:3000

## 功能特性

### 1. 压测参数配置
- 支持配置HTTP请求方法（GET/POST/PUT/DELETE等
- 自定义请求URL、请求头、请求体
- 配置线程数、启动时间、循环次数或持续时间
- 高级设置：协议、端口、域名、路径

### 2. 任务管理
- 创建压测任务
- 实时监控任务状态
- 支持启动/停止任务
- 任务列表查看

### 3. 结果展示
- 响应时间趋势图
- 响应时间分布图
- 吞吐量趋势图
- 成功/失败比例图
- 详细数据表格

### 4. 报告导出
- HTML格式报告
- Excel格式报告
- 自动生成包含图表和统计数据

## API接口

### 配置管理
- `GET /api/configs` - 获取配置列表
- `GET /api/configs/{id}` - 获取配置详情
- `POST /api/configs` - 创建配置
- `PUT /api/configs/{id}` - 更新配置
- `DELETE /api/configs/{id}` - 删除配置

### 任务管理
- `GET /api/tasks` - 获取任务列表
- `GET /api/tasks/{id}` - 获取任务详情
- `POST /api/tasks` - 创建任务
- `POST /api/tasks/{id}/start` - 启动任务
- `POST /api/tasks/{id}/stop` - 停止任务
- `DELETE /api/tasks/{id}` - 删除任务

### 结果与报告
- `GET /api/tasks/{id}/statistics` - 获取统计数据
- `GET /api/tasks/{id}/timeline` - 获取时间线数据
- `GET /api/tasks/{id}/distribution` - 获取响应时间分布
- `POST /api/tasks/{id}/report` - 生成报告
- `GET /api/tasks/{id}/report/download` - 下载报告

## 数据库

使用H2内存数据库，可通过以下地址访问控制台：
- http://localhost:8088/api/h2-console

连接信息：
- JDBC URL: jdbc:h2:mem:loadtestdb
- 用户名: sa
- 密码: (空)

## JMeter集成

确保已安装JMeter并设置环境变量：

```bash
export JMETER_HOME=/path/to/jmeter
```

或者在 `application.yml` 中可配置JMeter路径：

```yaml
jmeter:
  home: ${JMETER_HOME:}
  result-dir: ./results
  report-dir: ./reports
```

## 使用说明

1. 创建压测配置
   - 进入"压测配置"页面
   - 点击"新建配置"
   - 填写请求信息和压测参数
   - 保存配置

2. 启动压测任务
   - 进入"任务管理"页面
   - 点击"新建任务"或直接在配置列表中点击"启动"
   - 选择配置并创建任务
   - 任务会自动开始执行

3. 查看压测结果
   - 在任务列表中点击"查看"
   - 查看实时监控数据和图表
   - 等待任务完成后查看完整结果

4. 导出测试报告
   - 进入"报告导出"页面
   - 选择已完成的任务
   - 选择报告格式并导出
