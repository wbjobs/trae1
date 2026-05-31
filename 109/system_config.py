class SystemConfig:
    def __init__(self, config_dict):
        self.config = config_dict

        self.name = config_dict['ddos_mitigator']['name']
        self.dry_run = config_dict['ddos_mitigator']['dry_run']

        self.sflow_port = config_dict['sflow']['listen_port']
        self.sflow_address = config_dict['sflow']['listen_address']
        self.sampling_rate = config_dict['sflow']['sampling_rate']

        self.traffic_multiplier = config_dict['threshold']['traffic_multiplier']
        self.baseline_window = config_dict['threshold']['baseline_window']
        self.detection_window = config_dict['threshold']['detection_window']
        self.rule_expire_seconds = config_dict['threshold']['rule_expire_seconds']

        self.escalation_enabled = config_dict['escalation']['enabled']
        self.escalation_check_interval = config_dict['escalation']['check_interval']
        self.escalation_upgrade_threshold = config_dict['escalation']['upgrade_threshold']

        self.ml_enabled = config_dict['ml']['enabled']
        self.ml_model_path = config_dict['ml']['model_path']
        self.ml_confidence_threshold = config_dict['ml']['confidence_threshold']
        self.ml_retrain_interval_days = config_dict['ml']['retrain_interval_days']
        self.ml_target_accuracy = config_dict['ml']['target_accuracy']
        self.ml_inference_timeout_ms = config_dict['ml']['inference_timeout_ms']
        self.ml_feature_window_seconds = config_dict['ml']['feature_window_seconds']

        self.tiered_policy = config_dict['tiered_policy']['tiers']

        self.exabgp_socket = config_dict['exabgp']['socket_path']
        self.local_as = config_dict['exabgp']['local_as']
        self.local_ip = config_dict['exabgp']['local_ip']
        self.router_ip = config_dict['exabgp']['router_ip']
        self.router_as = config_dict['exabgp']['router_as']

        self.influxdb_url = config_dict['influxdb']['url']
        self.influxdb_token = config_dict['influxdb']['token']
        self.influxdb_org = config_dict['influxdb']['org']
        self.influxdb_bucket = config_dict['influxdb']['bucket']

        self.api_host = config_dict['api']['host']
        self.api_port = config_dict['api']['port']

        self.scrubbing_center_ip = config_dict['scrubbing_center']['ip']
        self.redirect_next_hop = config_dict['scrubbing_center']['redirect_next_hop']
        self.waf_ip = config_dict['scrubbing_center']['waf_ip']

    def get_tier_for_rate(self, rate_gbps):
        """根据流量速率获取对应的策略级别"""
        for tier in self.tiered_policy:
            if rate_gbps <= tier['max_rate']:
                return tier
        return self.tiered_policy[-1]

    def get_attack_type_tier(self, attack_type: str):
        """根据攻击类型获取推荐的策略级别"""
        attack_tier_mapping = {
            'syn_flood': 1,
            'udp_amplification': 3,
            'http_flood': 2,
            'dns_query_flood': 2,
            'ntp_reflection': 3,
            'benign': 0
        }
        tier_level = attack_tier_mapping.get(attack_type, 1)
        if tier_level == 0:
            return None
        return next(t for t in self.tiered_policy if t['level'] == tier_level)
