"""Unit tests for RabbitMQ Audit Gateway"""
import unittest
import time
from unittest.mock import Mock, patch, MagicMock
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from config import (
    AppConfig, GatewayConfig, HAConfig, AuditConfig,
    SamplingConfig, InterceptionConfig, SinksConfig,
    KafkaSinkConfig, ElasticsearchSinkConfig, DashboardConfig, OtelConfig
)


class TestConfigClasses(unittest.TestCase):
    def test_gateway_config_defaults(self):
        config = GatewayConfig()
        self.assertEqual(config.name, "rabbitmq_audit_gateway")
        self.assertEqual(config.port, 5672)
        self.assertEqual(config.username, "guest")

    def test_ha_config_defaults(self):
        config = HAConfig()
        self.assertEqual(config.mode, "active_standby")
        self.assertEqual(config.health_check_interval, 5)

    def test_audit_config_defaults(self):
        config = AuditConfig()
        self.assertTrue(config.enabled)
        self.assertEqual(config.sample_rate, 0.01)

    def test_sampling_config_defaults(self):
        config = SamplingConfig()
        self.assertEqual(config.default_rate, 0.01)
        self.assertIn("critical", config.by_message_type)
        self.assertEqual(config.by_message_type["critical"], 1.0)


class TestInterceptionEngine(unittest.TestCase):
    def setUp(self):
        self.rules_path = os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            'config', 'interception_rules.yaml'
        )
        self.config = InterceptionConfig(
            enabled=True,
            rules_file=self.rules_path,
            block_action="drop",
            log_blocked=True
        )

    def test_sql_injection_detection(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        body = "SELECT * FROM users WHERE id=1 OR 1=1"
        headers = {}
        should_block, rule_id, rule_name = engine.should_block(body, headers)

        self.assertTrue(should_block)
        self.assertIsNotNone(rule_id)

    def test_xss_detection(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        body = "<script>alert('xss')</script>"
        headers = {}
        should_block, rule_id, rule_name = engine.should_block(body, headers)

        self.assertTrue(should_block)

    def test_clean_message(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        body = "Hello, this is a normal message"
        headers = {}
        should_block, rule_id, rule_name = engine.should_block(body, headers)

        self.assertFalse(should_block)

    def test_header_target_matching(self):
        from interception import InterceptionEngine

        config = InterceptionConfig(
            enabled=True,
            rules_file="config/interception_rules.yaml"
        )
        engine = InterceptionEngine(config)

        body = ""
        headers = {"message_type": "TEST_DATA"}
        should_block, rule_id, rule_name = engine.should_block(body, headers)

        self.assertTrue(should_block)


class TestSamplingMechanism(unittest.TestCase):
    def test_random_sampling(self):
        from sampling import MessageSampler, SamplingConfig, SamplingStrategy

        config = SamplingConfig(default_rate=1.0)
        sampler = MessageSampler(config)
        sampler.set_strategy(SamplingStrategy.RANDOM)

        results = [sampler.should_sample(f"msg_{i}").should_sample for i in range(100)]
        self.assertTrue(all(results))

    def test_zero_sampling_rate(self):
        from sampling import MessageSampler, SamplingConfig

        config = SamplingConfig(default_rate=0.0)
        sampler = MessageSampler(config)

        results = [sampler.should_sample(f"msg_{i}").should_sample for i in range(100)]
        self.assertFalse(any(results))

    def test_hash_based_sampling(self):
        from sampling import MessageSampler, SamplingConfig, SamplingStrategy

        config = SamplingConfig(default_rate=0.5)
        sampler = MessageSampler(config)
        sampler.set_strategy(SamplingStrategy.HASH_BASED)

        results1 = [sampler.should_sample(f"msg_{i}").should_sample for i in range(100)]
        sampler2 = MessageSampler(config)
        sampler2.set_strategy(SamplingStrategy.HASH_BASED)
        results2 = [sampler2.should_sample(f"msg_{i}").should_sample for i in range(100)]

        self.assertEqual(results1, results2)

    def test_type_based_sampling(self):
        from sampling import MessageSampler, SamplingConfig

        config = SamplingConfig(default_rate=0.01, by_message_type={"critical": 1.0})
        sampler = MessageSampler(config)

        critical_result = sampler.should_sample("msg_1", message_type="critical")
        self.assertTrue(critical_result.should_sample)

        low_result = sampler.should_sample("msg_2", message_type="low")
        self.assertFalse(low_result.should_sample)


class TestAuditRecord(unittest.TestCase):
    def test_audit_record_creation(self):
        from audit import AuditRecord, AuditAction

        record = AuditRecord(
            message_id="test-123",
            exchange="test-exchange",
            routing_key="test-key",
            timestamp=time.time(),
            producer_ip="192.168.1.1",
            consumer_ip=None,
            message_body_size=1024,
            direction="producer_to_rabbitmq",
            action=AuditAction.PUBLISH.value
        )

        self.assertEqual(record.message_id, "test-123")
        self.assertEqual(record.exchange, "test-exchange")
        self.assertFalse(record.intercepted)

    def test_audit_record_to_dict(self):
        from audit import AuditRecord, AuditAction

        record = AuditRecord(
            message_id="test-123",
            exchange="test-exchange",
            routing_key="test-key",
            timestamp=time.time(),
            producer_ip="192.168.1.1",
            consumer_ip=None,
            message_body_size=1024,
            direction="producer_to_rabbitmq",
            action=AuditAction.PUBLISH.value
        )

        data = record.to_dict()
        self.assertIsInstance(data, dict)
        self.assertEqual(data["message_id"], "test-123")

    def test_audit_record_to_json(self):
        from audit import AuditRecord, AuditAction

        record = AuditRecord(
            message_id="test-123",
            exchange="test-exchange",
            routing_key="test-key",
            timestamp=time.time(),
            producer_ip="192.168.1.1",
            consumer_ip=None,
            message_body_size=1024,
            direction="producer_to_rabbitmq",
            action=AuditAction.PUBLISH.value
        )

        json_str = record.to_json()
        self.assertIsInstance(json_str, str)
        self.assertIn("test-123", json_str)


class TestAuditStats(unittest.TestCase):
    def test_stats_increment(self):
        from audit import AuditStats, AuditAction

        stats = AuditStats()
        stats.increment(AuditAction.PUBLISH)
        stats.increment(AuditAction.PUBLISH, 5)

        self.assertEqual(stats.total_published, 6)
        self.assertEqual(stats.total_messages, 6)

    def test_stats_track_exchange(self):
        from audit import AuditStats

        stats = AuditStats()
        stats.track_exchange("exchange1")
        stats.track_exchange("exchange1")
        stats.track_exchange("exchange2")

        self.assertEqual(stats.messages_by_exchange["exchange1"], 2)
        self.assertEqual(stats.messages_by_exchange["exchange2"], 1)

    def test_stats_get_summary(self):
        from audit import AuditStats, AuditAction

        stats = AuditStats()
        stats.increment(AuditAction.PUBLISH, 10)
        stats.increment(AuditAction.CONSUME, 8)
        stats.increment(AuditAction.INTERCEPT, 2)
        stats.add_bytes(5000)

        summary = stats.get_summary()

        self.assertEqual(summary["total_messages"], 20)
        self.assertEqual(summary["total_published"], 10)
        self.assertEqual(summary["total_consumed"], 8)
        self.assertEqual(summary["total_intercepted"], 2)
        self.assertEqual(summary["total_bytes"], 5000)


class TestHighAvailability(unittest.TestCase):
    def test_ha_config_defaults(self):
        config = HAConfig()
        self.assertEqual(config.mode, "active_standby")
        self.assertEqual(config.health_check_interval, 5)

    def test_lock_file_acquire_release(self):
        import tempfile
        from ha import LockFile

        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            lock_path = tmp.name

        try:
            lock = LockFile(lock_path)
            self.assertTrue(lock.acquire(timeout=1.0))
            self.assertTrue(lock.is_locked())
            lock.release()
            self.assertFalse(lock.is_locked())
        finally:
            if os.path.exists(lock_path):
                os.remove(lock_path)

    def test_ha_manager_initial_state(self):
        from ha import HighAvailabilityManager, HAConfig, NodeState

        config = HAConfig()
        manager = HighAvailabilityManager(config, "test-node-1")

        self.assertEqual(manager.get_state(), NodeState.INITIALIZING)
        self.assertFalse(manager.is_active())
        self.assertFalse(manager.is_standby())


class TestAuditBatch(unittest.TestCase):
    def test_batch_operations(self):
        from audit import AuditBatch, AuditRecord, AuditAction

        batch = AuditBatch()
        self.assertEqual(batch.size(), 0)

        record = AuditRecord(
            message_id="test-1",
            exchange="test",
            routing_key="key",
            timestamp=time.time(),
            producer_ip="127.0.0.1",
            consumer_ip=None,
            message_body_size=100,
            direction="test",
            action=AuditAction.PUBLISH.value
        )

        batch.add(record)
        self.assertEqual(batch.size(), 1)

        batch.clear()
        self.assertEqual(batch.size(), 0)


class TestOtelInstrumentation(unittest.TestCase):
    def test_noop_tracer(self):
        from otel import NoOpTracer, NoOpSpan

        tracer = NoOpTracer()
        span = tracer.start_span("test")

        span.set_attribute("key", "value")
        span.set_status("ok")
        span.end()

        self.assertIsInstance(span, NoOpSpan)

    def test_otel_disabled_config(self):
        from otel import OtelInstrumentation
        from config import OtelConfig

        class MockNestedConfig:
            enabled = False
            service_name = "test_service"
            exporter = "otlp"
            endpoint = "http://localhost:4317"
            insecure = True

        class MockOtelConfig:
            otel = MockNestedConfig()

        config = MockOtelConfig()
        otel = OtelInstrumentation(config)

        self.assertIsNotNone(otel.get_tracer())


class TestMessageSamplerStats(unittest.TestCase):
    def test_sampler_stats(self):
        from sampling import MessageSampler, SamplingConfig

        config = SamplingConfig(default_rate=0.5)
        sampler = MessageSampler(config)

        sampler.should_sample("msg_1")
        sampler.should_sample("msg_2")

        stats = sampler.get_stats()

        self.assertEqual(stats["default_rate"], 0.5)
        self.assertEqual(stats["strategy"], "random")


class TestInterceptionRuleMatching(unittest.TestCase):
    def test_rule_severity_score(self):
        from interception import InterceptionRule

        rule = InterceptionRule(
            id="test",
            name="Test",
            pattern="test",
            target="body",
            severity="high"
        )

        self.assertEqual(rule.get_severity_score(), 3)

    def test_rule_disabled(self):
        from interception import InterceptionRule

        rule = InterceptionRule(
            id="test",
            name="Test",
            pattern="test",
            target="body",
            enabled=False
        )

        should_block, _ = rule.matches("test pattern", {})
        self.assertFalse(should_block)


class TestPerformanceMonitor(unittest.TestCase):
    def test_operation_timing(self):
        from otel import PerformanceMonitor

        monitor = PerformanceMonitor()

        monitor.start_operation("op1")
        time.sleep(0.01)
        latency = monitor.end_operation("op1")

        self.assertIsNotNone(latency)
        self.assertGreater(latency, 0)

    def test_average_latency(self):
        from otel import PerformanceMonitor

        monitor = PerformanceMonitor()

        for i in range(5):
            monitor.start_operation(f"op_{i}")
            time.sleep(0.001)
            monitor.end_operation(f"op_{i}")

        avg = monitor.get_average_latency("op_0")
        self.assertIsNotNone(avg)
        self.assertGreater(avg, 0)


class TestRefCountedBody(unittest.TestCase):
    """测试零拷贝引用计数的消息体封装"""

    def test_ref_counted_body_basic(self):
        from audit import RefCountedBody

        body = b"test message content"
        ref_body = RefCountedBody(body)

        self.assertEqual(ref_body.size, len(body))
        self.assertEqual(ref_body.body, body)

    def test_ref_counted_body_retain_release(self):
        from audit import RefCountedBody

        body = b"test message content"
        ref_body = RefCountedBody(body)

        ref_body.retain()
        ref_body.release()
        # 引用计数应该还剩1，body应该还有效
        self.assertEqual(ref_body.body, body)

        ref_body.release()
        # 引用计数为0，body应该被释放
        self.assertEqual(ref_body.body, b'')
        self.assertEqual(ref_body.size, 0)

    def test_ref_counted_body_get_prefix_suffix(self):
        from audit import RefCountedBody

        body = b"a" * 10000
        ref_body = RefCountedBody(body)

        prefix = ref_body.get_prefix(100)
        self.assertEqual(len(prefix), 100)
        self.assertEqual(prefix, b"a" * 100)

        suffix = ref_body.get_suffix(100)
        self.assertEqual(len(suffix), 100)
        self.assertEqual(suffix, b"a" * 100)


class TestLargeMessageHandling(unittest.TestCase):
    """测试大消息处理逻辑"""

    def test_is_large_message(self):
        from config import AppConfig
        from audit import AuditManager, AuditStats

        config = AppConfig()
        config.audit.max_body_size = 1024  # 1KB

        audit_manager = AuditManager(config, [], AuditStats())

        self.assertTrue(audit_manager.is_large_message(2048))
        self.assertFalse(audit_manager.is_large_message(512))

    def test_create_audit_record_large_message(self):
        from config import AppConfig
        from audit import AuditManager, AuditStats, AuditAction

        config = AppConfig()
        config.audit.max_body_size = 1024  # 1KB
        config.audit.preview_size = 100

        audit_manager = AuditManager(config, [], AuditStats())

        # 大消息
        large_body = b"x" * 2048
        record = audit_manager.create_audit_record(
            message_id="test_large",
            exchange="test",
            routing_key="test",
            producer_ip="127.0.0.1",
            consumer_ip=None,
            body=large_body,
            ref_body=None,
            direction="test",
            action=AuditAction.PUBLISH,
            headers={}
        )

        self.assertTrue(record.is_large_message)
        self.assertTrue(record.content_skipped)
        self.assertIsNone(record.body_preview)

    def test_create_audit_record_small_message(self):
        from config import AppConfig
        from audit import AuditManager, AuditStats, AuditAction

        config = AppConfig()
        config.audit.max_body_size = 1024  # 1KB
        config.audit.preview_size = 100

        audit_manager = AuditManager(config, [], AuditStats())

        # 小消息
        small_body = b"hello world"
        record = audit_manager.create_audit_record(
            message_id="test_small",
            exchange="test",
            routing_key="test",
            producer_ip="127.0.0.1",
            consumer_ip=None,
            body=small_body,
            ref_body=None,
            direction="test",
            action=AuditAction.PUBLISH,
            headers={}
        )

        self.assertFalse(record.is_large_message)
        self.assertFalse(record.content_skipped)
        self.assertIsNotNone(record.body_preview)


class TestInterceptionPrefixSuffix(unittest.TestCase):
    """测试拦截引擎只检查前4KB+后4KB"""

    def setUp(self):
        self.rules_path = os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            'config', 'interception_rules.yaml'
        )
        self.config = InterceptionConfig(
            enabled=True,
            rules_file=self.rules_path,
            block_action="drop",
            log_blocked=True,
            check_prefix_size=4096,
            check_suffix_size=4096
        )

    def test_extract_check_portion_small(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        body = b"small message"
        portion = engine._extract_check_portion(body)

        self.assertIn("small message", portion)

    def test_extract_check_portion_large(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        prefix_content = b"SQL_INJECTION_TEST"
        suffix_content = b"XSS_ATTACK"
        body = prefix_content + b"x" * 10000 + suffix_content

        portion = engine._extract_check_portion(body)

        self.assertIn("SQL_INJECTION_TEST", portion)
        self.assertIn("XSS_ATTACK", portion)

    def test_should_block_bytes(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        body = b"SELECT * FROM users"
        headers = {}

        should_block, rule_id, rule_name = engine.should_block_bytes(body, headers)

        self.assertTrue(should_block)

    def test_should_block_bytes_large(self):
        from interception import InterceptionEngine

        engine = InterceptionEngine(self.config)

        # 大消息，攻击特征在末尾
        suffix_content = b"<script>alert('xss')</script>"
        body = b"x" * 10000 + suffix_content

        headers = {}
        should_block, rule_id, rule_name = engine.should_block_bytes(body, headers)

        self.assertTrue(should_block)


class TestLargeMessageSampling(unittest.TestCase):
    """测试大消息自动降采样"""

    def test_is_large_message(self):
        from sampling import MessageSampler, SamplingConfig

        config = SamplingConfig(
            default_rate=0.5,
            large_message_threshold=1024,
            large_message_rate=0.01
        )
        sampler = MessageSampler(config)

        self.assertTrue(sampler.is_large_message(2048))
        self.assertFalse(sampler.is_large_message(512))

    def test_large_message_uses_lower_rate(self):
        from sampling import MessageSampler, SamplingConfig, SamplingStrategy

        config = SamplingConfig(
            default_rate=1.0,  # 正常消息100%采样
            large_message_threshold=1024,
            large_message_rate=0.0  # 大消息0%采样
        )
        sampler = MessageSampler(config)
        sampler.set_strategy(SamplingStrategy.RANDOM)

        # 小消息应该被采样
        small_result = sampler.should_sample("msg_small", body_size=100)
        self.assertTrue(small_result.should_sample)

        # 大消息不应该被采样
        large_result = sampler.should_sample("msg_large", body_size=2048)
        self.assertFalse(large_result.should_sample)

    def test_large_message_type_based(self):
        from sampling import MessageSampler, SamplingConfig, SamplingStrategy

        config = SamplingConfig(
            default_rate=1.0,
            large_message_threshold=1024,
            large_message_rate=0.0,
            by_message_type={"critical": 1.0}
        )
        sampler = MessageSampler(config)
        sampler.set_strategy(SamplingStrategy.TYPE_BASED)

        # 即使是critical类型，大消息也使用降低的采样率
        large_result = sampler.should_sample("msg_critical_large", message_type="critical", body_size=2048)
        self.assertFalse(large_result.should_sample)


class TestAuditStatsLargeMessages(unittest.TestCase):
    """测试大消息统计"""

    def test_track_large_message(self):
        from audit import AuditStats

        stats = AuditStats()
        stats.track_large_message()
        stats.track_large_message()
        stats.track_content_skipped()

        summary = stats.get_summary()

        self.assertEqual(summary["total_large_messages"], 2)
        self.assertEqual(summary["total_content_skipped"], 1)


class TestAttachmentExtractor(unittest.TestCase):
    """测试附件提取功能"""

    def test_extract_base64_attachment(self):
        from virus_scan import AttachmentExtractor

        import base64
        test_data = b"test binary data for virus scanning" * 10
        encoded = base64.b64encode(test_data).decode()

        body = f"data:application/pdf;base64,{encoded}".encode()

        attachments = AttachmentExtractor.extract_base64_attachments(body)

        self.assertEqual(len(attachments), 1)
        mime_type, decoded = attachments[0]
        self.assertEqual(mime_type, "application/pdf")
        self.assertEqual(decoded, test_data)

    def test_check_base64_patterns_positive(self):
        from virus_scan import AttachmentExtractor

        import base64
        test_data = b"test data" * 20
        encoded = base64.b64encode(test_data).decode()

        body = f"data:image/png;base64,{encoded}".encode()

        self.assertTrue(AttachmentExtractor.check_base64_patterns(body))

    def test_check_base64_patterns_negative(self):
        from virus_scan import AttachmentExtractor

        body = b"normal text content without base64 data"

        self.assertFalse(AttachmentExtractor.check_base64_patterns(body))


class TestVirusScanEngine(unittest.TestCase):
    """测试病毒扫描引擎"""

    def setUp(self):
        self.scanners = []

    def test_scan_result_creation(self):
        from virus_scan import ScanResult

        result = ScanResult(
            message_id="test_msg_1",
            is_infected=True,
            virus_name="Test.Virus",
            scan_duration_ms=100.0,
            scanner="test_scanner"
        )

        self.assertTrue(result.is_infected)
        self.assertEqual(result.virus_name, "Test.Virus")
        self.assertEqual(result.scanner, "test_scanner")

    def test_scan_result_to_dict(self):
        from virus_scan import ScanResult

        result = ScanResult(
            message_id="test_msg_1",
            is_infected=False,
            scan_duration_ms=50.0,
            scanner="test_scanner"
        )

        result_dict = result.to_dict()

        self.assertEqual(result_dict["message_id"], "test_msg_1")
        self.assertFalse(result_dict["is_infected"])

    def test_quarantine_message(self):
        from virus_scan import QuarantineMessage

        msg = QuarantineMessage(
            message_id="test_quarantine_1",
            virus_name="Test.Virus",
            original_exchange="test_exchange",
            original_routing_key="test.routing.key",
            body=b"infected content",
            headers={"x-test": "value"}
        )

        self.assertEqual(msg.message_id, "test_quarantine_1")
        self.assertEqual(msg.virus_name, "Test.Virus")

    def test_virus_scan_stats(self):
        from virus_scan import VirusScanStats

        stats = VirusScanStats()
        stats.increment_scanned("clamav")
        stats.increment_scanned("clamav")
        stats.increment_infected("clamav")
        stats.increment_scanned("yara")
        stats.record_scan_duration(100.0)

        result = stats.get_stats()

        self.assertEqual(result["total_scanned"], 3)
        self.assertEqual(result["total_infected"], 1)

    def test_scan_mode_enum(self):
        from virus_scan import ScanMode

        self.assertEqual(ScanMode.SYNC.value, "sync")
        self.assertEqual(ScanMode.ASYNC.value, "async")


class TestVirusScanConfig(unittest.TestCase):
    """测试病毒扫描配置"""

    def test_virus_scan_config_defaults(self):
        from config import VirusScanConfig, ClamAVConfig, YARAConfig

        config = VirusScanConfig()

        self.assertFalse(config.enabled)
        self.assertEqual(config.scan_mode, "async")
        self.assertEqual(config.scan_timeout, 5)
        self.assertEqual(config.quarantine_exchange, "audit.quarantine")
        self.assertTrue(config.auto_quarantine)
        self.assertEqual(config.max_threads, 4)

    def test_clamav_config_defaults(self):
        from config import ClamAVConfig

        config = ClamAVConfig()

        self.assertFalse(config.enabled)
        self.assertEqual(config.host, "localhost")
        self.assertEqual(config.port, 3310)

    def test_yara_config_defaults(self):
        from config import YARAConfig

        config = YARAConfig()

        self.assertFalse(config.enabled)
        self.assertEqual(config.rules_dir, "yara_rules")


class TestClamAVScanner(unittest.TestCase):
    """测试ClamAV扫描器"""

    def test_clamav_scanner_creation(self):
        from virus_scan import ClamAVScanner

        scanner = ClamAVScanner(host="localhost", port=3310)

        self.assertEqual(scanner.get_name(), "clamav")
        self.assertIsInstance(scanner.is_available(), bool)

    def test_clamav_scanner_scan_method(self):
        from virus_scan import ClamAVScanner

        scanner = ClamAVScanner(host="localhost", port=3310)

        is_infected, virus_name = scanner.scan(b"test data")

        self.assertIsInstance(is_infected, bool)
        self.assertIsInstance(virus_name, (str, type(None)))


class TestYARAScanner(unittest.TestCase):
    """测试YARA扫描器"""

    def test_yara_scanner_creation(self):
        from virus_scan import YARAScanner

        scanner = YARAScanner(rules_dir="/tmp/nonexistent_yara_rules")

        self.assertEqual(scanner.get_name(), "yara")
        self.assertIsInstance(scanner.is_available(), bool)

    def test_yara_scanner_scan_method(self):
        from virus_scan import YARAScanner

        scanner = YARAScanner(rules_dir="/tmp/nonexistent_yara_rules")

        is_infected, virus_name = scanner.scan(b"test data")

        self.assertIsInstance(is_infected, bool)
        self.assertIsInstance(virus_name, (str, type(None)))


class TestVirusDatabaseUpdater(unittest.TestCase):
    """测试病毒数据库更新"""

    def test_updater_creation(self):
        from virus_scan import VirusDatabaseUpdater, VirusScannerInterface

        scanners = []

        updater = VirusDatabaseUpdater(scanners, update_interval_hours=24)

        self.assertIsNotNone(updater)

    def test_updater_start_stop(self):
        from virus_scan import VirusDatabaseUpdater

        updater = VirusDatabaseUpdater([], update_interval_hours=24)

        updater.start()
        import time
        time.sleep(0.1)
        updater.stop()

        self.assertTrue(True)


class TestVirusScanEngineIntegration(unittest.TestCase):
    """测试病毒扫描引擎集成"""

    def test_engine_creation_disabled(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=False,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        self.assertFalse(engine.enabled)

    def test_engine_should_scan_small_message(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            min_attachment_size=1024
        )

        result = engine.should_scan(b"small", {})

        self.assertFalse(result)

    def test_engine_get_status(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        status = engine.get_status()

        self.assertIn("enabled", status)
        self.assertIn("scanners", status)
        self.assertIn("stats", status)

    def test_engine_get_quarantine_messages(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        messages = engine.get_quarantine_messages()

        self.assertIsInstance(messages, list)

    def test_engine_stop(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        engine.stop()

        self.assertTrue(True)


class TestGatewayVirusScanIntegration(unittest.TestCase):
    """测试网关与病毒扫描集成"""

    def test_gateway_get_virus_scan_status(self):
        from config import AppConfig
        from virus_scan import VirusScanEngine, ScanMode

        config = AppConfig()
        config.virus_scan.enabled = False

        engine = VirusScanEngine(
            enabled=False,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        status = engine.get_status()

        self.assertFalse(status["enabled"])

    def test_gateway_get_quarantine_messages(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        messages = engine.get_quarantine_messages()

        self.assertEqual(len(messages), 0)

    def test_gateway_register_virus_alert_callback(self):
        from virus_scan import VirusScanEngine, ScanMode

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=ScanMode.ASYNC,
            scanners=[],
            max_threads=2
        )

        called = []

        def mock_callback(message_id, body, headers, result):
            called.append(True)

        engine.add_notify_callback(mock_callback)

        self.assertTrue(True)


if __name__ == '__main__':
    unittest.main()
