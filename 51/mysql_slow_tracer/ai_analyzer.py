"""AI-based slow query analyzer using local Ollama models.

Sends SQL, execution stats, lock info, and call stacks to a local
Ollama instance (default: qwen2.5-coder:7b) for optimization suggestions.
"""

import json
import logging
import re
import threading
import time
import urllib.request
import urllib.error
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


@dataclass
class AISuggestion:
    """Structured AI suggestion for a slow query."""

    query_id: str
    sql: str
    summary: str = ""
    index_suggestions: List[str] = field(default_factory=list)
    sql_rewrites: List[str] = field(default_factory=list)
    parameter_tuning: List[str] = field(default_factory=list)
    architecture_notes: List[str] = field(default_factory=list)
    raw_response: str = ""
    analysis_time_ms: float = 0.0
    error: str = ""

    def has_content(self) -> bool:
        return bool(
            self.index_suggestions
            or self.sql_rewrites
            or self.parameter_tuning
            or self.architecture_notes
        )

    def to_dict(self) -> Dict:
        return {
            "query_id": self.query_id,
            "sql": self.sql,
            "summary": self.summary,
            "index_suggestions": self.index_suggestions,
            "sql_rewrites": self.sql_rewrites,
            "parameter_tuning": self.parameter_tuning,
            "architecture_notes": self.architecture_notes,
            "analysis_time_ms": round(self.analysis_time_ms, 2),
            "error": self.error,
        }


SYSTEM_PROMPT = """You are an expert MySQL database performance engineer. Analyze the slow query provided and give specific, actionable optimization suggestions.

Your response must be in Chinese and follow this structured format:

【分析总结】
Brief 1-2 sentence analysis of the root cause.

【索引建议】
- Each suggestion on a new line, starting with a dash
- Include specific table.column names
- Mention index type (BTREE, HASH, etc.) when relevant

【SQL改写建议】
- Each suggestion on a new line, starting with a dash
- Provide the rewritten SQL when possible

【参数调整建议】
- Each suggestion on a new line, starting with a dash
- Include specific parameter names and recommended values

【架构层面建议】
- Each suggestion on a new line, starting with a dash

If the information is insufficient, say so in the summary and give general advice.
Be concise and specific. Avoid vague statements."""


PROMPT_TEMPLATE = """Analyze this MySQL slow query and provide optimization suggestions:

SQL:
```sql
{sql}
```

Execution Statistics:
- Total execution time: {duration_ms:.1f} ms
- CPU time: {cpu_time_ms:.1f} ms
- I/O wait time: {io_wait_ms:.1f} ms
- Lock wait time: {lock_wait_ms:.1f} ms
- Number of stack samples: {sample_count}

Call Stack (top functions):
{stack_summary}

MySQL Version: {mysql_version}

Please provide specific, actionable optimization suggestions."""


class OllamaClient:
    """Minimal Ollama REST API client with timeout and retry support."""

    def __init__(
        self,
        base_url: str = "http://localhost:11434",
        model: str = "qwen2.5-coder:7b",
        timeout: int = 60,
        max_retries: int = 2,
    ):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.timeout = timeout
        self.max_retries = max_retries
        self._lock = threading.Lock()

    def health_check(self) -> bool:
        try:
            req = urllib.request.Request(f"{self.base_url}/api/tags")
            with urllib.request.urlopen(req, timeout=5) as resp:
                return resp.status == 200
        except Exception:
            return False

    def list_models(self) -> List[str]:
        try:
            req = urllib.request.Request(f"{self.base_url}/api/tags")
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return [m["name"] for m in data.get("models", [])]
        except Exception:
            return []

    def generate(
        self,
        prompt: str,
        system: Optional[str] = None,
        temperature: float = 0.3,
    ) -> Optional[str]:
        payload = {
            "model": self.model,
            "prompt": prompt,
            "stream": False,
            "options": {
                "temperature": temperature,
                "num_predict": 2048,
            },
        }
        if system:
            payload["system"] = system

        for attempt in range(self.max_retries + 1):
            try:
                data = json.dumps(payload).encode("utf-8")
                req = urllib.request.Request(
                    f"{self.base_url}/api/generate",
                    data=data,
                    headers={"Content-Type": "application/json"},
                )
                with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                    result = json.loads(resp.read().decode("utf-8"))
                    return result.get("response", "")
            except urllib.error.URLError as e:
                logger.warning(
                    f"Ollama request attempt {attempt + 1} failed: {e}"
                )
                if attempt < self.max_retries:
                    time.sleep(2 ** attempt)
            except Exception as e:
                logger.error(f"Ollama unexpected error: {e}")
                break

        return None


