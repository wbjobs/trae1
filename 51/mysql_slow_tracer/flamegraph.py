"""Flame graph data generation from collected stack samples.

Uses the collapsed stack format compatible with Brendan Gregg's
FlameGraph tools, but also produces self-contained SVG data that
can be embedded directly into HTML reports.
"""

import logging
from collections import defaultdict
from typing import Dict, List, Optional, Tuple

from .collector import QueryEvent

logger = logging.getLogger(__name__)


class FlameGraphGenerator:
    """Generates flame graph data from stack samples."""

    def __init__(self, symbol_resolver=None):
        self.symbol_resolver = symbol_resolver

    def generate_collapsed_stacks(
        self, queries: List[QueryEvent], tracer=None
    ) -> List[str]:
        """Generate collapsed stack lines compatible with FlameGraph.pl.

        Format: frame1;frame2;frame3 count
        """
        stack_counts: Dict[str, int] = defaultdict(int)

        for query in queries:
            for sample in query.stack_samples:
                stack_id = sample.get("stack_id", -1)
                if stack_id < 0 or tracer is None:
                    frames = ["[unknown]"]
                else:
                    addrs = tracer.get_stack(stack_id)
                    if not addrs:
                        frames = ["[unknown]"]
                    else:
                        frames = []
                        for addr in reversed(addrs):
                            sym = tracer.get_stack_symbol(addr)
                            sym_clean = self._clean_symbol(sym)
                            frames.append(sym_clean)
                        if not frames:
                            frames = ["[unknown]"]

                func = sample.get("func_name", "unknown")
                frames.append(func)

                stack_line = ";".join(frames)
                stack_counts[stack_line] += 1

        return [f"{stack} {count}" for stack, count in sorted(stack_counts.items())]

    def generate_per_query_flame(
        self, query: QueryEvent, tracer=None
    ) -> List[str]:
        """Generate flame graph data for a single query."""
        stack_counts: Dict[str, int] = defaultdict(int)

        for sample in query.stack_samples:
            stack_id = sample.get("stack_id", -1)
            if stack_id < 0 or tracer is None:
                frames = [f"[tid={query.tid}]"]
            else:
                addrs = tracer.get_stack(stack_id)
                if not addrs:
                    frames = [f"[tid={query.tid}]"]
                else:
                    frames = []
                    for addr in reversed(addrs):
                        sym = tracer.get_stack_symbol(addr)
                        sym_clean = self._clean_symbol(sym)
                        frames.append(sym_clean)

            func = sample.get("func_name", "unknown")
            frames.append(func)
            frames.append(query.sql[:60].replace("\n", " "))

            stack_line = ";".join(frames)
            stack_counts[stack_line] += 1

        return [f"{stack} {count}" for stack, count in sorted(stack_counts.items())]

    @staticmethod
    def _clean_symbol(sym: str) -> str:
        if sym is None:
            return "[unknown]"
        sym = sym.split("+")[0].strip()
        sym = sym.split("(")[0].strip()
        if not sym:
            return "[unknown]"
        return sym

    def generate_flamegraph_svg_data(
        self, collapsed_stacks: List[str], title: str = "MySQL Query Flame Graph"
    ) -> str:
        """Generate self-contained SVG flame graph data.

        This produces a simple stacked bar representation that
        can be embedded in HTML reports.
        """
        if not collapsed_stacks:
            return ""

        lines = collapsed_stacks
        total = sum(int(line.split()[-1]) for line in lines)
        if total == 0:
            return ""

        frame_width = 1000.0
        frame_height = 20
        pad = 2
        max_depth = 0
        stack_data = []

        for line in lines:
            parts = line.rsplit(" ", 1)
            if len(parts) != 2:
                continue
            stack_str, count = parts
            count = int(count)
            frames = stack_str.split(";")
            max_depth = max(max_depth, len(frames))
            width = (count / total) * frame_width
            stack_data.append((frames, width, count))

        total_height = (max_depth + 2) * frame_height + 50

        svg_parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {frame_width} {total_height}" '
            f'width="100%" height="{total_height}" style="font-family:Verdana;font-size:11px;">',
            f'<title>{title}</title>',
            f'<text x="{frame_width/2}" y="25" text-anchor="middle" font-size="16" font-weight="bold">{title}</text>',
            f'<text x="{frame_width/2}" y="45" text-anchor="middle" fill="#666">Total samples: {total} | '
            f'Depth: {max_depth}</text>',
        ]

        x_offset = 0.0
        colors = ["#FF7F50", "#FF6347", "#FF4500", "#DC143C", "#B22222"]

        for frames, width, count in stack_data:
            for i, frame in enumerate(frames):
                y = total_height - (i + 1) * frame_height - pad
                color = colors[i % len(colors)]
                title_escaped = frame.replace("&", "&amp;").replace("<", "&lt;")
                display = frame[:30] + ("..." if len(frame) > 30 else "")
                svg_parts.append(
                    f'<g class="frame" style="cursor:pointer;">'
                    f'<title>{title_escaped} ({count} samples, {count/total*100:.1f}%)</title>'
                    f'<rect x="{x_offset:.1f}" y="{y}" width="{width:.1f}" height="{frame_height - pad}" '
                    f'fill="{color}" stroke="#000" stroke-width="0.3" rx="1" ry="1"/>'
                    f'<text x="{x_offset + 3:.1f}" y="{y + frame_height - 6}" fill="#000" font-size="9px">'
                    f'{display}</text>'
                    f'</g>'
                )
            x_offset += width

        svg_parts.append("</svg>")
        return "\n".join(svg_parts)

    def generate_function_timeline(
        self, query: QueryEvent
    ) -> List[Dict]:
        """Generate a timeline of function invocations from samples."""
        timeline = []
        prev_ts = query.start_ts

        for sample in sorted(query.stack_samples, key=lambda s: s.get("timestamp", 0)):
            ts = sample.get("timestamp", 0)
            func = sample.get("func_name", "unknown")
            elapsed_ms = (ts - prev_ts) * 1000 if prev_ts else 0

            timeline.append({
                "timestamp": ts,
                "func_name": func,
                "elapsed_ms": round(elapsed_ms, 2),
                "time_in_query_ms": round((ts - query.start_ts) * 1000, 2),
            })
            prev_ts = ts

        return timeline
