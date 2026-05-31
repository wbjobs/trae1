#!/usr/bin/env python3
"""
智能家居 WebSocket 模拟服务器
用于测试语音命令识别应用的 WebSocket 通信功能

使用方法:
    pip install websockets
    python smart_home_server.py
"""

import asyncio
import json
import websockets
from datetime import datetime

class SmartHomeServer:
    def __init__(self, host="localhost", port=8765):
        self.host = host
        self.port = port
        self.connected_clients = set()
        self.device_states = {
            "power": False,
            "volume": 50,
            "playing": False,
            "muted": False,
            "current_track": 1
        }

    async def handle_command(self, data):
        """处理语音命令并返回执行结果"""
        action = data.get("action", "")
        label = data.get("label", "")
        confidence = data.get("confidence", 0)
        
        print(f"[{datetime.now().strftime('%H:%M:%S')}] "
              f"收到命令: {label} (动作: {action}, 置信度: {confidence:.2%})")

        success = True
        message = f"已执行: {label}"

        # 模拟设备状态变更
        if action == "power_on":
            self.device_states["power"] = True
            message = "设备已开机"
        elif action == "power_off":
            self.device_states["power"] = False
            message = "设备已关机"
        elif action == "volume_up":
            self.device_states["volume"] = min(100, self.device_states["volume"] + 10)
            message = f"音量已调高至 {self.device_states['volume']}%"
        elif action == "volume_down":
            self.device_states["volume"] = max(0, self.device_states["volume"] - 10)
            message = f"音量已调低至 {self.device_states['volume']}%"
        elif action == "next_track":
            self.device_states["current_track"] += 1
            message = f"已切换到第 {self.device_states['current_track']} 首"
        elif action == "previous_track":
            self.device_states["current_track"] = max(1, self.device_states["current_track"] - 1)
            message = f"已切回到第 {self.device_states['current_track']} 首"
        elif action == "pause":
            self.device_states["playing"] = False
            message = "播放已暂停"
        elif action == "play":
            self.device_states["playing"] = True
            self.device_states["muted"] = False
            message = "开始播放"
        elif action == "mute":
            self.device_states["muted"] = True
            message = "已静音"
        elif action == "unmute":
            self.device_states["muted"] = False
            message = "已取消静音"
        elif action == "unknown":
            success = False
            message = "未知命令"

        response = {
            "success": success,
            "action": action,
            "message": message,
            "timestamp": data.get("timestamp", 0),
            "device_states": self.device_states
        }
        
        print(f"  → 响应: {message} (成功: {success})")
        return response

    async def client_handler(self, websocket):
        """处理单个客户端连接"""
        print(f"\n[连接] 新客户端已连接: {websocket.remote_address}")
        self.connected_clients.add(websocket)
        
        try:
            async for message in websocket:
                try:
                    data = json.loads(message)
                    response = await self.handle_command(data)
                    await websocket.send(json.dumps(response))
                except json.JSONDecodeError:
                    print(f"[错误] 无效的 JSON 消息: {message}")
                except Exception as e:
                    print(f"[错误] 处理消息失败: {e}")
        except websockets.exceptions.ConnectionClosed:
            print(f"[断开] 客户端已断开: {websocket.remote_address}")
        finally:
            self.connected_clients.discard(websocket)

    async def start(self):
        """启动服务器"""
        print(f"╔{'═'*60}╗")
        print(f"║{'智能家居 WebSocket 模拟服务器':^60}║")
        print(f"╚{'═'*60}╝")
        print(f"\n服务器地址: ws://{self.host}:{self.port}")
        print(f"等待客户端连接...\n")
        
        async with websockets.serve(self.client_handler, self.host, self.port):
            await asyncio.Future()


if __name__ == "__main__":
    server = SmartHomeServer()
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        print("\n\n服务器已停止")
