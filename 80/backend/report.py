"""PDF 质量报表导出模块

基于 reportlab 生成 PDF 报告，包含：
- 汇总表（所有流的平均 MOS、丢包率、抖动等）
- 质量趋势图（MOS 曲线图）
- 流详情表
"""

from __future__ import annotations

import logging
import time
from pathlib import Path
from typing import List, Optional

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (
    SimpleDocTemplate,
    Table,
    TableStyle,
    Paragraph,
    Spacer,
    PageBreak,
    Image as RLImage,
)
from reportlab.graphics.shapes import Drawing, String, Line, PolyLine, Rect
from reportlab.graphics.charts.linecharts import HorizontalLineChart

from .quality import StreamAnalyzer, StreamMetrics
from .window import StreamRegistry

logger = logging.getLogger(__name__)


def _make_mos_chart(streams: List[StreamAnalyzer], width: int = 400, height: int = 200):
    """生成 MOS 趋势折线图。"""
    drawing = Drawing(width, height)
    chart = HorizontalLineChart()
    chart.x = 50
    chart.y = 50
    chart.width = width - 70
    chart.height = height - 70

    all_series = []
    labels = []
    for s in streams:
        snaps = s.get_window_metrics()
        if snaps:
            series = [m.mos for m in snaps]
            # 保证所有 series 长度一致
            all_series.append(series)
            labels.append(f"0x{s.info.ssrc:04X}")

    if not all_series:
        drawing.add(String(width / 2, height / 2, "No data", textAnchor="middle"))
        return drawing

    max_len = max(len(s) for s in all_series)
    for series in all_series:
        while len(series) < max_len:
            series.append(series[-1] if series else 4.0)

    chart.data = all_series
    chart.lines[0].strokeColor = colors.red
    chart.lines[1].strokeColor = colors.blue
    chart.lines[2].strokeColor = colors.green
    chart.lines[3].strokeColor = colors.orange
    chart.valueAxis.valueMin = 1
    chart.valueAxis.valueMax = 5
    chart.valueAxis.valueStep = 0.5
    chart.categoryAxis.labels.boxAnchor = "n"
    chart.categoryAxis.labels.dx = 0
    chart.categoryAxis.labels.dy = -5

    drawing.add(chart)
    drawing.add(String(width / 2, height - 15, "MOS Trend (Last 10s)", textAnchor="middle"))

    for i, label in enumerate(labels[:4]):
        y = height - 35 - i * 12
        c = [colors.red, colors.blue, colors.green, colors.orange][i % 4]
        drawing.add(Line(5, y, 15, y, strokeColor=c, strokeWidth=2))
        drawing.add(String(20, y, label, fontSize=8))

    return drawing


def export_report(
    registry: StreamRegistry,
    output_dir: str = "reports",
    filename: Optional[str] = None,
) -> str:
    """生成并导出 PDF 报表。"""
    Path(output_dir).mkdir(parents=True, exist_ok=True)
    if filename is None:
        ts = time.strftime("%Y%m%d_%H%M%S", time.localtime())
        filename = f"rtp_quality_report_{ts}.pdf"
    filepath = Path(output_dir) / filename

    doc = SimpleDocTemplate(
        str(filepath),
        pagesize=A4,
        leftMargin=20 * mm,
        rightMargin=20 * mm,
        topMargin=20 * mm,
        bottomMargin=20 * mm,
    )

    styles = getSampleStyleSheet()
    title_style = ParagraphStyle("Title2", parent=styles["Title"], fontSize=18, spaceAfter=10)
    heading_style = ParagraphStyle("Heading2", parent=styles["Heading2"], fontSize=14, spaceAfter=8)
    normal_style = styles["Normal"]

    story = []

    # 封面
    story.append(Paragraph("RTP Audio/Video Quality Report", title_style))
    story.append(Paragraph(f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())}", normal_style))
    story.append(Spacer(1, 10 * mm))

    streams = registry.get_all_streams()
    if not streams:
        story.append(Paragraph("No active RTP streams detected.", normal_style))
        doc.build(story)
        logger.info("Report saved (empty): %s", filepath)
        return str(filepath)

    # 汇总表
    story.append(Paragraph("Stream Summary", heading_style))
    summary_data = [["SSRC", "Media", "Codec", "Pkts", "Loss%", "Avg MOS", "Min MOS", "Jitter(ms)", "Delay(ms)"]]
    for s in streams:
        summary = s.get_stream_summary()
        if not summary:
            continue
        summary_data.append([
            summary.get("ssrc", ""),
            summary.get("media_kind", ""),
            summary.get("codec", ""),
            str(summary.get("total_packets", 0)),
            f"{summary.get('total_loss_rate', 0):.2%}",
            f"{summary.get('avg_mos', 0):.2f}",
            f"{summary.get('min_mos', 0):.2f}",
            f"{summary.get('avg_jitter_ms', 0):.1f}",
            f"{summary.get('avg_delay_ms', 0):.1f}",
        ])

    summary_table = Table(summary_data, colWidths=[30*mm, 18*mm, 25*mm, 18*mm, 18*mm, 20*mm, 20*mm, 22*mm, 22*mm])
    summary_table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#4472C4")),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("ALIGN", (0, 0), (-1, -1), "CENTER"),
        ("FONTSIZE", (0, 0), (-1, -1), 8),
        ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.HexColor("#F2F2F2"), colors.white]),
    ]))
    story.append(summary_table)
    story.append(Spacer(1, 10 * mm))

    # MOS 趋势图
    story.append(Paragraph("MOS Trend", heading_style))
    chart = _make_mos_chart(streams)
    story.append(chart)
    story.append(Spacer(1, 10 * mm))

    # 流详情
    story.append(PageBreak())
    story.append(Paragraph("Stream Details", heading_style))
    for s in streams:
        story.append(Paragraph(f"SSRC: 0x{s.info.ssrc:08X} | {s.info.media_kind} | {s.info.codec}", heading_style))
        snaps = s.get_window_metrics()
        if snaps:
            detail_data = [["Time", "Pkts", "Lost", "Loss%", "Jitter(ms)", "Delay(ms)", "MOS", "FPS", "Bitrate(kbps)"]]
            for snap in snaps:
                detail_data.append([
                    time.strftime("%H:%M:%S", time.localtime(snap.timestamp)),
                    str(snap.packet_count),
                    str(snap.lost_packets),
                    f"{snap.loss_rate:.2%}",
                    f"{snap.jitter:.2f}",
                    f"{snap.delay:.2f}",
                    f"{snap.mos:.2f}",
                    f"{snap.fps:.1f}",
                    f"{snap.bitrate:.1f}",
                ])
            detail_table = Table(detail_data, colWidths=[25*mm, 15*mm, 15*mm, 18*mm, 22*mm, 22*mm, 18*mm, 18*mm, 25*mm])
            detail_table.setStyle(TableStyle([
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#548235")),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
                ("ALIGN", (0, 0), (-1, -1), "CENTER"),
                ("FONTSIZE", (0, 0), (-1, -1), 7),
                ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.HexColor("#F2F2F2"), colors.white]),
            ]))
            story.append(detail_table)
        story.append(Spacer(1, 5 * mm))

    doc.build(story)
    logger.info("Report saved: %s", filepath)
    return str(filepath)
