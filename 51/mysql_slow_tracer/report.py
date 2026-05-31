"""HTML report generator for MySQL slow query analysis.

Produces self-contained HTML reports with embedded flame graph SVGs,
timeline charts, and detailed query analysis tables.
"""

import json
import logging
import os
from datetime import datetime
from typing import Dict, List, Optional

from .collector import QueryEvent
from .flamegraph import FlameGraphGenerator

logger = logging.getLogger(__name__)


HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>MySQL Slow Query Analysis Report</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: #1a1a2e;
    color: #e0e0e0;
    line-height: 1.6;
  }
  .container { max-width: 1400px; margin: 0 auto; padding: 20px; }
  .header {
    background: linear-gradient(135deg, #16213e 0%, #0f3460 100%);
    border-radius: 12px;
    padding: 30px;
    margin-bottom: 24px;
    box-shadow: 0 4px 20px rgba(0,0,0,0.3);
  }
  .header h1 { color: #e94560; font-size: 28px; margin-bottom: 8px; }
  .header .subtitle { color: #8892b0; font-size: 14px; }
  .header .meta {
    display: flex; gap: 24px; margin-top: 16px; flex-wrap: wrap;
  }
  .header .meta-item {
    background: rgba(255,255,255,0.05);
    padding: 8px 16px; border-radius: 6px;
    font-size: 13px;
  }
  .header .meta-label { color: #8892b0; margin-right: 6px; }
  .header .meta-value { color: #e94560; font-weight: 600; }

  .summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 16px;
    margin-bottom: 24px;
  }
  .summary-card {
    background: #16213e;
    border-radius: 10px;
    padding: 20px;
    box-shadow: 0 2px 10px rgba(0,0,0,0.2);
    border-left: 3px solid #e94560;
  }
  .summary-card .label {
    color: #8892b0; font-size: 12px; text-transform: uppercase;
    letter-spacing: 1px; margin-bottom: 6px;
  }
  .summary-card .value {
    color: #e94560; font-size: 28px; font-weight: 700;
  }
  .summary-card .unit { color: #8892b0; font-size: 14px; margin-left: 4px; }

  .query-card {
    background: #16213e;
    border-radius: 10px;
    padding: 20px;
    margin-bottom: 20px;
    box-shadow: 0 2px 10px rgba(0,0,0,0.2);
  }
  .query-card-header {
    display: flex; justify-content: space-between; align-items: center;
    margin-bottom: 16px; padding-bottom: 12px;
    border-bottom: 1px solid rgba(255,255,255,0.1);
  }
  .query-card-header h3 {
    color: #64ffda; font-size: 16px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    max-width: 70%;
  }
  .badge {
    display: inline-block; padding: 4px 12px; border-radius: 12px;
    font-size: 12px; font-weight: 600;
  }
  .badge-red { background: #e94560; color: #fff; }
  .badge-blue { background: #0f3460; color: #64ffda; }
  .badge-green { background: #0d7377; color: #fff; }
  .badge-orange { background: #f5a623; color: #1a1a2e; }

  .sql-block {
    background: #0f3460;
    border-radius: 6px;
    padding: 12px 16px;
    margin-bottom: 16px;
    font-family: "Fira Code", "Consolas", monospace;
    font-size: 13px;
    color: #64ffda;
    white-space: pre-wrap;
    word-break: break-all;
    border-left: 3px solid #64ffda;
  }

  .metrics-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 12px;
    margin-bottom: 16px;
  }
  .metric {
    background: rgba(255,255,255,0.03);
    border-radius: 6px;
    padding: 10px;
    text-align: center;
  }
  .metric .m-label { color: #8892b0; font-size: 11px; text-transform: uppercase; }
  .metric .m-value { color: #e94560; font-size: 18px; font-weight: 700; margin-top: 4px; }
  .metric .m-unit { color: #8892b0; font-size: 11px; }

  .timeline-container {
    background: #0f3460;
    border-radius: 6px;
    padding: 16px;
    margin-bottom: 16px;
    overflow-x: auto;
  }
  .timeline-title {
    color: #8892b0; font-size: 13px; margin-bottom: 10px;
    text-transform: uppercase; letter-spacing: 1px;
  }
  .timeline-bar {
    height: 20px; border-radius: 4px; position: relative;
    background: #1a1a2e;
    margin-bottom: 4px;
  }
  .timeline-segment {
    position: absolute; height: 100%;
    border-radius: 4px;
    cursor: pointer;
    transition: opacity 0.2s;
  }
  .timeline-segment:hover { opacity: 0.8; }
  .timeline-segment .seg-label {
    position: absolute; left: 4px; top: 50%;
    transform: translateY(-50%);
    font-size: 9px; white-space: nowrap; overflow: hidden;
    color: rgba(255,255,255,0.9);
  }
  .timeline-axis {
    display: flex; justify-content: space-between;
    color: #8892b0; font-size: 10px; margin-top: 6px;
  }

  .flamegraph-container {
    background: #0f3460;
    border-radius: 6px;
    padding: 16px;
    margin-bottom: 16px;
  }
  .flamegraph-container svg {
    width: 100%; height: auto;
    display: block;
  }

  .details-table {
    width: 100%; border-collapse: collapse;
    font-size: 13px;
  }
  .details-table th {
    background: #0f3460; color: #64ffda;
    padding: 10px 12px; text-align: left;
    font-weight: 600; text-transform: uppercase; font-size: 11px;
    letter-spacing: 0.5px;
  }
  .details-table td {
    padding: 8px 12px;
    border-bottom: 1px solid rgba(255,255,255,0.05);
  }
  .details-table tr:hover { background: rgba(255,255,255,0.02); }
  .text-right { text-align: right; }
  .code {
    font-family: "Fira Code", "Consolas", monospace;
    font-size: 12px; color: #64ffda;
  }

  .breakdown-bar {
    display: flex; height: 24px; border-radius: 4px; overflow: hidden;
    margin: 8px 0;
  }
  .breakdown-seg {
    display: flex; align-items: center; justify-content: center;
    font-size: 11px; font-weight: 600; color: #fff;
  }

  .recommendation {
    background: rgba(100,255,218,0.1);
    border-left: 3px solid #64ffda;
    padding: 12px 16px; border-radius: 0 6px 6px 0;
    margin-bottom: 8px;
    font-size: 13px;
  }
  .recommendation .rec-title {
    color: #64ffda; font-weight: 600; margin-bottom: 4px;
  }

  .ai-section {
    background: rgba(100, 255, 218, 0.08);
    border: 1px solid rgba(100, 255, 218, 0.3);
    border-radius: 8px;
    padding: 16px;
    margin-top: 16px;
  }
  .ai-section-header {
    display: flex; justify-content: space-between; align-items: center;
    margin-bottom: 12px; padding-bottom: 8px;
    border-bottom: 1px solid rgba(100, 255, 218, 0.2);
  }
  .ai-section-title {
    color: #64ffda; font-size: 14px; font-weight: 600;
    text-transform: uppercase; letter-spacing: 1px;
  }
  .ai-badge {
    background: #0d7377; color: #fff; font-size: 10px;
    padding: 3px 8px; border-radius: 10px;
  }
  .ai-block {
    background: rgba(0, 0, 0, 0.2);
    border-radius: 6px;
    padding: 12px 14px;
    margin-bottom: 8px;
    border-left: 3px solid #64ffda;
  }
  .ai-block-title {
    color: #64ffda; font-size: 12px; font-weight: 600;
    margin-bottom: 6px; text-transform: uppercase; letter-spacing: 0.5px;
  }
  .ai-block-content {
    color: #e0e0e0; font-size: 13px; line-height: 1.7;
  }
  .ai-block-content ul { margin-left: 16px; }
  .ai-block-content li { margin-bottom: 4px; }
  .ai-block-content code {
    background: rgba(100, 255, 218, 0.15);
    padding: 2px 6px; border-radius: 3px;
    font-family: "Fira Code", "Consolas", monospace;
    font-size: 12px;
  }
  .ai-error {
    color: #e94560; font-size: 12px;
    background: rgba(233, 69, 96, 0.1);
    padding: 8px 12px; border-radius: 4px;
  }

  .footer {
    text-align: center; color: #8892b0;
    font-size: 12px; padding: 24px;
    border-top: 1px solid rgba(255,255,255,0.1);
    margin-top: 24px;
  }

  .nav-tabs {
    display: flex; gap: 4px; margin-bottom: 16px;
    border-bottom: 1px solid rgba(255,255,255,0.1);
  }
  .nav-tab {
    padding: 10px 20px; cursor: pointer;
    color: #8892b0; border-bottom: 2px solid transparent;
    transition: all 0.2s; font-size: 14px;
  }
  .nav-tab.active {
    color: #e94560; border-bottom-color: #e94560;
  }
  .nav-tab:hover:not(.active) { color: #e0e0e0; }

  .tab-content { display: none; }
  .tab-content.active { display: block; }

  .no-data {
    text-align: center; padding: 60px 20px;
    color: #8892b0; font-size: 16px;
  }
</style>
</head>
<body>
<div class="container">

<div class="header">
  <h1>MySQL Slow Query Analysis</h1>
  <div class="subtitle">eBPF-based Root Cause Analysis Report{ai_subtitle}</div>
  <div class="meta">
    <div class="meta-item"><span class="meta-label">PID:</span><span class="meta-value">{pid}</span></div>
    <div class="meta-item"><span class="meta-label">Threshold:</span><span class="meta-value">{threshold_ms}ms</span></div>
    <div class="meta-item"><span class="meta-label">Generated:</span><span class="meta-value">{generated_at}</span></div>
    <div class="meta-item"><span class="meta-label">Duration:</span><span class="meta-value">{duration}s</span></div>
    {ai_meta}
  </div>
</div>

{summary_html}

{query_cards_html}

<div class="footer">
  MySQL Slow Query Tracer v1.0 | Powered by eBPF/BCC | Generated {generated_at}
</div>

</div>

<script>
document.querySelectorAll('.nav-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    const target = tab.dataset.tab;
    document.querySelectorAll('.nav-tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    tab.classList.add('active');
    document.getElementById(target).classList.add('active');
  });
});
</script>

</body>
</html>
"""


class ReportGenerator:
    """Generates HTML reports from collected query data."""

    def __init__(self, output_dir: str = "./reports"):
        self.output_dir = output_dir
        self.flame_gen = FlameGraphGenerator()

    def _generate_summary(self, queries: List[QueryEvent]) -> str:
        if not queries:
            return '<div class="no-data">No slow queries captured above threshold.</div>'

        durations = [q.duration_ms for q in queries]
        io_waits = [q.io_wait_ms for q in queries]
        lock_waits = [q.lock_wait_ms for q in queries]
        samples = sum(q.sample_count for q in queries)

        cards_html = f"""
        <div class="summary-grid">
          <div class="summary-card">
            <div class="label">Total Slow Queries</div>
            <div class="value">{len(queries)}</div>
          </div>
          <div class="summary-card">
            <div class="label">Avg Duration</div>
            <div class="value">{sum(durations)/len(durations):.1f}<span class="unit">ms</span></div>
          </div>
          <div class="summary-card">
            <div class="label">Max Duration</div>
            <div class="value">{max(durations):.1f}<span class="unit">ms</span></div>
          </div>
          <div class="summary-card">
            <div class="label">P95 Duration</div>
            <div class="value">{self._percentile(durations, 95):.1f}<span class="unit">ms</span></div>
          </div>
          <div class="summary-card">
            <div class="label">Avg I/O Wait</div>
            <div class="value">{sum(io_waits)/len(io_waits):.1f}<span class="unit">ms</span></div>
          </div>
          <div class="summary-card">
            <div class="label">Avg Lock Wait</div>
            <div class="value">{sum(lock_waits)/len(lock_waits):.1f}<span class="unit">ms</span></div>
          </div>
          <div class="summary-card">
            <div class="label">Total Samples</div>
            <div class="value">{samples}</div>
          </div>
          <div class="summary-card">
            <div class="label">Unique SQLs</div>
            <div class="value">{len(set(q.sql for q in queries))}</div>
          </div>
        </div>
        """
        return cards_html

    def _generate_timeline_svg(self, query: QueryEvent) -> str:
        if not query.stack_samples:
            return '<div style="color:#8892b0;font-size:13px;padding:10px;">No function timeline data</div>'

        total_ms = query.duration_ms
        colors = [
            "#e94560", "#64ffda", "#f5a623", "#0d7377", "#533483",
            "#e98a15", "#3cc6f4", "#ff6b9d", "#45b7d1", "#96ceb4"
        ]

        segments_html = ""
        prev_time = query.start_ts
        sorted_samples = sorted(query.stack_samples, key=lambda s: s.get("timestamp", 0))

        for i, sample in enumerate(sorted_samples):
            ts = sample.get("timestamp", 0)
            func = sample.get("func_name", "unknown")
            start_pct = ((ts - query.start_ts) * 1000 / total_ms) * 100 if total_ms > 0 else 0
            next_ts = sorted_samples[i + 1].get("timestamp", query.end_ts) if i + 1 < len(sorted_samples) else query.end_ts
            width_pct = ((next_ts - ts) * 1000 / total_ms) * 100 if total_ms > 0 else 0
            width_pct = max(width_pct, 0.5)

            color = colors[i % len(colors)]
            display_func = func.split("::")[-1] if "::" in func else func

            segments_html += f"""
            <div class="timeline-segment" style="left:{start_pct:.2f}%;width:{width_pct:.2f}%;background:{color};"
                 title="{func} | {(next_ts - ts) * 1000:.2f}ms">
              <span class="seg-label">{display_func[:20]}</span>
            </div>
            """

        axis_labels = ""
        for pct in [0, 25, 50, 75, 100]:
            t = (pct / 100) * total_ms
            axis_labels += f"<span>{t:.0f}ms</span>"

        return f"""
        <div class="timeline-container">
          <div class="timeline-title">Function Execution Timeline ({total_ms:.0f}ms total)</div>
          <div class="timeline-bar">
            {segments_html}
          </div>
          <div class="timeline-axis">{axis_labels}</div>
        </div>
        """

    def _generate_breakdown_bar(self, query: QueryEvent) -> str:
        total = max(query.duration_ms, 0.001)
        cpu_pct = (query.cpu_time_ms / total) * 100
        io_pct = (query.io_wait_ms / total) * 100
        lock_pct = (query.lock_wait_ms / total) * 100

        return f"""
        <div class="breakdown-bar">
          <div class="breakdown-seg" style="width:{cpu_pct:.1f}%;background:#e94560;" title="CPU Time">
            CPU {cpu_pct:.0f}%
          </div>
          <div class="breakdown-seg" style="width:{io_pct:.1f}%;background:#f5a623;" title="I/O Wait">
            I/O {io_pct:.0f}%
          </div>
          <div class="breakdown-seg" style="width:{lock_pct:.1f}%;background:#533483;" title="Lock Wait">
            Lock {lock_pct:.0f}%
          </div>
        </div>
        """

    def _generate_recommendations(self, query: QueryEvent) -> List[str]:
        recs = []
        total = query.duration_ms

        if total > 0 and query.io_wait_ms / total > 0.3:
            recs.append({
                "title": "High I/O Wait Detected",
                "detail": f"I/O wait accounts for {query.io_wait_ms/total*100:.0f}% of query time. "
                          "Consider adding indexes, optimizing query, or checking disk I/O subsystem."
            })

        if total > 0 and query.lock_wait_ms / total > 0.2:
            recs.append({
                "title": "High Lock Wait Detected",
                "detail": f"Lock wait accounts for {query.lock_wait_ms/total*100:.0f}% of query time. "
                          "Check for long-running transactions, optimize isolation level, or review locking strategy."
            })

        if total > 1000:
            recs.append({
                "title": "Query Exceeds 1 Second",
                "detail": f"Query took {total:.0f}ms. Review execution plan with EXPLAIN and consider "
                          "query rewriting or schema optimization."
            })

        if query.sample_count == 0:
            recs.append({
                "title": "No Stack Samples Collected",
                "detail": "The query was too fast to collect meaningful stack samples. "
                          "Consider lowering the threshold or increasing sample rate."
            })

        return recs

    def _generate_ai_suggestion_html(self, ai_suggestion: Optional[Dict]) -> str:
        if not ai_suggestion:
            return ""

        error = ai_suggestion.get("error", "")
        if error:
            return f"""
            <div class="ai-section">
              <div class="ai-section-header">
                <span class="ai-section-title">AI Analysis</span>
                <span class="ai-badge">ERROR</span>
              </div>
              <div class="ai-error">{error}</div>
            </div>
            """

        sections = []

        summary = ai_suggestion.get("summary", "")
        if summary:
            sections.append(f"""
            <div class="ai-block">
              <div class="ai-block-title">Analysis Summary</div>
              <div class="ai-block-content">{summary}</div>
            </div>
            """)

        index_sug = ai_suggestion.get("index_suggestions", [])
        if index_sug:
            items = "".join(f"<li>{s}</li>" for s in index_sug)
            sections.append(f"""
            <div class="ai-block">
              <div class="ai-block-title">Index Suggestions</div>
              <div class="ai-block-content"><ul>{items}</ul></div>
            </div>
            """)

        sql_rewrites = ai_suggestion.get("sql_rewrites", [])
        if sql_rewrites:
            items = "".join(f"<li><code>{s}</code></li>" for s in sql_rewrites)
            sections.append(f"""
            <div class="ai-block">
              <div class="ai-block-title">SQL Rewrite Suggestions</div>
              <div class="ai-block-content"><ul>{items}</ul></div>
            </div>
            """)

        params = ai_suggestion.get("parameter_tuning", [])
        if params:
            items = "".join(f"<li>{s}</li>" for s in params)
            sections.append(f"""
            <div class="ai-block">
              <div class="ai-block-title">Parameter Tuning</div>
              <div class="ai-block-content"><ul>{items}</ul></div>
            </div>
            """)

        arch = ai_suggestion.get("architecture_notes", [])
        if arch:
            items = "".join(f"<li>{s}</li>" for s in arch)
            sections.append(f"""
            <div class="ai-block">
              <div class="ai-block-title">Architecture Notes</div>
              <div class="ai-block-content"><ul>{items}</ul></div>
            </div>
            """)

        if not sections:
            return ""

        analysis_time = ai_suggestion.get("analysis_time_ms", 0)
        time_str = f"{analysis_time:.0f}ms" if analysis_time else "N/A"

        return f"""
        <div class="ai-section">
          <div class="ai-section-header">
            <span class="ai-section-title">AI Optimization Suggestions</span>
            <span class="ai-badge">AI | {time_str}</span>
          </div>
          {''.join(sections)}
        </div>
        """

    def _generate_query_card(self, query: QueryEvent, index: int, tracer=None) -> str:
        sql_escaped = query.sql.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        start_str = datetime.fromtimestamp(query.start_ts).strftime("%H:%M:%S.%f")[:-3]

        collapsed = self.flame_gen.generate_per_query_flame(query, tracer)
        flame_svg = self.flame_gen.generate_flamegraph_svg_data(
            collapsed, title=f"Query #{index + 1} Flame Graph"
        )

        timeline = self._generate_timeline_svg(query)
        breakdown = self._generate_breakdown_bar(query)
        recs = self._generate_recommendations(query)

        rec_html = ""
        for rec in recs:
            rec_html += f"""
            <div class="recommendation">
              <div class="rec-title">{rec['title']}</div>
              <div>{rec['detail']}</div>
            </div>
            """

        badge_class = "badge-red" if query.duration_ms > 500 else "badge-orange" if query.duration_ms > 200 else "badge-blue"

        card_html = f"""
        <div class="query-card">
          <div class="query-card-header">
            <h3 title="{sql_escaped[:200]}">Query #{index + 1} | {start_str} | {query.sql[:60]}...</h3>
            <span class="badge {badge_class}">{query.duration_ms:.0f}ms</span>
          </div>

          <div class="sql-block">{sql_escaped}</div>

          <div class="metrics-row">
            <div class="metric">
              <div class="m-label">Duration</div>
              <div class="m-value">{query.duration_ms:.1f}</div>
              <div class="m-unit">ms</div>
            </div>
            <div class="metric">
              <div class="m-label">CPU Time</div>
              <div class="m-value">{query.cpu_time_ms:.1f}</div>
              <div class="m-unit">ms</div>
            </div>
            <div class="metric">
              <div class="m-label">I/O Wait</div>
              <div class="m-value">{query.io_wait_ms:.1f}</div>
              <div class="m-unit">ms</div>
            </div>
            <div class="metric">
              <div class="m-label">Lock Wait</div>
              <div class="m-value">{query.lock_wait_ms:.1f}</div>
              <div class="m-unit">ms</div>
            </div>
            <div class="metric">
              <div class="m-label">Samples</div>
              <div class="m-value">{query.sample_count}</div>
            </div>
            <div class="metric">
              <div class="m-label">TID</div>
              <div class="m-value">{query.tid}</div>
            </div>
          </div>

          <div class="flamegraph-container">
            <div class="timeline-title">Time Breakdown</div>
            {breakdown}
          </div>

          {timeline}

          {f'<div class="flamegraph-container">{flame_svg}</div>' if flame_svg else ''}

          {rec_html if rec_html else ''}

          {self._generate_ai_suggestion_html(query.ai_suggestion)}
        </div>
        """
        return card_html

    def generate_report(
        self,
        queries: List[QueryEvent],
        pid: int,
        threshold_ms: int,
        duration: int,
        tracer=None,
        filename: Optional[str] = None,
        ai_enabled: bool = False,
        ai_model: str = "",
        ai_queries_analyzed: int = 0,
    ) -> str:
        os.makedirs(self.output_dir, exist_ok=True)

        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"mysql_slow_query_report_{pid}_{timestamp}.html"

        filepath = os.path.join(self.output_dir, filename)

        sorted_queries = sorted(queries, key=lambda q: q.duration_ms, reverse=True)

        summary_html = self._generate_summary(sorted_queries)

        cards_html = ""
        for i, query in enumerate(sorted_queries):
            cards_html += self._generate_query_card(query, i, tracer)

        generated_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        ai_subtitle = f" | AI-assisted (model: {ai_model})" if ai_enabled else ""
        if ai_enabled:
            ai_meta_html = (
                f'<div class="meta-item"><span class="meta-label">AI Model:</span>'
                f'<span class="meta-value">{ai_model}</span></div>'
                f'<div class="meta-item"><span class="meta-label">AI Analyzed:</span>'
                f'<span class="meta-value">{ai_queries_analyzed} queries</span></div>'
            )
        else:
            ai_meta_html = ""

        html = HTML_TEMPLATE.format(
            pid=pid,
            threshold_ms=threshold_ms,
            generated_at=generated_at,
            duration=duration,
            summary_html=summary_html,
            query_cards_html=cards_html,
            ai_subtitle=ai_subtitle,
            ai_meta=ai_meta_html,
        )

        with open(filepath, "w", encoding="utf-8") as f:
            f.write(html)

        logger.info(f"Report generated: {filepath}")
        return filepath

    def generate_json_report(
        self,
        queries: List[QueryEvent],
        pid: int,
        threshold_ms: int,
        filename: Optional[str] = None,
    ) -> str:
        os.makedirs(self.output_dir, exist_ok=True)

        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"mysql_slow_query_data_{pid}_{timestamp}.json"

        filepath = os.path.join(self.output_dir, filename)

        data = {
            "metadata": {
                "pid": pid,
                "threshold_ms": threshold_ms,
                "generated_at": datetime.now().isoformat(),
                "query_count": len(queries),
            },
            "queries": [q.to_dict() for q in queries],
        }

        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, default=str)

        logger.info(f"JSON report generated: {filepath}")
        return filepath

    @staticmethod
    def _percentile(data: List[float], p: int) -> float:
        if not data:
            return 0.0
        sorted_data = sorted(data)
        idx = int(len(sorted_data) * p / 100)
        idx = min(idx, len(sorted_data) - 1)
        return sorted_data[idx]
