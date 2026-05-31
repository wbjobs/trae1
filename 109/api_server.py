import asyncio
import logging
from fastapi import FastAPI, HTTPException
from fastapi.responses import JSONResponse
from pydantic import BaseModel
from typing import List, Dict, Optional
import uvicorn

logger = logging.getLogger(__name__)

app = FastAPI(title="DDoS Traffic Diversion API", version="2.0.0")

class RuleAction(BaseModel):
    rule_id: str

class DstIpAction(BaseModel):
    dst_ip: str

class DiversionRule(BaseModel):
    src_ip: Optional[str] = None
    dst_ip: str
    protocol: Optional[int] = None
    src_port: Optional[int] = None
    dst_port: Optional[int] = None
    action: str = "discard"
    rate_limit: Optional[int] = None

class APIServer:
    def __init__(self, config, rule_manager):
        self.config = config
        self.rule_manager = rule_manager
        self._setup_routes()
    
    def _setup_routes(self):
        @app.get("/api/v1/health")
        async def health_check():
            return {"status": "healthy"}
        
        @app.get("/api/v1/rules")
        async def get_active_rules():
            rules = self.rule_manager.get_active_rules()
            return {"rules": rules, "count": len(rules)}
        
        @app.get("/api/v1/stats")
        async def get_stats():
            stats = self.rule_manager.get_stats()
            stats["tiered_policy"] = self.config.tiered_policy
            return stats
        
        @app.get("/api/v1/policy")
        async def get_policy():
            return {"tiered_policy": self.config.tiered_policy}
        
        @app.post("/api/v1/rules/manual")
        async def create_manual_rule(rule: DiversionRule):
            from uuid import uuid4
            from flowspec_controller import FlowSpecRule
            
            rule_id = str(uuid4())
            # 默认使用最低级别策略
            tier = self.config.tiered_policy[0]
            
            # 创建记录对象
            dummy_record = type('', (), {
                'src_ip': rule.src_ip or "0.0.0.0",
                'dst_ip': rule.dst_ip,
                'protocol': rule.protocol,
                'src_port': rule.src_port,
                'dst_port': rule.dst_port
            })()
            
            # 生成规则
            rules = self.rule_manager._create_rules_for_tier(rule_id, dummy_record, tier)
            
            from rule_manager import ActiveRule
            self.rule_manager.active_rules[rule_id] = ActiveRule(rules, "manual", tier, 0.0)
            self.rule_manager.dst_ip_to_rule_id[rule.dst_ip] = rule_id
            
            if self.rule_manager.flowspec_controller:
                for r in rules:
                    await self.rule_manager.flowspec_controller.add_rule(r)
            
            await self.rule_manager.influxdb_writer.write_diversion_event(
                rules[0], "manual", "applied"
            )
            
            return {"rule_id": rule_id, "status": "created", "tier": tier['name']}
        
        @app.post("/api/v1/rules/revoke")
        async def revoke_rule(action: RuleAction):
            if action.rule_id not in self.rule_manager.active_rules:
                raise HTTPException(status_code=404, detail="Rule not found")
            
            await self.rule_manager.revoke_rule(action.rule_id)
            return {"rule_id": action.rule_id, "status": "revoked"}
        
        @app.post("/api/v1/rules/escalate")
        async def escalate_rule(action: DstIpAction):
            success = await self.rule_manager.escalate_rule(action.dst_ip)
            if not success:
                raise HTTPException(status_code=404, detail="Failed to escalate rule (no active rule or already at max tier")
            return {"dst_ip": action.dst_ip, "status": "escalated"}
    
    async def start(self):
        logger.info("Starting API server", extra={"host": self.config.api_host, "port": self.config.api_port})
        
        config = uvicorn.Config(
            app,
            host=self.config.api_host,
            port=self.config.api_port,
            log_level="info"
        )
        server = uvicorn.Server(config)
        await server.serve()
