import asyncio
import logging
import time
import uuid
from typing import Dict, Optional, List
from flowspec_controller import FlowSpecRule

logger = logging.getLogger(__name__)

class ActiveRule:
    def __init__(self, rules: List[FlowSpecRule], attack_type: str, tier: dict, rate_gbps: float, strategy: dict = None):
        self.rules = rules
        self.attack_type = attack_type
        self.tier = tier
        self.rate_gbps = rate_gbps
        self.strategy = strategy or {}
        self.created_at = time.time()
        self.last_seen = time.time()
        self.last_rate_upgrades = 0
        self.withdrawn = False

class RuleManager:
    def __init__(self, config, influxdb_writer):
        self.config = config
        self.influxdb_writer = influxdb_writer
        self.active_rules: Dict[str, ActiveRule] = {}
        self.flowspec_controller = None
        self.manual_escalation = False
        self.dst_ip_to_rule_id: Dict[str, str] = {}
    
    def set_flowspec_controller(self, controller):
        self.flowspec_controller = controller
    
    def trigger_diversion(self, record, attack_type: str, rate_gbps: float = 0.0):
        dst_ip = record.dst_ip
        
        if dst_ip in self.dst_ip_to_rule_id:
            rule_id = self.dst_ip_to_rule_id[dst_ip]
            if rule_id in self.active_rules:
                self.active_rules[rule_id].last_seen = time.time()
                self.active_rules[rule_id].rate_gbps = rate_gbps
                if self.config.escalation_enabled:
                    asyncio.create_task(self._check_and_upgrade(rule_id))
                return
        
        rule_id = str(uuid.uuid4())
        tier = self.config.get_tier_for_rate(rate_gbps)
        
        rules = self._create_rules_for_tier(rule_id, record, tier)
        
        self.active_rules[rule_id] = ActiveRule(rules, attack_type, tier, rate_gbps)
        self.dst_ip_to_rule_id[dst_ip] = rule_id
        
        logger.info("New diversion rule created", extra={
            "rule_id": rule_id,
            "attack_type": attack_type,
            "dst_ip": dst_ip,
            "src_ip": record.src_ip,
            "tier_level": tier['level'],
            "tier_name": tier['name'],
            "rate_gbps": rate_gbps
        })
        
        asyncio.create_task(self._apply_rules(rule_id, attack_type))
    
    def trigger_diversion_with_strategy(self, record, attack_type: str, rate_gbps: float, strategy: dict):
        dst_ip = record.dst_ip
        
        if dst_ip in self.dst_ip_to_rule_id:
            rule_id = self.dst_ip_to_rule_id[dst_ip]
            if rule_id in self.active_rules:
                active_rule = self.active_rules[rule_id]
                active_rule.last_seen = time.time()
                active_rule.rate_gbps = rate_gbps
                
                if self._should_upgrade_for_attack_type(active_rule, strategy):
                    asyncio.create_task(self._upgrade_rule_for_attack_type(rule_id, record, strategy))
                elif self.config.escalation_enabled:
                    asyncio.create_task(self._check_and_upgrade(rule_id))
                return
        
        rule_id = str(uuid.uuid4())
        tier = self._get_tier_from_strategy(strategy, rate_gbps)
        
        rules = self._create_rules_for_attack_type(rule_id, record, attack_type, strategy)
        
        self.active_rules[rule_id] = ActiveRule(rules, attack_type, tier, rate_gbps, strategy)
        self.dst_ip_to_rule_id[dst_ip] = rule_id
        
        logger.info("ML-based diversion rule created", extra={
            "rule_id": rule_id,
            "attack_type": attack_type,
            "dst_ip": dst_ip,
            "src_ip": record.src_ip,
            "tier_level": tier['level'] if tier else 'none',
            "rate_gbps": rate_gbps,
            "strategy": strategy.get('description', '')
        })
        
        asyncio.create_task(self._apply_rules(rule_id, attack_type))
    
    def _should_upgrade_for_attack_type(self, active_rule: ActiveRule, new_strategy: dict) -> bool:
        if not active_rule.strategy:
            return False
        
        current_action = active_rule.strategy.get('action', 'redirect')
        new_action = new_strategy.get('action', 'redirect')
        
        if current_action == 'redirect' and new_action in ['rate-limit', 'discard']:
            return True
        if current_action == 'rate-limit' and new_action == 'discard':
            return True
        
        return False
    
    def _get_tier_from_strategy(self, strategy: dict, rate_gbps: float) -> Optional[dict]:
        action = strategy.get('action', 'redirect')
        
        if action == 'discard':
            return next((t for t in self.config.tiered_policy if t['level'] == 3), self.config.tiered_policy[-1])
        elif action == 'rate-limit':
            return next((t for t in self.config.tiered_policy if t['level'] == 2), self.config.tiered_policy[1])
        elif action == 'redirect':
            tier_from_rate = self.config.get_tier_for_rate(rate_gbps)
            return next((t for t in self.config.tiered_policy if t['level'] == max(1, tier_from_rate['level'])), self.config.tiered_policy[0])
        
        return self.config.tiered_policy[0]
    
    def _create_rules_for_attack_type(self, base_rule_id: str, record, attack_type: str, strategy: dict) -> List[FlowSpecRule]:
        rules = []
        action = strategy.get('action', 'redirect')
        
        if attack_type == 'http_flood':
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-waf-redirect",
                src_ip=record.src_ip,
                dst_ip=record.dst_ip,
                protocol=6,
                src_port=None,
                dst_port=80,
                action="redirect",
                redirect_next_hop=self.config.waf_ip
            ))
            if record.dst_port == 443 or record.dst_port == 8080:
                rules.append(FlowSpecRule(
                    rule_id=f"{base_rule_id}-waf-redirect-443",
                    src_ip=record.src_ip,
                    dst_ip=record.dst_ip,
                    protocol=6,
                    src_port=None,
                    dst_port=443,
                    action="redirect",
                    redirect_next_hop=self.config.waf_ip
                ))
        
        elif attack_type in ['udp_amplification', 'ntp_reflection']:
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-discard-udp",
                src_ip=None,
                dst_ip=record.dst_ip,
                protocol=17,
                src_port=None,
                dst_port=None,
                action="discard"
            ))
        
        elif attack_type == 'dns_query_flood':
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-rate-limit-dns",
                src_ip=None,
                dst_ip=record.dst_ip,
                protocol=17,
                src_port=None,
                dst_port=53,
                action="rate-limit",
                rate_limit=100
            ))
        
        elif attack_type == 'syn_flood':
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-redirect-tcp",
                src_ip=None,
                dst_ip=record.dst_ip,
                protocol=6,
                src_port=None,
                dst_port=None,
                action="redirect",
                redirect_next_hop=self.config.redirect_next_hop
            ))
        
        else:
            if action == "redirect":
                rules.append(FlowSpecRule(
                    rule_id=f"{base_rule_id}-redirect",
                    src_ip=record.src_ip,
                    dst_ip=record.dst_ip,
                    protocol=None,
                    src_port=None,
                    dst_port=None,
                    action="redirect",
                    redirect_next_hop=self.config.redirect_next_hop
                ))
            elif action == "rate-limit":
                rules.append(FlowSpecRule(
                    rule_id=f"{base_rule_id}-ratelimit",
                    src_ip=record.src_ip,
                    dst_ip=record.dst_ip,
                    protocol=None,
                    src_port=None,
                    dst_port=None,
                    action="rate-limit",
                    rate_limit=strategy.get('rate_limit', 50)
                ))
            elif action == "discard":
                rules.append(FlowSpecRule(
                    rule_id=f"{base_rule_id}-discard",
                    src_ip=record.src_ip,
                    dst_ip=record.dst_ip,
                    protocol=None,
                    src_port=None,
                    dst_port=None,
                    action="discard"
                ))
        
        return rules
    
    def _create_rules_for_tier(self, base_rule_id: str, record, tier: dict) -> List[FlowSpecRule]:
        rules = []
        
        if tier['action'] == "redirect":
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-redirect",
                src_ip=record.src_ip,
                dst_ip=record.dst_ip,
                protocol=None,
                src_port=None,
                dst_port=None,
                action="redirect",
                redirect_next_hop=self.config.redirect_next_hop
            ))
        
        elif tier['action'] == "rate-limit":
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-redirect",
                src_ip=record.src_ip,
                dst_ip=record.dst_ip,
                protocol=None,
                src_port=None,
                dst_port=None,
                action="redirect",
                redirect_next_hop=self.config.redirect_next_hop
            ))
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-ratelimit",
                src_ip=record.src_ip,
                dst_ip=record.dst_ip,
                protocol=None,
                src_port=None,
                dst_port=None,
                action="rate-limit",
                rate_limit=tier['rate_limit']
            ))
        
        elif tier['action'] == "selective-drop":
            rules.append(FlowSpecRule(
                rule_id=f"{base_rule_id}-drop-udp",
                src_ip=record.src_ip,
                dst_ip=record.dst_ip,
                protocol=17,
                src_port=None,
                dst_port=None,
                action="discard"
            ))
        
        return rules
    
    async def _apply_rules(self, rule_id: str, attack_type: str):
        if rule_id not in self.active_rules:
            return
        
        active_rule = self.active_rules[rule_id]
        for rule in active_rule.rules:
            if self.flowspec_controller:
                success = await self.flowspec_controller.add_rule(rule)
                if success:
                    await self.influxdb_writer.write_diversion_event(
                        rule,
                        f"{attack_type}-tier{active_rule.tier['level'] if active_rule.tier else 'unknown'}",
                        "applied"
                    )
    
    async def _check_and_upgrade(self, rule_id: str):
        if rule_id not in self.active_rules:
            return
        
        active_rule = self.active_rules[rule_id]
        current_tier_level = active_rule.tier['level'] if active_rule.tier else 1
        current_rate = active_rule.rate_gbps
        
        correct_tier = self.config.get_tier_for_rate(current_rate)
        
        if correct_tier['level'] > current_tier_level:
            active_rule.last_rate_upgrades += 1
            if active_rule.last_rate_upgrades >= self.config.escalation_upgrade_threshold:
                logger.warning("Upgrading rule tier", extra={
                    "rule_id": rule_id,
                    "old_tier": current_tier_level,
                    "new_tier": correct_tier['level'],
                    "rate_gbps": current_rate
                })
                await self._upgrade_rule(rule_id, correct_tier)
    
    async def _upgrade_rule(self, rule_id: str, new_tier: dict):
        if rule_id not in self.active_rules:
            return
        
        active_rule = self.active_rules[rule_id]
        
        for rule in active_rule.rules:
            if self.flowspec_controller:
                await self.flowspec_controller.remove_rule(rule)
        
        dummy_record = type('', (), {
            'src_ip': active_rule.rules[0].src_ip if active_rule.rules else '0.0.0.0',
            'dst_ip': active_rule.rules[0].dst_ip if active_rule.rules else '0.0.0.0',
            'protocol': None,
            'src_port': None,
            'dst_port': None
        })()
        
        new_rules = self._create_rules_for_tier(rule_id, dummy_record, new_tier)
        
        active_rule.rules = new_rules
        active_rule.tier = new_tier
        active_rule.last_rate_upgrades = 0
        
        for rule in new_rules:
            if self.flowspec_controller:
                await self.flowspec_controller.add_rule(rule)
        
        await self.influxdb_writer.write_diversion_event(
            new_rules[0] if new_rules else None,
            f"{active_rule.attack_type}-tier{new_tier['level']}",
            "upgraded"
        )
    
    async def _upgrade_rule_for_attack_type(self, rule_id: str, record, strategy: dict):
        if rule_id not in self.active_rules:
            return
        
        active_rule = self.active_rules[rule_id]
        
        for rule in active_rule.rules:
            if self.flowspec_controller:
                await self.flowspec_controller.remove_rule(rule)
        
        new_tier = self._get_tier_from_strategy(strategy, active_rule.rate_gbps)
        new_rules = self._create_rules_for_attack_type(rule_id, record, active_rule.attack_type, strategy)
        
        active_rule.rules = new_rules
        active_rule.tier = new_tier
        active_rule.strategy = strategy
        active_rule.last_rate_upgrades = 0
        
        for rule in new_rules:
            if self.flowspec_controller:
                await self.flowspec_controller.add_rule(rule)
        
        logger.info("Rule upgraded based on ML strategy", extra={
            "rule_id": rule_id,
            "attack_type": active_rule.attack_type,
            "new_tier": new_tier['level'] if new_tier else 'unknown'
        })
    
    async def escalate_rule(self, dst_ip: str):
        if dst_ip not in self.dst_ip_to_rule_id:
            logger.warning("No active rule found for destination IP", extra={"dst_ip": dst_ip})
            return False
        
        rule_id = self.dst_ip_to_rule_id[dst_ip]
        if rule_id not in self.active_rules:
            return False
        
        active_rule = self.active_rules[rule_id]
        current_level = active_rule.tier['level'] if active_rule.tier else 1
        max_level = self.config.tiered_policy[-1]['level']
        
        if current_level >= max_level:
            logger.warning("Rule already at maximum tier", extra={"rule_id": rule_id})
            return False
        
        next_tier = next(t for t in self.config.tiered_policy if t['level'] == current_level + 1)
        logger.info("Manually escalating rule", extra={
            "rule_id": rule_id,
            "dst_ip": dst_ip,
            "old_tier": current_level,
            "new_tier": next_tier['level']
        })
        
        await self._upgrade_rule(rule_id, next_tier)
        return True
    
    async def revoke_rule(self, rule_id: str):
        if rule_id in self.active_rules and not self.active_rules[rule_id].withdrawn:
            active_rule = self.active_rules[rule_id]
            active_rule.withdrawn = True
            
            for dst_ip, rid in list(self.dst_ip_to_rule_id.items()):
                if rid == rule_id:
                    del self.dst_ip_to_rule_id[dst_ip]
                    break
            
            for rule in active_rule.rules:
                if self.flowspec_controller:
                    await self.flowspec_controller.remove_rule(rule)
                    await self.influxdb_writer.write_diversion_event(
                        rule,
                        active_rule.attack_type,
                        "revoked"
                    )
            
            del self.active_rules[rule_id]
            logger.info("Rule revoked", extra={"rule_id": rule_id})
    
    async def cleanup_expired_rules(self):
        while True:
            current_time = time.time()
            expired_rules = []
            
            for rule_id, active_rule in list(self.active_rules.items()):
                if current_time - active_rule.last_seen > self.config.rule_expire_seconds:
                    expired_rules.append(rule_id)
            
            for rule_id in expired_rules:
                await self.revoke_rule(rule_id)
            
            await asyncio.sleep(5)
    
    def get_active_rules(self) -> List[Dict]:
        rules_list = []
        for rule_id, active_rule in self.active_rules.items():
            rules_list.append({
                "rule_id": rule_id,
                "attack_type": active_rule.attack_type,
                "tier_level": active_rule.tier['level'] if active_rule.tier else 0,
                "tier_name": active_rule.tier['name'] if active_rule.tier else 'none',
                "rate_gbps": active_rule.rate_gbps,
                "strategy": active_rule.strategy,
                "created_at": active_rule.created_at,
                "last_seen": active_rule.last_seen,
                "num_rules": len(active_rule.rules),
                "dst_ips": list(set(r.dst_ip for r in active_rule.rules))
            })
        return rules_list
    
    def get_stats(self) -> Dict:
        return {
            "total_active_rules": len(self.active_rules),
            "dry_run": self.config.dry_run,
            "escalation_enabled": self.config.escalation_enabled,
            "ml_enabled": self.config.ml_enabled
        }
