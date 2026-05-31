import re
import logging
import time
from typing import List, Optional, Dict, Tuple
from dataclasses import dataclass, field

from .dlp_config import SensitiveConfig, SensitiveWord

logger = logging.getLogger(__name__)


@dataclass
class MatchResult:
    word: str
    category: str
    severity: str
    description: str
    snippet: str
    position: int
    regex: bool = False
    confidence: float = 1.0


class _ACNode:
    __slots__ = ('children', 'fail', 'output')

    def __init__(self):
        self.children: Dict[str, '_ACNode'] = {}
        self.fail: Optional['_ACNode'] = None
        self.output: List[Tuple[str, str, str, str]] = []


class AhoCorasick:
    def __init__(self):
        self.root = _ACNode()
        self._built = False

    def add_word(self, word: str, category: str, severity: str, description: str):
        node = self.root
        for ch in word:
            if ch not in node.children:
                node.children[ch] = _ACNode()
            node = node.children[ch]
        node.output.append((word, category, severity, description))
        self._built = False

    def build(self):
        from collections import deque
        queue = deque()
        self.root.fail = None
        for child in self.root.children.values():
            child.fail = self.root
            queue.append(child)
        while queue:
            current = queue.popleft()
            for ch, child in current.children.items():
                f = current.fail
                while f and ch not in f.children:
                    f = f.fail
                child.fail = f.children[ch] if f and ch in f.children else self.root
                child.output.extend(child.fail.output if child.fail else [])
                queue.append(child)
        self._built = True

    def search(self, text: str) -> List[Tuple[int, str, str, str, str]]:
        if not self._built:
            self.build()
        results = []
        node = self.root
        for i, ch in enumerate(text):
            while node and ch not in node.children:
                node = node.fail
            if not node:
                node = self.root
                continue
            node = node.children[ch]
            for word, category, severity, description in node.output:
                results.append((i - len(word) + 1, word, category, severity, description))
        return results


def _jaccard_similarity(a: str, b: str) -> float:
    set_a = set(a)
    set_b = set(b)
    if not set_a and not set_b:
        return 1.0
    return len(set_a & set_b) / len(set_a | set_b)


def _levenshtein_distance(a: str, b: str) -> int:
    if len(a) < len(b):
        a, b = b, a
    if not b:
        return len(a)
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a):
        curr = [i + 1]
        for j, cb in enumerate(b):
            insertions = prev[j + 1] + 1
            deletions = curr[j] + 1
            substitutions = prev[j] + (ca != cb)
            curr.append(min(insertions, deletions, substitutions))
        prev = curr
    return prev[-1]


def _normalized_levenshtein(a: str, b: str) -> float:
    if not a and not b:
        return 1.0
    max_len = max(len(a), len(b))
    if max_len == 0:
        return 1.0
    return 1.0 - _levenshtein_distance(a, b) / max_len


class SensitiveMatcher:
    def __init__(self, config: SensitiveConfig):
        self.config = config
        self._ac = AhoCorasick()
        self._regex_patterns: List[SensitiveWord] = []
        self._fuzzy_words: List[SensitiveWord] = []
        self._build()

    def _build(self):
        for w in self.config.words:
            if w.regex:
                self._regex_patterns.append(w)
            else:
                self._ac.add_word(w.word, w.category, w.severity, w.description)
        self._ac.build()

    def reload(self, words: List[SensitiveWord]):
        self._ac = AhoCorasick()
        self._regex_patterns = []
        self._fuzzy_words = []
        for w in words:
            if w.regex:
                self._regex_patterns.append(w)
            else:
                self._ac.add_word(w.word, w.category, w.severity, w.description)
        self._ac.build()

    def _get_snippet(self, text: str, pos: int, word_len: int) -> str:
        ctx = self.config.snippet_context_chars
        start = max(0, pos - ctx)
        end = min(len(text), pos + word_len + ctx)
        snippet = text[start:end]
        if start > 0:
            snippet = "..." + snippet
        if end < len(text):
            snippet = snippet + "..."
        return snippet

    def match(self, text: str) -> List[MatchResult]:
        if not text:
            return []

        results: List[MatchResult] = []
        seen: set = set()

        ac_results = self._ac.search(text)
        for pos, word, category, severity, description in ac_results:
            key = (word, pos)
            if key in seen:
                continue
            seen.add(key)
            snippet = self._get_snippet(text, pos, len(word))
            results.append(MatchResult(
                word=word,
                category=category,
                severity=severity,
                description=description,
                snippet=snippet,
                position=pos,
                regex=False,
                confidence=1.0,
            ))

        for w in self._regex_patterns:
            try:
                for m in re.finditer(w.word, text, re.IGNORECASE | re.DOTALL):
                    pos = m.start()
                    key = (m.group(0), pos)
                    if key in seen:
                        continue
                    seen.add(key)
                    snippet = self._get_snippet(text, pos, len(m.group(0)))
                    results.append(MatchResult(
                        word=m.group(0),
                        category=w.category,
                        severity=w.severity,
                        description=w.description or f"regex: {w.word}",
                        snippet=snippet,
                        position=pos,
                        regex=True,
                        confidence=1.0,
                    ))
            except re.error as e:
                logger.warning(f"Invalid regex pattern '{w.word}': {e}")

        if self.config.similarity_threshold > 0:
            for r in list(results):
                continue

        return results

    def match_fuzzy(self, text: str) -> List[MatchResult]:
        """Fuzzy matching for similar-but-not-exact matches (e.g., '机密文件' vs '机密文档')."""
        results = []
        threshold = self.config.similarity_threshold
        if threshold <= 0 or threshold >= 1:
            return results

        text_lower = text.lower()
        for w in self.config.words:
            if w.regex:
                continue
            word_lower = w.word.lower()
            if word_lower in text_lower:
                continue
            for chunk_start in range(0, len(text_lower), 50):
                chunk = text_lower[chunk_start:chunk_start + 200]
                if len(chunk) < len(word_lower) * 0.5:
                    continue
                sim = _normalized_levenshtein(word_lower, chunk[: len(word_lower)])
                if sim >= threshold:
                    for offset in range(max(0, chunk_start - 50), len(text)):
                        window = text[offset:offset + len(word_lower)]
                        if len(window) < len(word_lower) * 0.5:
                            continue
                        s = _normalized_levenshtein(word_lower, window.lower())
                        if s >= threshold:
                            snippet = self._get_snippet(text, offset, len(window))
                            results.append(MatchResult(
                                word=window,
                                category=w.category,
                                severity=w.severity,
                                description=f"fuzzy match for '{w.word}'",
                                snippet=snippet,
                                position=offset,
                                regex=False,
                                confidence=s,
                            ))
                            break
        return results
