"""Message interception rules engine"""
import re
import yaml
import time
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Tuple
from enum import Enum


class Severity(Enum):
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"


class Target(Enum):
    BODY = "body"
    HEADER = "header"


@dataclass
class InterceptionRule:
    id: str
    name: str
    pattern: str
    target: str
    severity: str = "medium"
    enabled: bool = True
    header_key: Optional[str] = None
    _compiled_pattern: Optional[re.Pattern] = field(default=None, repr=False)

    def __post_init__(self):
        try:
            flags = re.IGNORECASE | re.MULTILINE
            self._compiled_pattern = re.compile(self.pattern, flags)
        except re.error:
            self._compiled_pattern = None

    def matches(self, body: str, headers: Dict[str, Any]) -> Tuple[bool, Optional[str]]:
        if not self.enabled or self._compiled_pattern is None:
            return False, None

        if self.target == Target.BODY.value or self.target == "body":
            if body and self._compiled_pattern.search(body):
                return True, self._compiled_pattern.pattern
        elif self.target == Target.HEADER.value or self.target == "header":
            if self.header_key and self.header_key in headers:
                header_value = str(headers[self.header_key])
                if self._compiled_pattern.search(header_value):
                    return True, self._compiled_pattern.pattern
            else:
                for key, value in headers.items():
                    if self._compiled_pattern.search(str(value)):
                        return True, self._compiled_pattern.pattern

        return False, None

    def get_severity_score(self) -> int:
        severity_scores = {
            Severity.LOW.value: 1,
            Severity.MEDIUM.value: 2,
            Severity.HIGH.value: 3,
            Severity.CRITICAL.value: 4
        }
        return severity_scores.get(self.severity, 1)


class InterceptionEngine:
    def __init__(self, config):
        self.config = config
        self.rules: List[InterceptionRule] = []
        self._enabled = config.enabled
        self._block_action = config.block_action
        self._log_blocked = config.log_blocked
        # 新增配置
        self._check_prefix_size = config.check_prefix_size
        self._check_suffix_size = config.check_suffix_size
        self._load_rules()

    def _load_rules(self) -> None:
        rules_path = self.config.rules_file

        try:
            import os
            base_dir = self._get_base_dir()

            if base_dir:
                full_path = os.path.join(base_dir, rules_path)
            else:
                full_path = rules_path

            if not os.path.isabs(full_path):
                current_dir = os.path.dirname(os.path.abspath(__file__))
                full_path = os.path.join(current_dir, '..', rules_path)

            full_path = os.path.normpath(full_path)

            if not os.path.exists(full_path):
                return

            with open(full_path, 'r', encoding='utf-8') as f:
                rules_data = yaml.safe_load(f)

            if rules_data and 'rules' in rules_data:
                for rule_data in rules_data['rules']:
                    rule = InterceptionRule(
                        id=rule_data['id'],
                        name=rule_data['name'],
                        pattern=rule_data['pattern'],
                        target=rule_data['target'],
                        severity=rule_data.get('severity', 'medium'),
                        enabled=rule_data.get('enabled', True),
                        header_key=rule_data.get('header_key')
                    )
                    self.rules.append(rule)
        except Exception:
            pass

    def _get_base_dir(self):
        import os
        current = os.path.dirname(os.path.abspath(__file__))
        return os.path.dirname(current)

    def reload_rules(self) -> None:
        self.rules.clear()
        self._load_rules()

    def _extract_check_portion(self, body: bytes) -> str:
        """只提取消息体的前缀和后缀部分进行检查，避免复制整个大消息"""
        if not body:
            return ""

        body_size = len(body)
        prefix_size = min(self._check_prefix_size, body_size)
        suffix_size = min(self._check_suffix_size, body_size)

        if body_size <= prefix_size + suffix_size:
            # 消息体较小，全部检查
            return body.decode('utf-8', errors='ignore')

        prefix = body[:prefix_size]
        suffix = body[-suffix_size:]

        # 合并前缀和后缀，中间用分隔符避免跨边界误匹配
        check_portion = prefix + b"\n...[TRUNCATED]...\n" + suffix
        return check_portion.decode('utf-8', errors='ignore')

    def check_message_bytes(self, body: bytes, headers: Dict[str, Any]) -> Tuple[bool, List[Dict[str, Any]]]:
        """检查bytes类型的消息体，只提取前缀和后缀部分"""
        if not self._enabled:
            return False, []

        check_body = self._extract_check_portion(body)
        return self.check_message(check_body, headers)

    def check_message(self, body: str, headers: Dict[str, Any]) -> Tuple[bool, List[Dict[str, Any]]]:
        if not self._enabled:
            return False, []

        blocked = False
        matched_rules = []

        for rule in self.rules:
            is_match, matched_pattern = rule.matches(body, headers)
            if is_match:
                blocked = True
                matched_rules.append({
                    "rule_id": rule.id,
                    "rule_name": rule.name,
                    "severity": rule.severity,
                    "pattern": matched_pattern,
                    "severity_score": rule.get_severity_score()
                })

        return blocked, matched_rules

    def should_block_bytes(self, body: bytes, headers: Dict[str, Any]) -> Tuple[bool, Optional[str], Optional[str]]:
        """检查bytes类型的消息体是否应该被拦截"""
        blocked, matched_rules = self.check_message_bytes(body, headers)

        if blocked and matched_rules:
            matched_rules.sort(key=lambda x: x['severity_score'], reverse=True)
            top_rule = matched_rules[0]
            return True, top_rule['rule_id'], top_rule['rule_name']

        return False, None, None

    def should_block(self, body: str, headers: Dict[str, Any]) -> Tuple[bool, Optional[str], Optional[str]]:
        blocked, matched_rules = self.check_message(body, headers)

        if blocked and matched_rules:
            matched_rules.sort(key=lambda x: x['severity_score'], reverse=True)
            top_rule = matched_rules[0]
            return True, top_rule['rule_id'], top_rule['rule_name']

        return False, None, None

    @property
    def check_prefix_size(self) -> int:
        return self._check_prefix_size

    @property
    def check_suffix_size(self) -> int:
        return self._check_suffix_size

    def get_rules_summary(self) -> List[Dict[str, Any]]:
        return [
            {
                "id": rule.id,
                "name": rule.name,
                "severity": rule.severity,
                "target": rule.target,
                "enabled": rule.enabled,
                "pattern": rule.pattern
            }
            for rule in self.rules
        ]

    def enable_rule(self, rule_id: str) -> bool:
        for rule in self.rules:
            if rule.id == rule_id:
                rule.enabled = True
                return True
        return False

    def disable_rule(self, rule_id: str) -> bool:
        for rule in self.rules:
            if rule.id == rule_id:
                rule.enabled = False
                return True
        return False

    def add_rule(self, rule_data: Dict[str, Any]) -> bool:
        try:
            rule = InterceptionRule(
                id=rule_data['id'],
                name=rule_data['name'],
                pattern=rule_data['pattern'],
                target=rule_data['target'],
                severity=rule_data.get('severity', 'medium'),
                enabled=rule_data.get('enabled', True),
                header_key=rule_data.get('header_key')
            )
            for existing_rule in self.rules:
                if existing_rule.id == rule.id:
                    return False
            self.rules.append(rule)
            return True
        except Exception:
            return False

    def remove_rule(self, rule_id: str) -> bool:
        for i, rule in enumerate(self.rules):
            if rule.id == rule_id:
                self.rules.pop(i)
                return True
        return False
