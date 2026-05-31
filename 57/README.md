# mylvmbackup

基于 **LVM 精简快照 + mydumper 并行备份 + S3 归档** 的 MySQL 热备份 / 恢复 CLI 工具。

## 特性

- **热备份一致性**：`FLUSH TABLES WITH READ LOCK` → 创建 LVM 快照 → 立即释放锁 → 挂在快照卷上使用 mydumper 做逻辑备份
- **LVM 精简快照（Thin Snapshot）**：自动识别源卷类型，默认按源 LV 大小的 20% 且不超过 VG 可用的 50% / 精简池的 80% 自适应；支持薄快照链用于增量
- **并行备份**：mydumper 4 线程（可配置）
- **压缩上传**：zstd / pigz / lz4 压缩后上传到 S3（兼容 MinIO / Ceph RGW）
- **增量备份**：基于上一份备份生成薄快照，manifest 中保留 `parent_backup_id` 形成快照链
- **时间点恢复**：按 `--timestamp` 找到最近一份备份，解压并在指定 VG 上创建新 LV 进行恢复

## 安装

```bash
pip install -e .
```

依赖：

- `mydumper` / `myloader`（需在 `PATH`）
- LVM 用户态工具（`lvs`、`lvcreate`、`lvremove`、`mount`、`umount`）
- Python 3.8+，`click`、`PyYAML`、`boto3`、`mysql-connector-python`

## 快速开始

1. 准备配置文件：

```bash
cp config.example.yaml /etc/mylvmbackup.yaml
# 编辑配置，填入 MySQL 账号、LVM VG/LV、S3 bucket
```

2. 执行一次全量备份：

```bash
sudo mylvmbackup -c /etc/mylvmbackup.yaml backup --full
```

3. 执行一次增量备份（基于上一份快照）：

```bash
sudo mylvmbackup -c /etc/mylvmbackup.yaml backup --incremental
```

4. 按备份 ID 恢复到新卷：

```bash
sudo mylvmbackup -c /etc/mylvmbackup.yaml restore \
    --backup-id mysnap-full-20240101-120000-1700000000 \
    --new-lv mysql-restore-01 --lv-size 100G --apply
```

5. 按时间点恢复（取最接近且不晚于该时间点的备份）：

```bash
sudo mylvmbackup -c /etc/mylvmbackup.yaml restore \
    --timestamp 2024-01-01T12:00:00 --apply
```

## 备份执行流程

1. 连接 MySQL，执行 `FLUSH TABLES WITH READ LOCK`（全局读锁）
2. 在锁持有期间调用 `lvcreate -s` 创建精简快照（快照大小自适应）
3. 立即释放全局读锁（窗口仅数秒）
4. 挂载快照卷为只读
5. 在挂载卷上使用 **mydumper（4 线程）** 并行导出
6. 打包为 `.tar.zst`（zstd 压缩）并计算 sha256
7. 生成 `manifest.json`（包含 binlog file/position、快照名、备份 ID、父链）
8. 上传压缩包与 manifest 到 S3
9. 卸载并 `lvremove -f` 删除快照

## 恢复执行流程

1. 从 S3 下载指定备份（或按时间点选择最近一份）的 `manifest.json` 和归档
2. 解压归档
3. 在指定 VG 上 `lvcreate` 新 LV（精简/普通），mkfs，挂载
4. 将解压内容复制到新卷（`--apply`），可直接切换 MySQL 数据目录
5. 卸载并保留新卷（供后续使用）

## 配置

| 字段 | 说明 |
| --- | --- |
| `mysql.host/port/user/password` | MySQL 连接信息，密码也可通过 `MYSQL_PASSWORD` |
| `mysql.defaults_file` | 使用 MySQL 配置文件（推荐） |
| `lvm.vg_name / lv_name` | 源数据卷所在 VG / LV |
| `lvm.thin_pool` | 源 LV 所在精简池；留空将使用传统快照 |
| `lvm.snapshot_size` | 留空则自适应 |
| `backup.threads` | mydumper 线程数，默认 4 |
| `backup.compress_program` | `zstd` / `gzip` / `pigz` / `lz4` / `none` |
| `s3.*` | S3 连接信息；`endpoint_url` 兼容 MinIO 等 |

## 安全建议

- 运行用户需有 `sudo` 免密执行 LVM / mount 命令的权限
- 使用 `mysql.defaults_file` 而不是明文密码
- S3 密钥通过 `AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY` 环境变量注入
- 归档上传建议启用 `ServerSideEncryption: AES256`
- 启用 GPG 加密时，口令文件（`passphrase_file`）权限应设为 `0600`，仅备份用户可读
- GPG 口令可通过 `GPG_PASSPHRASE` 或 `GPG_PASSPHRASE_FILE` 环境变量注入，避免写入配置文件
