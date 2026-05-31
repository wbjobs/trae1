"""CLI 入口 - 压测调度 + Fuzzing 子命令"""

import argparse
import asyncio
import json
import os
import sys
import time
import uuid
import logging
from typing import List, Optional

from .config import ClientConfig
from .client import WsClient
from .stats import StatisticsCollector
from .coordinator import RedisCoordinator
from .report import ReportGenerator
from .charts import ChartGenerator
from .fuzz_payloads import (
    get_all_payloads, get_payloads_by_category,
    get_categories, get_severity_counts, print_payloads_table,
    FuzzPayload,
)
from .fuzz_engine import FuzzEngine, BaselineComparer
from .fuzz_report import FuzzReportGenerator

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("ws_stress")


# ============================================================
# 压测模式参数解析
# ============================================================

def _parse_stress_args(argv: Optional[List[str]] = None) -> ClientConfig:
    parser = argparse.ArgumentParser(
        prog="ws_stress stress",
        description="WebSocket 并发压测",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--url", required=True, help="WebSocket 服务器地址")
    parser.add_argument("--clients", type=int, default=5000, help="总并发客户端数 (默认: 5000)")
    parser.add_argument("--rooms", type=int, default=100, help="房间总数 (默认: 100)")
    parser.add_argument("--duration", type=int, default=300, help="压测持续时间秒 (默认: 300)")
    parser.add_argument("--ramp-up", type=int, default=120, help="爬坡时间秒 (默认: 120)")
    parser.add_argument("--msg-min", type=int, default=20, help="消息最小长度 (默认: 20)")
    parser.add_argument("--msg-max", type=int, default=200, help="消息最大长度 (默认: 200)")
    parser.add_argument("--msg-interval", type=float, default=1.0, help="消息发送间隔秒 (默认: 1.0)")
    parser.add_argument("--room-prefix", type=str, default="room", help="房间名前缀 (默认: room)")
    parser.add_argument("--output-dir", type=str, default="./output", help="报告输出目录")
    parser.add_argument("--redis", dest="redis_url", help="Redis 地址 (分布式)")
    parser.add_argument("--role", choices=["single", "master", "slave"], default="single",
                        help="运行角色 (默认: single)")
    parser.add_argument("--nodes", type=int, default=1, help="节点数 (master)")
    parser.add_argument("--node-id", default="", help="节点ID")

    args = parser.parse_args(argv)

    if args.msg_min > args.msg_max:
        parser.error("--msg-min 不能大于 --msg-max")

    return ClientConfig(
        url=args.url, rooms=args.rooms, clients=args.clients,
        duration=args.duration, ramp_up=args.ramp_up,
        msg_min=args.msg_min, msg_max=args.msg_max,
        msg_interval=args.msg_interval, room_prefix=args.room_prefix,
        redis_url=args.redis_url, role=args.role,
        node_id=args.node_id or (f"master-{uuid.uuid4().hex[:6]}" if args.role == "master"
                                  else f"slave-{uuid.uuid4().hex[:6]}"),
        output_dir=args.output_dir,
    )


# ============================================================
# Fuzzing 模式参数解析
# ============================================================

def _parse_fuzz_args(argv: Optional[List[str]] = None) -> dict:
    parser = argparse.ArgumentParser(
        prog="ws_stress fuzz",
        description="WebSocket 协议模糊测试",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--url", default=None, help="WebSocket 服务器地址")
    parser.add_argument("--room", default="fuzz-room", help="测试房间名 (默认: fuzz-room)")
    parser.add_argument("--client-id", default="fuzz-client-001", help="客户端ID")
    parser.add_argument("--timeout", type=float, default=10.0, help="单个 Payload 超时秒 (默认: 10)")
    parser.add_argument("--delay", type=float, default=0.5, help="Payload 间隔秒 (默认: 0.5)")
    parser.add_argument("--categories", nargs="*", default=None,
                        help="按类别筛选: utf8 json injection xss ssti sqli ...")
    parser.add_argument("--severities", nargs="*", default=None,
                        help="按严重度筛选: critical high medium low")
    parser.add_argument("--custom", default=None, help="自定义 Payload JSON 文件路径")
    parser.add_argument("--output-dir", type=str, default="./output", help="报告输出目录")
    parser.add_argument("--baseline", default=None, help="基线报告路径 (用于回归对比)")
    parser.add_argument("--save-baseline", action="store_true", help="将本次结果保存为基线")
    parser.add_argument("--list-payloads", action="store_true", help="列出所有预设 Payload")
    parser.add_argument("--list-categories", action="store_true", help="列出所有类别")
    parser.add_argument("--template", action="store_true", help="输出自定义 Payload 模板")

    args = parser.parse_args(argv)
    return vars(args)


# ============================================================
# 压测模式执行
# ============================================================

async def _ramp_connect(
    config: ClientConfig,
    client_start_idx: int,
    client_count: int,
    ramp_up_seconds: float,
    active_clients: dict,
) -> list:
    clients: list = []
    connect_semaphore = asyncio.Semaphore(200)

    started = time.time()

    for i in range(client_count):
        client_id = f"client-{client_start_idx + i}"
        room = f"{config.room_prefix}-{((client_start_idx + i) % config.rooms) + 1}"
        client = WsClient(
            url=config.url, room=room, client_id=client_id,
            msg_min=config.msg_min, msg_max=config.msg_max,
            msg_interval=config.msg_interval,
        )
        clients.append(client)

        if ramp_up_seconds > 0 and i > 0:
            target_time = (i / client_count) * ramp_up_seconds
            elapsed = time.time() - started
            if elapsed < target_time:
                await asyncio.sleep(target_time - elapsed)

        asyncio.create_task(
            _connect_with_semaphore(client, client_id, connect_semaphore, active_clients)
        )

    await asyncio.sleep(1)
    deadline = time.time() + ramp_up_seconds + 30 if ramp_up_seconds > 0 else time.time() + 30
    while len(active_clients) < client_count and time.time() < deadline:
        await asyncio.sleep(0.5)

    return clients


async def _connect_with_semaphore(client, client_id, semaphore, active_clients):
    async with semaphore:
        success = await client.connect()
        if success:
            active_clients[client_id] = client


async def _run_distributed(config: ClientConfig):
    logger.info(f"节点 {config.node_id} 启动分布式模式 (role={config.role})")
    coordinator = RedisCoordinator(config.redis_url, config.node_id)

    if not await coordinator.connect():
        logger.error("无法连接 Redis, 退出")
        sys.exit(1)

    await coordinator.register_node(config.clients)

    if config.role == "master":
        logger.info(f"等待 {config.nodes} 个节点注册...")
        if not await coordinator.wait_for_all_nodes(config.nodes):
            logger.error("等待节点注册超时")
            await coordinator.close()
            sys.exit(1)
        logger.info("所有节点已注册, 等待就绪...")
        await coordinator.signal_ready()
        if not await coordinator.wait_for_all_ready():
            logger.error("等待节点就绪超时")
            await coordinator.close()
            sys.exit(1)
        logger.info("所有节点就绪, 广播开始信号...")
        await coordinator.broadcast_start()
    else:
        logger.info("等待开始信号...")
        await coordinator.signal_ready()
        if not await coordinator.wait_for_start():
            logger.error("等待开始信号超时")
            await coordinator.close()
            sys.exit(1)

    nodes = await coordinator.get_all_nodes()
    node_ids = sorted(nodes.keys())
    node_idx = node_ids.index(config.node_id)
    start_idx, count = await coordinator.compute_client_assignment(config.clients)
    logger.info(f"本节点分配: client-{start_idx} ~ client-{start_idx + count - 1} (共 {count} 个)")

    report_dict = await _execute_test(config, start_idx, count)
    report_dict["node_id"] = config.node_id

    await coordinator.submit_result(report_dict)
    logger.info("结果已提交到 Redis")

    if config.role == "master":
        await asyncio.sleep(5)
        all_results = await coordinator.collect_results()
        logger.info(f"收集到 {len(all_results)} 个节点的结果")
        agg = ReportGenerator.aggregate_reports(all_results, vars(config))
        ReportGenerator.save_aggregate(agg, config.output_dir)
        ChartGenerator.generate_aggregate_html(agg, config.output_dir)
        logger.info(f"聚合报告已保存到 {config.output_dir}/")

    await coordinator.unregister_node()
    await coordinator.close()


async def _execute_test(config: ClientConfig, start_idx: int, count: int) -> dict:
    stats = StatisticsCollector()
    stats.reset()
    stats.start()

    logger.info(f"开始压测: {count} 个客户端, URL={config.url}, 爬坡={config.ramp_up}s")
    logger.info(f"房间数={config.rooms}, 时长={config.duration}s, 消息间隔={config.msg_interval}s")

    active_clients: dict = {}
    clients = await _ramp_connect(config, start_idx, count, config.ramp_up, active_clients)

    connected_count = len(active_clients)
    logger.info(f"连接阶段完成: {connected_count}/{count} 成功")

    if connected_count == 0:
        logger.error("没有客户端成功连接")
        stats.stop()
        report = stats.build_report(vars(config), config.node_id)
        return vars(report)

    logger.info(f"开始消息收发阶段 (持续 {config.duration} 秒)...")
    tasks = []
    for cid, client in list(active_clients.items()):
        task = asyncio.create_task(client.run(config.duration))
        tasks.append(task)

    try:
        await asyncio.gather(*tasks)
    except Exception as e:
        logger.error(f"运行时错误: {e}")

    logger.info("消息收发阶段结束")
    stats.stop()

    report = stats.build_report(vars(config), config.node_id)
    logger.info(f"连接成功率: {report.connection_success_rate}%")
    logger.info(f"消息到达率: {report.message_arrival_rate}%")
    logger.info(f"延迟 P50={report.latency_p50}ms P95={report.latency_p95}ms P99={report.latency_p99}ms")
    logger.info(f"重连次数: {report.reconnect_attempts} 成功率: {report.reconnect_success_rate}%")

    return vars(report)


def _print_stress_summary(report_dict: dict):
    conn = report_dict.get("connection", {})
    msg = report_dict.get("message", {})
    lat = report_dict.get("latency_ms", {})
    reconnect = report_dict.get("reconnect", {})

    print("\n" + "=" * 60)
    print("  压测结果汇总")
    print("=" * 60)
    print(f"  连接成功率:  {conn.get('success_rate', 0):.2f}% ({conn.get('successful', 0)}/{conn.get('total', 0)})")
    print(f"  消息到达率:  {msg.get('arrival_rate', 0):.2f}% ({msg.get('total_received', 0)}/{msg.get('total_sent', 0)})")
    print(f"  延迟 P50:    {lat.get('p50', 0):.2f} ms")
    print(f"  延迟 P95:    {lat.get('p95', 0):.2f} ms")
    print(f"  延迟 P99:    {lat.get('p99', 0):.2f} ms")
    print(f"  延迟平均:    {lat.get('avg', 0):.2f} ms")
    print(f"  延迟最小:    {lat.get('min', 0):.2f} ms")
    print(f"  延迟最大:    {lat.get('max', 0):.2f} ms")
    print("-" * 60)
    print(f"  重连总次数:  {reconnect.get('attempts', 0)}")
    print(f"  重连成功率:  {reconnect.get('success_rate', 0):.2f}%")
    print(f"  重连/客户端: {reconnect.get('per_client_avg', 0):.2f}")
    print("=" * 60 + "\n")


def cmd_stress(argv: Optional[List[str]] = None):
    config = _parse_stress_args(argv)

    if config.role == "single" and config.redis_url:
        config.role = "single"

    logger.info(f"节点 {config.node_id} 启动")

    if config.redis_url and config.role != "single":
        asyncio.run(_run_distributed(config))
    else:
        report_dict = asyncio.run(_execute_test(config, 0, config.clients))
        _print_stress_summary(report_dict)

        json_path = ReportGenerator.save_json(report_dict, config.output_dir)
        logger.info(f"JSON 报告: {json_path}")

        html_path = ChartGenerator.generate_html(report_dict, config.output_dir)
        logger.info(f"HTML 报告: {html_path}")


# ============================================================
# Fuzzing 模式执行
# ============================================================

async def _execute_fuzz(params: dict) -> dict:
    url = params["url"]
    room = params["room"]
    client_id = params["client_id"]
    timeout = params["timeout"]
    delay = params["delay"]
    categories = params.get("categories")
    severities = params.get("severities")
    custom_file = params.get("custom")
    output_dir = params["output_dir"]
    baseline_path = params.get("baseline")
    save_baseline = params.get("save_baseline", False)

    engine = FuzzEngine(
        url=url, room=room, client_id=client_id,
        timeout=timeout, delay_between=delay,
    )

    report = await engine.run(
        categories=categories,
        severities=severities,
        custom_file=custom_file,
    )
    report.node_id = f"fuzz-{uuid.uuid4().hex[:6]}"

    FuzzReportGenerator.print_summary(report)

    json_path = FuzzReportGenerator.save_json(report, output_dir)
    logger.info(f"Fuzz JSON 报告: {json_path}")

    comparison = None
    if baseline_path:
        baseline = FuzzReportGenerator.load_baseline(baseline_path)
        if baseline:
            comparison = BaselineComparer.compare(baseline, report)
            logger.info(
                f"基线对比: 基线异常={comparison['baseline_anomalies']}, "
                f"当前异常={comparison['current_anomalies']}, "
                f"变化={comparison['anomaly_delta']:+d}"
            )

    html_path = FuzzReportGenerator.generate_html(report, output_dir, comparison)
    logger.info(f"Fuzz HTML 报告: {html_path}")

    if save_baseline:
        baseline_path = FuzzReportGenerator.save_baseline(report, output_dir)
        logger.info(f"已保存为基线: {baseline_path}")

    return vars(report)


def cmd_fuzz(argv: Optional[List[str]] = None):
    params = _parse_fuzz_args(argv)

    if params.get("list_payloads"):
        print("\n预设 Fuzzing Payload 列表:\n")
        print(f"  {'ID':10s} | {'类别':12s} | {'严重度':8s} | 名称")
        print("  " + "-" * 70)
        print(print_payloads_table())
        print()
        counts = get_severity_counts()
        print(f"  严重度统计: {', '.join(f'{k}={v}' for k, v in counts.items())}")
        print(f"  总计: {sum(counts.values())} 个 Payload\n")
        return

    if params.get("list_categories"):
        cats = get_categories()
        print(f"\n可用 Payload 类别 ({len(cats)}):\n")
        for c in cats:
            payloads = get_payloads_by_category(c)
            print(f"  {c:15s} - {len(payloads)} 个 Payload")
        print()
        return

    if params.get("template"):
        from .fuzz_payloads import export_payload_template
        print(json.dumps([export_payload_template()], indent=2, ensure_ascii=False))
        print()
        return

    if not params.get("url"):
        print("错误: --url 是必须的（使用 --list-payloads、--list-categories 或 --template 时不需要）", file=sys.stderr)
        sys.exit(2)

    asyncio.run(_execute_fuzz(params))


# ============================================================
# 主入口 - 子命令路由
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        prog="ws_stress",
        description="WebSocket 压测与 Fuzzing 工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
子命令:
  stress    并发压测模式 (默认)
  fuzz      协议模糊测试模式

示例:
  # 并发压测
  python -m ws_stress stress --url ws://localhost:8080/ws --clients 5000

  # Fuzzing 测试
  python -m ws_stress fuzz --url ws://localhost:8080/ws

  # Fuzzing 仅测试 XSS 和 SQLi 类别
  python -m ws_stress fuzz --url ws://localhost:8080/ws --categories xss sqli

  # Fuzzing 与基线对比
  python -m ws_stress fuzz --url ws://localhost:8080/ws --baseline ./output/fuzz_baseline.json

  # 列出所有预设 Payload
  python -m ws_stress fuzz --list-payloads
        """,
    )
    parser.add_argument("command", nargs="?", default="stress",
                        choices=["stress", "fuzz"],
                        help="运行模式: stress=压测, fuzz=模糊测试 (默认: stress)")

    args, remaining = parser.parse_known_args()

    if args.command == "stress":
        cmd_stress(remaining)
    elif args.command == "fuzz":
        cmd_fuzz(remaining)


if __name__ == "__main__":
    main()
