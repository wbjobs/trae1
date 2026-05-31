"""Fuzzing 安全报告生成 - JSON + HTML 输出 + 基线对比"""

import json
import os
import time
from typing import List, Optional

from .fuzz_engine import FuzzReport, FuzzResult, BaselineComparer


class FuzzReportGenerator:
    @staticmethod
    def to_dict(report: FuzzReport) -> dict:
        return {
            "fuzz_report": True,
            "node_id": report.node_id,
            "target_url": report.target_url,
            "start_time": report.start_time,
            "end_time": report.end_time,
            "duration_seconds": round(report.end_time - report.start_time, 2),
            "total_payloads": report.total_payloads,
            "anomalies_detected": report.anomalies_detected,
            "clean_payloads": report.clean_payloads,
            "anomaly_rate": (
                round(report.anomalies_detected / report.total_payloads * 100, 2)
                if report.total_payloads > 0 else 0.0
            ),
            "anomaly_counts": report.anomaly_counts,
            "category_counts": report.category_counts,
            "severity_counts": report.severity_counts,
            "results": [
                {
                    "payload_id": r.payload_id,
                    "payload_name": r.payload_name,
                    "payload_category": r.payload_category,
                    "payload_severity": r.payload_severity,
                    "anomaly_type": r.anomaly_type,
                    "anomaly_detected": r.anomaly_detected,
                    "response_code": r.response_code,
                    "response_data": r.response_data,
                    "connect_disconnected": r.connect_disconnected,
                    "latency_ms": r.latency_ms,
                    "replay_steps": r.replay_steps,
                    "timestamp": r.timestamp,
                    "raw_response": r.raw_response[:300],
                    "error_message": r.error_message[:300],
                }
                for r in report.results
            ],
        }

    @staticmethod
    def save_json(report: FuzzReport, output_dir: str, prefix: str = "fuzz_report") -> str:
        os.makedirs(output_dir, exist_ok=True)
        filepath = os.path.join(output_dir, f"{prefix}_{int(time.time())}.json")
        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(FuzzReportGenerator.to_dict(report), f, indent=2,
                      ensure_ascii=False, default=str)
        return filepath

    @staticmethod
    def save_baseline(report: FuzzReport, output_dir: str) -> str:
        os.makedirs(output_dir, exist_ok=True)
        filepath = os.path.join(output_dir, "fuzz_baseline.json")
        data = FuzzReportGenerator.to_dict(report)
        data["baseline_label"] = time.strftime("%Y-%m-%d %H:%M:%S")
        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False, default=str)
        return filepath

    @staticmethod
    def load_baseline(filepath: str) -> Optional[dict]:
        if not os.path.exists(filepath):
            return None
        try:
            with open(filepath, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return None

    @staticmethod
    def generate_html(report: FuzzReport, output_dir: str,
                       comparison: Optional[dict] = None) -> str:
        os.makedirs(output_dir, exist_ok=True)
        anomalies_list = [r for r in report.results if r.anomaly_detected]
        clean_list = [r for r in report.results if not r.anomaly_detected]

        anomaly_rows = ""
        for r in anomalies_list:
            anomaly_rows += f"""<tr>
                <td>{r.payload_id}</td>
                <td>{r.payload_name}</td>
                <td><span class="badge badge-{r.anomaly_type}">{r.anomaly_type}</span></td>
                <td>{r.payload_severity}</td>
                <td>{r.latency_ms:.1f}ms</td>
                <td class="error-cell" title="{r.error_message[:200]}">{r.error_message[:80]}</td>
            </tr>"""

        severity_data = json.dumps([
            {"severity": k, "count": v} for k, v in report.severity_counts.items()
        ])
        anomaly_data = json.dumps([
            {"type": k, "count": v} for k, v in report.anomaly_counts.items()
        ])
        category_data = json.dumps([
            {"category": k, "count": v} for k, v in report.category_counts.items()
        ])

        comp_html = ""
        if comparison:
            comp_html = FuzzReportGenerator._comparison_html(comparison)

        start_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(report.start_time))
        end_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(report.end_time))
        duration = round(report.end_time - report.start_time, 1)

        html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>WebSocket Fuzzing 安全报告</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
  * {{ margin: 0; padding: 0; box-sizing: border-box; }}
  body {{ font-family: -apple-system, BlinkMacSystemFont, sans-serif; background: #0f172a; color: #e2e8f0; padding: 24px; }}
  .container {{ max-width: 1400px; margin: 0 auto; }}
  h1 {{ font-size: 28px; margin-bottom: 8px; color: #f87171; }}
  .subtitle {{ color: #94a3b8; margin-bottom: 32px; }}
  .cards {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 16px; margin-bottom: 32px; }}
  .card {{ background: #1e293b; border-radius: 12px; padding: 20px; border: 1px solid #334155; }}
  .card-label {{ font-size: 12px; color: #94a3b8; text-transform: uppercase; margin-bottom: 8px; }}
  .card-value {{ font-size: 28px; font-weight: 700; }}
  .card-value.danger {{ color: #f87171; }}
  .card-value.success {{ color: #22c55e; }}
  .card-value.info {{ color: #38bdf8; }}
  .card-sub {{ font-size: 12px; color: #64748b; margin-top: 4px; }}
  .charts {{ display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 24px; margin-bottom: 32px; }}
  .chart-box {{ background: #1e293b; border-radius: 12px; padding: 20px; border: 1px solid #334155; }}
  .chart-box h3 {{ font-size: 16px; margin-bottom: 16px; color: #cbd5e1; }}
  canvas {{ max-height: 250px; }}
  table {{ width: 100%; border-collapse: collapse; margin-top: 16px; font-size: 13px; }}
  th, td {{ padding: 10px 12px; text-align: left; border-bottom: 1px solid #334155; }}
  th {{ color: #94a3b8; font-weight: 600; text-transform: uppercase; font-size: 11px; background: #1e293b; position: sticky; top: 0; }}
  td {{ color: #e2e8f0; }}
  .badge {{ padding: 3px 8px; border-radius: 4px; font-size: 11px; font-weight: 600; }}
  .badge-crash {{ background: #7f1d1d; color: #fca5a5; }}
  .badge-server_error_5xx {{ background: #92400e; color: #fcd34d; }}
  .badge-connection_disconnect {{ background: #7c2d12; color: #fdba74; }}
  .badge-response_timeout {{ background: #713f12; color: #fde047; }}
  .badge-protocol_error {{ background: #581c87; color: #d8b4fe; }}
  .badge-memory_anomaly_hint {{ background: #1e3a8a; color: #93c5fd; }}
  .error-cell {{ color: #fca5a5; font-family: monospace; font-size: 11px; }}
  .section-title {{ font-size: 20px; margin: 32px 0 16px; color: #cbd5e1; border-bottom: 1px solid #334155; padding-bottom: 8px; }}
  .comp-table {{ margin-top: 12px; }}
  .comp-new {{ background: rgba(248, 113, 113, 0.1); }}
  .comp-fixed {{ background: rgba(34, 197, 94, 0.1); }}
  .comp-regression {{ background: rgba(251, 146, 60, 0.1); }}
  pre {{ background: #1e293b; border-radius: 8px; padding: 12px; overflow-x: auto; font-size: 11px; margin-top: 8px; }}
</style>
</head>
<body>
<div class="container">
  <h1>🛡️ WebSocket Fuzzing 安全报告</h1>
  <div class="subtitle">目标: {report.target_url} | 开始: {start_str} | 结束: {end_str} | 时长: {duration}s</div>

  <div class="cards">
    <div class="card"><div class="card-label">Payload总数</div><div class="card-value info">{report.total_payloads}</div></div>
    <div class="card"><div class="card-label">异常检测</div><div class="card-value danger">{report.anomalies_detected}</div>
      <div class="card-sub">异常率 {round(report.anomalies_detected/report.total_payloads*100,1) if report.total_payloads>0 else 0}%</div></div>
    <div class="card"><div class="card-label">正常通过</div><div class="card-value success">{report.clean_payloads}</div></div>
    <div class="card"><div class="card-label">连接断开</div><div class="card-value danger">{report.anomaly_counts.get('connection_disconnect', 0)}</div></div>
    <div class="card"><div class="card-label">服务端错误</div><div class="card-value danger">{report.anomaly_counts.get('server_error_5xx', 0)}</div></div>
    <div class="card"><div class="card-label">响应超时</div><div class="card-value danger">{report.anomaly_counts.get('response_timeout', 0)}</div></div>
  </div>

  {comp_html}

  <div class="charts">
    <div class="chart-box"><h3>异常类型分布</h3><canvas id="anomalyChart"></canvas></div>
    <div class="chart-box"><h3>Payload严重程度</h3><canvas id="severityChart"></canvas></div>
    <div class="chart-box"><h3>Payload类别分布</h3><canvas id="categoryChart"></canvas></div>
  </div>

  <div class="section-title">异常详情 ({len(anomalies_list)})</div>
  <table>
    <thead><tr><th>ID</th><th>名称</th><th>异常类型</th><th>严重度</th><th>延迟</th><th>错误信息</th></tr></thead>
    <tbody>{anomaly_rows}</tbody>
  </table>

  <div class="section-title">通过的 Payload ({len(clean_list)})</div>
  <table>
    <thead><tr><th>ID</th><th>名称</th><th>类别</th><th>严重度</th><th>延迟</th></tr></thead>
    <tbody>
      {"".join(f'<tr><td>{r.payload_id}</td><td>{r.payload_name}</td><td>{r.payload_category}</td><td>{r.payload_severity}</td><td>{r.latency_ms:.1f}ms</td></tr>' for r in clean_list[:50])}
    </tbody>
  </table>
</div>

<script>
  const anomalyData = {anomaly_data};
  const severityData = {severity_data};
  const categoryData = {category_data};

  if (anomalyData.length > 0) {{
    new Chart(document.getElementById('anomalyChart'), {{
      type: 'doughnut',
      data: {{ labels: anomalyData.map(d=>d.type),
        datasets: [{{ data: anomalyData.map(d=>d.count),
          backgroundColor: ['#ef4444','#f97316','#eab308','#a855f7','#3b82f6','#06b6d4'] }}] }},
      options: {{ responsive:true, plugins:{{legend:{{position:'bottom',labels:{{color:'#94a3b8',font:{{size:10}}}}}} }} }});
  }}
  if (severityData.length > 0) {{
    new Chart(document.getElementById('severityChart'), {{
      type: 'bar',
      data: {{ labels: severityData.map(d=>d.severity),
        datasets: [{{ label:'Payload数', data: severityData.map(d=>d.count),
          backgroundColor: ['#ef4444','#f97316','#eab308','#22c55e'] }}] }},
      options: {{ responsive:true, plugins:{{legend:{{display:false}}}},
        scales:{{y:{{beginAtZero:true,grid:{{color:'#334155'}},ticks:{{color:'#94a3b8'}}}},
                x:{{grid:{{color:'#334155'}},ticks:{{color:'#94a3b8'}}}} }} }});
  }}
  if (categoryData.length > 0) {{
    new Chart(document.getElementById('categoryChart'), {{
      type: 'bar',
      data: {{ labels: categoryData.map(d=>d.category),
        datasets: [{{ label:'Payload数', data: categoryData.map(d=>d.count),
          backgroundColor: '#38bdf8' }}] }},
      options: {{ responsive:true, plugins:{{legend:{{display:false}}}},
        scales:{{y:{{beginAtZero:true,grid:{{color:'#334155'}},ticks:{{color:'#94a3b8'}}}},
                x:{{grid:{{color:'#334155'}},ticks:{{color:'#94a3b8',font:{{size:9}}}}}} }} }});
  }}
</script>
</body>
</html>"""

        filepath = os.path.join(output_dir, f"fuzz_report_{int(time.time())}.html")
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(html)
        return filepath

    @staticmethod
    def _comparison_html(comp: dict) -> str:
        new_anomalies = comp.get("new_anomalies", [])
        fixed_anomalies = comp.get("fixed_anomalies", [])
        regressions = comp.get("regressions", [])

        new_rows = "".join(
            f'<tr class="comp-new"><td>{a["payload_id"]}</td><td>{a["name"]}</td><td>{a["anomaly_type"]}</td></tr>'
            for a in new_anomalies
        ) or "<tr><td colspan='3'>无新增异常</td></tr>"
        fixed_rows = "".join(
            f'<tr class="comp-fixed"><td>{a["payload_id"]}</td><td>{a["name"]}</td><td>原异常: {a["old_anomaly"]}</td></tr>'
            for a in fixed_anomalies
        ) or "<tr><td colspan='3'>无已修复</td></tr>"
        reg_rows = "".join(
            f'<tr class="comp-regression"><td>{a["payload_id"]}</td><td>{a["name"]}</td><td>{a["new_anomaly"]}</td></tr>'
            for a in regressions
        ) or "<tr><td colspan='3'>无回归</td></tr>"

        delta = comp.get("anomaly_delta", 0)
        delta_class = "danger" if delta > 0 else ("success" if delta < 0 else "info")
        delta_sign = "+" if delta > 0 else ""

        return f"""
  <div class="section-title">📊 基线对比</div>
  <div class="cards" style="grid-template-columns: repeat(4, 1fr);">
    <div class="card"><div class="card-label">基线异常</div><div class="card-value info">{comp.get('baseline_anomalies', 0)}</div></div>
    <div class="card"><div class="card-label">当前异常</div><div class="card-value info">{comp.get('current_anomalies', 0)}</div></div>
    <div class="card"><div class="card-label">变化量</div><div class="card-value {delta_class}">{delta_sign}{delta}</div></div>
    <div class="card"><div class="card-label">新增异常</div><div class="card-value danger">{len(new_anomalies)}</div></div>
  </div>
  <table class="comp-table">
    <thead><tr><th colspan="3" style="background:#7f1d1d">🆕 新增异常 ({len(new_anomalies)})</th></tr>
    <tr><th>ID</th><th>名称</th><th>异常类型</th></tr></thead>
    <tbody>{new_rows}</tbody>
  </table>
  <table class="comp-table">
    <thead><tr><th colspan="3" style="background:#14532d">✅ 已修复 ({len(fixed_anomalies)})</th></tr>
    <tr><th>ID</th><th>名称</th><th>备注</th></tr></thead>
    <tbody>{fixed_rows}</tbody>
  </table>
  <table class="comp-table">
    <thead><tr><th colspan="3" style="background:#92400e">⚠️ 回归 ({len(regressions)})</th></tr>
    <tr><th>ID</th><th>名称</th><th>新异常</th></tr></thead>
    <tbody>{reg_rows}</tbody>
  </table>
"""

    @staticmethod
    def print_summary(report: FuzzReport):
        anomalies = report.anomalies_detected
        total = report.total_payloads
        rate = round(anomalies / total * 100, 1) if total > 0 else 0.0

        print("\n" + "=" * 60)
        print("  Fuzzing 安全报告摘要")
        print("=" * 60)
        print(f"  Payload总数:  {total}")
        print(f"  异常检测:    {anomalies} ({rate}%)")
        print(f"  正常通过:    {report.clean_payloads}")
        print("-" * 60)
        print("  异常类型分布:")
        for atype, count in sorted(report.anomaly_counts.items()):
            print(f"    {atype:25s}: {count}")
        print("-" * 60)
        print("  严重程度分布:")
        for sev, count in sorted(report.severity_counts.items()):
            print(f"    {sev:10s}: {count}")
        print("=" * 60 + "\n")