class AIAnalyzer:
    """Analyzes slow queries using local Ollama LLM.

    Sends SQL + execution stats + stack info to the model
    and parses structured optimization suggestions.
    """

    def __init__(
        self,
        ollama: Optional[OllamaClient] = None,
        base_url: str = "http://localhost:11434",
        model: str = "qwen2.5-coder:7b",
        mysql_version: str = "8.0",
        enabled: bool = True,
        max_concurrent: int = 2,
    ):
        if ollama:
            self.client = ollama
        else:
            self.client = OllamaClient(base_url=base_url, model=model)
        self.mysql_version = mysql_version
        self.enabled = enabled
        self._semaphore = threading.Semaphore(max_concurrent)
        self._suggestions: Dict[str, AISuggestion] = {}
        self._lock = threading.Lock()

    def is_available(self) -> bool:
        if not self.enabled:
            return False
        return self.client.health_check()

    def check_and_report(self) -> Tuple[bool, str]:
        if not self.enabled:
            return False, "AI analysis is disabled (use --ai to enable)"

        available = self.client.health_check()
        if not available:
            return False, (
                f"Ollama is not available at {self.client.base_url}. "
                f"Start it with: ollama serve"
            )

        models = self.client.list_models()
        if self.client.model not in models:
            return False, (
                f"Model '{self.client.model}' not found. "
                f"Available: {models}. "
                f"Pull it with: ollama pull {self.client.model}"
            )

        return True, f"AI analysis ready (model: {self.client.model})"

    def _build_stack_summary(self, stack_samples: List[Dict], max_entries: int = 15) -> str:
        if not stack_samples:
            return "  (no stack samples collected)"

        func_counts: Dict[str, int] = {}
        for sample in stack_samples:
            func = sample.get("func_name", "unknown")
            func = func.split("::")[-1] if "::" in func else func
            func = func.split("_Z")[-1] if "_Z" in func and len(func) > 20 else func
            func_counts[func] = func_counts.get(func, 0) + 1

        sorted_funcs = sorted(func_counts.items(), key=lambda x: x[1], reverse=True)
        lines = []
        for func, count in sorted_funcs[:max_entries]:
            pct = count / max(len(stack_samples), 1) * 100
            lines.append(f"  - {func}: {count} samples ({pct:.1f}%)")

        return "\n".join(lines) if lines else "  (no stack samples)"

    def _build_prompt(self, query_event) -> str:
        cpu_time = max(
            0.0, query_event.duration_ms - query_event.io_wait_ms - query_event.lock_wait_ms
        )
        stack_summary = self._build_stack_summary(query_event.stack_samples)

        return PROMPT_TEMPLATE.format(
            sql=query_event.sql,
            duration_ms=query_event.duration_ms,
            cpu_time_ms=cpu_time,
            io_wait_ms=query_event.io_wait_ms,
            lock_wait_ms=query_event.lock_wait_ms,
            sample_count=query_event.sample_count,
            stack_summary=stack_summary,
            mysql_version=self.mysql_version,
        )

    def _parse_response(self, response: str, query_id: str, sql: str) -> AISuggestion:
        suggestion = AISuggestion(query_id=query_id, sql=sql, raw_response=response)

        if not response:
            suggestion.error = "Empty response from model"
            return suggestion

        sections = {
            "summary": r"【分析总结】",
            "index": r"【索引建议】",
            "sql": r"【SQL改写建议】",
            "params": r"【参数调整建议】",
            "arch": r"【架构层面建议】",
        }

        section_positions = {}
        for key, pattern in sections.items():
            match = re.search(pattern, response)
            if match:
                section_positions[key] = match.end()

        if "summary" in section_positions:
            end = min(
                (p for k, p in section_positions.items() if k != "summary"),
                default=len(response),
            )
            text = response[section_positions["summary"] : end].strip()
            suggestion.summary = text[:500]

        ordered_sections = sorted(section_positions.items(), key=lambda x: x[1])
        for i, (key, start) in enumerate(ordered_sections):
            if key == "summary":
                continue
            end = (
                ordered_sections[i + 1][1]
                if i + 1 < len(ordered_sections)
                else len(response)
            )
            text = response[start:end].strip()
            items = self._parse_section_items(text)

            if key == "index":
                suggestion.index_suggestions = items
            elif key == "sql":
                suggestion.sql_rewrites = items
            elif key == "params":
                suggestion.parameter_tuning = items
            elif key == "arch":
                suggestion.architecture_notes = items

        if not suggestion.has_content() and response.strip():
            suggestion.summary = response.strip()[:500]

        return suggestion

    @staticmethod
    def _parse_section_items(text: str) -> List[str]:
        items = []
        for line in text.split("\n"):
            line = line.strip()
            if not line:
                continue
            if line.startswith("-"):
                line = line[1:].strip()
            if line.startswith("*"):
                line = line[1:].strip()
            if line and len(line) > 1:
                items.append(line)
        return items

    def analyze(self, query_event, timeout: Optional[int] = None) -> AISuggestion:
        if not self.enabled:
            return AISuggestion(
                query_id=query_event.query_id,
                sql=query_event.sql,
                error="AI analysis disabled",
            )

        start = time.time()
        suggestion = AISuggestion(
            query_id=query_event.query_id, sql=query_event.sql
        )

        try:
            self._semaphore.acquire(timeout=timeout or self.client.timeout + 10)
        except Exception:
            suggestion.error = "Analysis queue timeout"
            suggestion.analysis_time_ms = (time.time() - start) * 1000
            return suggestion

        try:
            prompt = self._build_prompt(query_event)
            response = self.client.generate(prompt=prompt, system=SYSTEM_PROMPT)

            if response:
                suggestion = self._parse_response(
                    response, query_event.query_id, query_event.sql
                )
                suggestion.query_id = query_event.query_id
                suggestion.sql = query_event.sql
            else:
                suggestion.error = "No response from Ollama"
        except Exception as e:
            suggestion.error = f"Analysis error: {e}"
            logger.error(f"AI analysis error for {query_event.query_id}: {e}")
        finally:
            self._semaphore.release()

        suggestion.analysis_time_ms = (time.time() - start) * 1000

        with self._lock:
            self._suggestions[query_event.query_id] = suggestion

        return suggestion

    def analyze_batch(
        self, query_events, progress_callback=None
    ) -> Dict[str, AISuggestion]:
        suggestions: Dict[str, AISuggestion] = {}

        for i, qe in enumerate(query_events):
            if progress_callback:
                progress_callback(i + 1, len(query_events), qe)

            suggestion = self.analyze(qe)
            suggestions[qe.query_id] = suggestion

        return suggestions

    def get_suggestion(self, query_id: str) -> Optional[AISuggestion]:
        return self._suggestions.get(query_id)

    def get_all_suggestions(self) -> Dict[str, AISuggestion]:
        with self._lock:
            return dict(self._suggestions)

    def format_terminal(self, suggestion: AISuggestion) -> str:
        lines = []
        lines.append(f"\n  AI Optimization Suggestion for {suggestion.query_id}")
        lines.append(f"  {'=' * 60}")
        lines.append(f"  SQL: {suggestion.sql[:80]}{'...' if len(suggestion.sql) > 80 else ''}")
        lines.append(f"  Analysis time: {suggestion.analysis_time_ms:.0f}ms")

        if suggestion.error:
            lines.append(f"  Error: {suggestion.error}")
            return "\n".join(lines)

        if suggestion.summary:
            lines.append(f"\n  【分析总结】")
            lines.append(f"  {suggestion.summary}")

        if suggestion.index_suggestions:
            lines.append(f"\n  【索引建议】")
            for s in suggestion.index_suggestions:
                lines.append(f"    - {s}")

        if suggestion.sql_rewrites:
            lines.append(f"\n  【SQL改写建议】")
            for s in suggestion.sql_rewrites:
                lines.append(f"    - {s}")

        if suggestion.parameter_tuning:
            lines.append(f"\n  【参数调整建议】")
            for s in suggestion.parameter_tuning:
                lines.append(f"    - {s}")

        if suggestion.architecture_notes:
            lines.append(f"\n  【架构层面建议】")
            for s in suggestion.architecture_notes:
                lines.append(f"    - {s}")

        lines.append(f"  {'=' * 60}")
        return "\n".join(lines)
