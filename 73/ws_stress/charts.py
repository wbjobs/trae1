"""HTML 图表生成模块 - 延迟分布直方图、吞吐量曲线"""

import json
import os
import time
from typing import List


HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>WebSocket 压测报告 - {title}</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
  * {{ margin: 0; padding: 0; box-sizing: border-box; }}
  body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: #e2e8f0; padding: 24px; }}
  .container {{ max-width: 1400px; margin: 0 auto; }}
  h1 {{ font-size: 28px; margin-bottom: 8px; color: #38bdf8; }}
  .subtitle {{ color: #94a3b8; margin-bottom: 32px; }}
  .cards {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 32px; }}
  .card {{ background: #1e293b; border-radius: 12px; padding: 20px; border: 1px solid #334155; }}
  .card-label {{ font-size: 12px; color: #94a3b8; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; }}
  .card-value {{ font-size: 28px; font-weight: 700; color: #38bdf8; }}
  .card-sub {{ font-size: 12px; color: #64748b; margin-top: 4px; }}
  .charts {{ display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-bottom: 32px; }}
  .chart-box {{ background: #1e293b; border-radius: 12px; padding: 20px; border: 1px solid #334155; }}
  .chart-box h3 {{ font-size: 16px; margin-bottom: 16px; color: #cbd5e1; }}
  canvas {{ max-height: 300px; }}
  .raw-section {{ margin-top: 32px; }}
  .raw-section h3 {{ font-size: 16px; margin-bottom: 12px; color: #cbd5e1; }}
  pre {{ background: #1e293b; border-radius: 8px; padding: 16px; overflow-x: auto; font-size: 12px; border: 1px solid #334155; }}
  table {{ width: 100%; border-collapse: collapse; margin-top: 16px; }}
  th, td {{ padding: 10px 12px; text-align: left; border-bottom: 1px solid #334155; font-size: 13px; }}
  th {{ color: #94a3b8; font-weight: 600; text-transform: uppercase; font-size: 11px; }}
  td {{ color: #e2e8f0; }}
</style>
</head>
<body>
<div class="container">
  <h1>📊 WebSocket 压测报告</h1>
  <div class="subtitle">节点: {node_id} | 开始: {start_time} | 结束: {end_time} | 时长: {duration}s</div>

  <div class="cards">
    <div class="card">
      <div class="card-label">连接成功率</div>
      <div class="card-value">{conn_rate}%</div>
      <div class="card-sub">{conn_succ}/{conn_total} 成功</div>
    </div>
    <div class="card">
      <div class="card-label">消息到达率</div>
      <div class="card-value">{msg_rate}%</div>
      <div class="card-sub">{msg_recv}/{msg_sent} 收到</div>
    </div>
    <div class="card">
      <div class="card-label">P50 延迟</div>
      <div class="card-value">{p50}ms</div>
    </div>
    <div class="card">
      <div class="card-label">P95 延迟</div>
      <div class="card-value">{p95}ms</div>
    </div>
    <div class="card">
      <div class="card-label">P99 延迟</div>
      <div class="card-value">{p99}ms</div>
    </div>
    <div class="card">
      <div class="card-label">平均延迟</div>
      <div class="card-value">{avg}ms</div>
    </div>
    <div class="card">
      <div class="card-label">重连次数</div>
      <div class="card-value">{reconnect_attempts}</div>
      <div class="card-sub">成功率 {reconnect_rate}%</div>
    </div>
    <div class="card">
      <div class="card-label">重连/客户端</div>
      <div class="card-value">{reconnect_per_client}</div>
      <div class="card-sub">平均每客户端</div>
    </div>
  </div>

  <div class="charts">
    <div class="chart-box">
      <h3>📈 吞吐量曲线 (每秒消息数)</h3>
      <canvas id="throughputChart"></canvas>
    </div>
    <div class="chart-box">
      <h3>📊 延迟分布直方图</h3>
      <canvas id="latencyChart"></canvas>
    </div>
  </div>

  <div class="raw-section">
    <h3>延迟分布详情</h3>
    <table>
      <thead><tr><th>延迟范围</th><th>消息数量</th><th>占比</th></tr></thead>
      <tbody>
        {latency_rows}
      </tbody>
    </table>
  </div>
</div>

<script>
  const throughputData = {throughput_data};
  const latencyData = {latency_data};

  const ctx1 = document.getElementById('throughputChart').getContext('2d');
  new Chart(ctx1, {{
    type: 'line',
    data: {{
      labels: throughputData.map(d => d.time + 's'),
      datasets: [{{
        label: '消息数/秒',
        data: throughputData.map(d => d.messages_per_sec),
        borderColor: '#38bdf8',
        backgroundColor: 'rgba(56, 189, 248, 0.1)',
        fill: true,
        tension: 0.3,
        pointRadius: 2,
      }}]
    }},
    options: {{
      responsive: true,
      plugins: {{ legend: {{ display: false }} }},
      scales: {{
        y: {{ beginAtZero: true, grid: {{ color: '#334155' }}, ticks: {{ color: '#94a3b8' }} }},
        x: {{ grid: {{ color: '#334155' }}, ticks: {{ color: '#94a3b8', maxTicksLimit: 15 }} }}
      }}
    }}
  }});

  const ctx2 = document.getElementById('latencyChart').getContext('2d');
  new Chart(ctx2, {{
    type: 'bar',
    data: {{
      labels: latencyData.map(d => d.range),
      datasets: [{{
        label: '消息数量',
        data: latencyData.map(d => d.count),
        backgroundColor: [
          '#22c55e', '#4ade80', '#86efac', '#facc15', '#fb923c', '#f87171', '#ef4444'
        ],
        borderRadius: 6,
      }}]
    }},
    options: {{
      responsive: true,
      plugins: {{ legend: {{ display: false }} }},
      scales: {{
        y: {{ beginAtZero: true, grid: {{ color: '#334155' }}, ticks: {{ color: '#94a3b8' }} }},
        x: {{ grid: {{ color: '#334155' }}, ticks: {{ color: '#94a3b8' }} }}
      }}
    }}
  }});
</script>
</body>
</html>
"""


class ChartGenerator:
    @staticmethod
    def generate_html(report_dict: dict, output_dir: str, title: str = "压测报告") -> str:
        os.makedirs(output_dir, exist_ok=True)
        conn = report_dict.get("connection", {})
        msg = report_dict.get("message", {})
        lat = report_dict.get("latency_ms", {})
        reconnect = report_dict.get("reconnect", {})
        throughput = report_dict.get("throughput_curve", [])
        latency_dist = report_dict.get("latency_distribution", [])
        node_id = report_dict.get("node_id", "unknown")

        total_lat_msgs = sum(d["count"] for d in latency_dist)
        rows = ""
        for d in latency_dist:
            pct = round(d["count"] / total_lat_msgs * 100, 1) if total_lat_msgs > 0 else 0
            rows += f'<tr><td>{d["range"]}</td><td>{d["count"]}</td><td>{pct}%</td></tr>\n'

        start_ts = report_dict.get("start_time", 0)
        end_ts = report_dict.get("end_time", 0)
        duration = round(end_ts - start_ts, 1)
        start_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(start_ts)) if start_ts else "N/A"
        end_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(end_ts)) if end_ts else "N/A"

        html = HTML_TEMPLATE.format(
            title=title,
            node_id=node_id,
            start_time=start_str,
            end_time=end_str,
            duration=duration,
            conn_rate=conn.get("success_rate", 0),
            conn_succ=conn.get("successful", 0),
            conn_total=conn.get("total", 0),
            msg_rate=msg.get("arrival_rate", 0),
            msg_recv=msg.get("total_received", 0),
            msg_sent=msg.get("total_sent", 0),
            p50=lat.get("p50", 0),
            p95=lat.get("p95", 0),
            p99=lat.get("p99", 0),
            avg=lat.get("avg", 0),
            reconnect_attempts=reconnect.get("attempts", 0),
            reconnect_rate=reconnect.get("success_rate", 0),
            reconnect_per_client=reconnect.get("per_client_avg", 0),
            throughput_data=json.dumps(throughput),
            latency_data=json.dumps(latency_dist),
            latency_rows=rows,
        )

        filepath = os.path.join(output_dir, f"report_{int(time.time())}.html")
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(html)
        return filepath

    @staticmethod
    def generate_aggregate_html(agg_dict: dict, output_dir: str) -> str:
        os.makedirs(output_dir, exist_ok=True)
        conn = agg_dict.get("connection", {})
        msg = agg_dict.get("message", {})
        reconnect = agg_dict.get("reconnect", {})
        node_count = agg_dict.get("node_count", 0)
        node_reports = agg_dict.get("node_reports", [])

        all_throughput = []
        all_latency = {}
        total_msgs = 0
        for r in node_reports:
            for tp in r.get("throughput_curve", []):
                all_throughput.append(tp)
            for ld in r.get("latency_distribution", []):
                bucket = ld["range"]
                all_latency[bucket] = all_latency.get(bucket, 0) + ld["count"]
                total_msgs += ld["count"]

        bucket_order = ["0-10ms", "10-25ms", "25-50ms", "50-100ms", "100-200ms", "200-500ms", "500ms+"]
        latency_list = [{"range": b, "count": all_latency.get(b, 0)} for b in bucket_order]

        rows = ""
        for d in latency_list:
            pct = round(d["count"] / total_msgs * 100, 1) if total_msgs > 0 else 0
            rows += f'<tr><td>{d["range"]}</td><td>{d["count"]}</td><td>{pct}%</td></tr>\n'

        node_rows = ""
        for r in node_reports:
            lat = r.get("latency_ms", {})
            rc = r.get("reconnect", {})
            node_rows += (
                f'<tr><td>{r.get("node_id", "N/A")}</td>'
                f'<td>{r.get("connection", {}).get("success_rate", 0)}%</td>'
                f'<td>{r.get("message", {}).get("arrival_rate", 0)}%</td>'
                f'<td>{lat.get("p50", 0)}ms</td>'
                f'<td>{lat.get("p95", 0)}ms</td>'
                f'<td>{lat.get("p99", 0)}ms</td>'
                f'<td>{rc.get("attempts", 0)}</td>'
                f'<td>{rc.get("success_rate", 0)}%</td></tr>\n'
            )

        html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>聚合压测报告</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
  * {{ margin: 0; padding: 0; box-sizing: border-box; }}
  body {{ font-family: -apple-system, BlinkMacSystemFont, sans-serif; background: #0f172a; color: #e2e8f0; padding: 24px; }}
  .container {{ max-width: 1400px; margin: 0 auto; }}
  h1 {{ font-size: 28px; margin-bottom: 8px; color: #38bdf8; }}
  .subtitle {{ color: #94a3b8; margin-bottom: 32px; }}
  .cards {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 32px; }}
  .card {{ background: #1e293b; border-radius: 12px; padding: 20px; border: 1px solid #334155; }}
  .card-label {{ font-size: 12px; color: #94a3b8; text-transform: uppercase; margin-bottom: 8px; }}
  .card-value {{ font-size: 28px; font-weight: 700; color: #38bdf8; }}
  .card-sub {{ font-size: 12px; color: #64748b; margin-top: 4px; }}
  .charts {{ display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-bottom: 32px; }}
  .chart-box {{ background: #1e293b; border-radius: 12px; padding: 20px; border: 1px solid #334155; }}
  .chart-box h3 {{ font-size: 16px; margin-bottom: 16px; color: #cbd5e1; }}
  canvas {{ max-height: 300px; }}
  table {{ width: 100%; border-collapse: collapse; margin-top: 16px; }}
  th, td {{ padding: 10px 12px; text-align: left; border-bottom: 1px solid #334155; font-size: 13px; }}
  th {{ color: #94a3b8; font-weight: 600; text-transform: uppercase; font-size: 11px; }}
  td {{ color: #e2e8f0; }}
</style>
</head>
<body>
<div class="container">
  <h1>📊 聚合压测报告 ({node_count} 节点)</h1>
  <div class="subtitle">时间: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(agg_dict.get('timestamp', time.time())))}</div>

  <div class="cards">
    <div class="card"><div class="card-label">节点数</div><div class="card-value">{node_count}</div></div>
    <div class="card"><div class="card-label">连接成功率</div><div class="card-value">{conn.get('success_rate', 0)}%</div>
      <div class="card-sub">{conn.get('successful', 0)}/{conn.get('total', 0)}</div></div>
    <div class="card"><div class="card-label">消息到达率</div><div class="card-value">{msg.get('arrival_rate', 0)}%</div>
      <div class="card-sub">{msg.get('total_received', 0)}/{msg.get('total_sent', 0)}</div></div>
    <div class="card"><div class="card-label">重连总次数</div><div class="card-value">{reconnect.get('attempts', 0)}</div>
      <div class="card-sub">成功率 {reconnect.get('success_rate', 0)}%</div></div>
  </div>

  <div class="charts">
    <div class="chart-box"><h3>📈 吞吐量曲线 (每秒消息数)</h3><canvas id="tChart"></canvas></div>
    <div class="chart-box"><h3>📊 延迟分布直方图</h3><canvas id="lChart"></canvas></div>
  </div>

  <h3 style="font-size:16px;margin-bottom:12px;color:#cbd5e1;">各节点详情</h3>
  <table>
    <thead><tr><th>节点ID</th><th>连接成功率</th><th>消息到达率</th><th>P50</th><th>P95</th><th>P99</th><th>重连次数</th><th>重连成功率</th></tr></thead>
    <tbody>{node_rows}</tbody>
  </table>
</div>

<script>
  const tData = {json.dumps(all_throughput)};
  const lData = {json.dumps(latency_list)};
  new Chart(document.getElementById('tChart'), {{
    type: 'line',
    data: {{ labels: tData.map(d=>d.time+'s'), datasets: [{{ label:'消息数/秒', data: tData.map(d=>d.messages_per_sec),
      borderColor:'#38bdf8', backgroundColor:'rgba(56,189,248,0.1)', fill:true, tension:0.3, pointRadius:2 }}] }},
    options: {{ responsive:true, plugins:{{legend:{{display:false}}}},
      scales:{{y:{{beginAtZero:true,grid:{{color:'#334155'}},ticks:{{color:'#94a3b8'}}}},
              x:{{grid:{{color:'#334155'}},ticks:{{color:'#94a3b8',maxTicksLimit:15}}}} }} }});
  new Chart(document.getElementById('lChart'), {{
    type: 'bar',
    data: {{ labels: lData.map(d=>d.range), datasets: [{{ label:'消息数量', data: lData.map(d=>d.count),
      backgroundColor:['#22c55e','#4ade80','#86efac','#facc15','#fb923c','#f87171','#ef4444'], borderRadius:6 }}] }},
    options: {{ responsive:true, plugins:{{legend:{{display:false}}}},
      scales:{{y:{{beginAtZero:true,grid:{{color:'#334155'}},ticks:{{color:'#94a3b8'}}}},
              x:{{grid:{{color:'#334155'}},ticks:{{color:'#94a3b8'}}}} }} }});
</script>
</body>
</html>"""

        filepath = os.path.join(output_dir, f"aggregate_{int(time.time())}.html")
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(html)
        return filepath
