import logging
import socket
import json
from typing import Dict, Any

logger = logging.getLogger(__name__)

class FlowSpecRule:
    def __init__(self, rule_id: str, src_ip: str, dst_ip: str, protocol: int, 
                 src_port: int, dst_port: int, action: str, rate_limit: int = None,
                 redirect_next_hop: str = None):
        self.rule_id = rule_id
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.protocol = protocol
        self.src_port = src_port
        self.dst_port = dst_port
        self.action = action
        self.rate_limit = rate_limit
        self.redirect_next_hop = redirect_next_hop
    
    def to_exabgp_command(self):
        match_parts = []
        
        if self.src_ip and self.src_ip != "0.0.0.0":
            match_parts.append(f"source {self.src_ip}/32")
        
        if self.dst_ip and self.dst_ip != "0.0.0.0":
            match_parts.append(f"destination {self.dst_ip}/32")
        
        if self.protocol:
            proto_name = {6: "tcp", 17: "udp", 1: "icmp"}.get(self.protocol, str(self.protocol))
            match_parts.append(f"protocol {proto_name}")
        
        if self.dst_port:
            match_parts.append(f"destination-port {self.dst_port}")
        
        if self.src_port:
            match_parts.append(f"source-port {self.src_port}")
        
        match_str = " then ".join(match_parts) if match_parts else "destination 0.0.0.0/0"
        
        action_str = self._get_action_string()
        
        return f"announce flow route {match_str} then {action_str}"
    
    def _get_action_string(self):
        if self.action == "discard":
            return "discard"
        elif self.action == "rate-limit":
            return f"rate-limit {self.rate_limit}"
        elif self.action == "redirect":
            if self.redirect_next_hop:
                return f"redirect nexthop {self.redirect_next_hop}"
            return "redirect"
        return "discard"
    
    def to_withdraw_command(self):
        match_parts = []
        
        if self.src_ip and self.src_ip != "0.0.0.0":
            match_parts.append(f"source {self.src_ip}/32")
        
        if self.dst_ip and self.dst_ip != "0.0.0.0":
            match_parts.append(f"destination {self.dst_ip}/32")
        
        if self.protocol:
            proto_name = {6: "tcp", 17: "udp", 1: "icmp"}.get(self.protocol, str(self.protocol))
            match_parts.append(f"protocol {proto_name}")
        
        match_str = " then ".join(match_parts) if match_parts else "destination 0.0.0.0/0"
        return f"withdraw flow route {match_str}"

class FlowSpecController:
    def __init__(self, config, rule_manager):
        self.config = config
        self.rule_manager = rule_manager
        self.connected = False
        self.sock = None
    
    async def connect(self):
        if self.config.dry_run:
            logger.info("Dry-run mode: skipping ExaBGP connection")
            self.connected = True
            return True
        
        try:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(self.config.exabgp_socket)
            self.connected = True
            logger.info("Connected to ExaBGP")
            return True
        except Exception as e:
            logger.error("Failed to connect to ExaBGP", extra={"error": str(e)})
            return False
    
    async def add_rule(self, rule: FlowSpecRule):
        command = rule.to_exabgp_command()
        
        if self.config.dry_run:
            logger.info("Dry-run mode: would send command", extra={"command": command, "rule_id": rule.rule_id})
            return True
        
        if not self.connected:
            await self.connect()
        
        try:
            self.sock.sendall((command + "\n").encode())
            logger.info("Sent FlowSpec rule", extra={"rule_id": rule.rule_id, "command": command})
            return True
        except Exception as e:
            logger.error("Failed to send FlowSpec rule", extra={"error": str(e), "rule_id": rule.rule_id})
            return False
    
    async def remove_rule(self, rule: FlowSpecRule):
        command = rule.to_withdraw_command()
        
        if self.config.dry_run:
            logger.info("Dry-run mode: would send withdraw command", extra={"command": command, "rule_id": rule.rule_id})
            return True
        
        if not self.connected:
            await self.connect()
        
        try:
            self.sock.sendall((command + "\n").encode())
            logger.info("Sent FlowSpec withdraw", extra={"rule_id": rule.rule_id, "command": command})
            return True
        except Exception as e:
            logger.error("Failed to send FlowSpec withdraw", extra={"error": str(e), "rule_id": rule.rule_id})
            return False
