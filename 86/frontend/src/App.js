import React, { useState, useEffect, useCallback } from 'react';
import { Layout, Tabs, message } from 'antd';
import io from 'socket.io-client';
import MapView from './components/MapView';
import FormationControl from './components/FormationControl';
import DroneList from './components/DroneList';
import ReplayPanel from './components/ReplayPanel';
import DroneControl from './components/DroneControl';
import ObstaclePanel from './components/ObstaclePanel';

const { Content, Sider } = Layout;

function App() {
  const [drones, setDrones] = useState([]);
  const [selectedDrone, setSelectedDrone] = useState(null);
  const [formation, setFormation] = useState(null);
  const [socket, setSocket] = useState(null);
  const [isReplayMode, setIsReplayMode] = useState(false);
  const [replayState, setReplayState] = useState(null);
  const [noFlyZones, setNoFlyZones] = useState([]);
  const [dynamicObstacles, setDynamicObstacles] = useState([]);
  const [plannedPath, setPlannedPath] = useState(null);
  const [formationState, setFormationState] = useState(null);
  const [currentObstacleEvents, setCurrentObstacleEvents] = useState([]);
  const [pathResult, setPathResult] = useState(null);

  useEffect(() => {
    const socketInstance = io(process.env.REACT_APP_SOCKET_URL || 'http://localhost:3001');
    
    socketInstance.on('connect', () => {
      console.log('Connected to server');
      message.success('已连接到服务器');
    });

    socketInstance.on('connect_error', () => {
      message.error('连接服务器失败，请检查服务是否启动');
    });

    socketInstance.on('drones:list', (droneList) => {
      setDrones(droneList);
    });

    socketInstance.on('drone:telemetry', (data) => {
      setDrones(prevDrones => prevDrones.map(drone => 
        drone.id === data.droneId 
          ? { ...drone, ...data, position: data.position }
          : drone
      ));
    });

    socketInstance.on('drone:status', (data) => {
      setDrones(prevDrones => prevDrones.map(drone => 
        drone.id === data.droneId 
          ? { ...drone, ...data }
          : drone
      ));
    });

    socketInstance.on('drone:locked', (data) => {
      setDrones(prevDrones => prevDrones.map(drone => 
        drone.id === data.droneId 
          ? { ...drone, locked: data.locked }
          : drone
      ));
    });

    socketInstance.on('formation:current', (formationData) => {
      setFormation(formationData);
      if (formationData.state) {
        setFormationState(formationData.state);
      }
      if (formationData.noFlyZones) {
        setNoFlyZones(formationData.noFlyZones);
      }
      if (formationData.dynamicObstacles) {
        setDynamicObstacles(formationData.dynamicObstacles);
      }
      if (formationData.plannedPath) {
        setPlannedPath(formationData.plannedPath);
      }
    });

    socketInstance.on('formation:degraded', (data) => {
      message.warning(`编队降级: ${data.reason}，速度降至${data.speedFactor}x，间距${data.spacingMultiplier}x`);
      setFormationState(prev => ({ ...prev, ...data, isAvoiding: true, avoidReason: data.reason }));
    });

    socketInstance.on('formation:restored', () => {
      message.success('编队恢复正常');
      setFormationState(prev => ({ ...prev, isAvoiding: false, avoidReason: null, formationSpeedFactor: 1.0 }));
    });

    socketInstance.on('formation:broken', (data) => {
      message.error(`编队解散: ${data.reason}`);
      setFormationState(prev => ({ ...prev, isFormationBroken: true, isAvoiding: true, avoidReason: data.reason }));
    });

    socketInstance.on('formation:reassembled', (data) => {
      message.success(`编队重组完成，${data.dronesRealigned}架无人机对齐，耗时${data.timeTaken}ms`);
      setFormationState(prev => ({ ...prev, isFormationBroken: false, isAvoiding: false, avoidReason: null }));
    });

    socketInstance.on('nfz:list', (zones) => {
      setNoFlyZones(zones);
    });

    socketInstance.on('nfz:added', (zone) => {
      message.info(`已添加禁飞区: ${zone.name}`);
      setNoFlyZones(prev => [...prev, zone]);
    });

    socketInstance.on('nfz:removed', (zoneId) => {
      message.info('已删除禁飞区');
      setNoFlyZones(prev => prev.filter(z => z.id !== zoneId));
    });

    socketInstance.on('obstacle:list', (obstacles) => {
      setDynamicObstacles(obstacles);
    });

    socketInstance.on('obstacle:added', (obstacle) => {
      message.warning(`检测到动态障碍物: ${obstacle.name || obstacle.id}`);
      setDynamicObstacles(prev => [...prev, obstacle]);
    });

    socketInstance.on('path:planned', (data) => {
      message.success(`路径规划完成，耗时${data.planningTime}ms`);
      setPlannedPath(data.path);
    });

    socketInstance.on('path:result', (result) => {
      setPathResult(result);
      if (result.success) {
        message.success(`路径规划成功，${result.path?.length || 0}个路径点，耗时${result.planningTime}ms`);
      } else {
        message.error(`路径规划失败: ${result.error}`);
      }
    });

    socketInstance.on('replay:started', () => {
      setIsReplayMode(true);
      message.info('任务回放开始');
    });

    socketInstance.on('replay:stopped', () => {
      setIsReplayMode(false);
      setReplayState(null);
      message.info('任务回放结束');
    });

    socketInstance.on('replay:progress', (state) => {
      setReplayState(state);
    });

    setSocket(socketInstance);

    const refreshInterval = setInterval(() => {
      if (socketInstance.connected) {
        socketInstance.emit('obstacleEvents:get');
      }
    }, 2000);

    socketInstance.on('obstacleEvents:current', (events) => {
      setCurrentObstacleEvents(events);
    });

    return () => {
      clearInterval(refreshInterval);
      socketInstance.disconnect();
    };
  }, []);

  const handleFormationChange = useCallback((type, spacing) => {
    if (socket && drones.length > 0) {
      const center = drones.length > 0 && drones[0].position ? {
        lat: drones[0].position.lat,
        lng: drones[0].position.lng,
        alt: 10
      } : { lat: 31.2304, lng: 121.4737, alt: 10 };
      
      socket.emit('formation:set', { type, center, spacing });
      message.success(`已切换队形: ${type === 'line' ? '一字形' : type === 'v_shape' ? 'V字形' : type === 'circle' ? '圆形' : '三角形'}`);
    }
  }, [socket, drones]);

  const handleTakeoff = useCallback((altitude) => {
    if (socket) {
      socket.emit('formation:takeoff', altitude);
      message.info('编队起飞中...');
    }
  }, [socket]);

  const handleLand = useCallback(() => {
    if (socket) {
      socket.emit('formation:land');
      message.info('编队降落中...');
      setPlannedPath(null);
      setPathResult(null);
    }
  }, [socket]);

  const handleFormationMove = useCallback((vector) => {
    if (socket) {
      socket.emit('formation:move', vector);
    }
  }, [socket]);

  const handleDroneLock = useCallback((droneId, locked) => {
    if (socket) {
      socket.emit(locked ? 'drone:lock' : 'drone:unlock', droneId);
      message.info(`${locked ? '锁定' : '解锁'}无人机: ${droneId}`);
    }
  }, [socket]);

  const handleDroneManual = useCallback((droneId, direction, speed) => {
    if (socket) {
      socket.emit('drone:manual', { droneId, direction, speed });
    }
  }, [socket]);

  const handleMapClick = useCallback((point) => {
    if (socket) {
      console.log('Map clicked:', point);
    }
  }, [socket]);

  const tabItems = [
    {
      key: 'formation',
      label: '编队控制',
      children: (
        <>
          <FormationControl
            formation={formation}
            onChange={handleFormationChange}
            onTakeoff={handleTakeoff}
            onLand={handleLand}
            onMove={handleFormationMove}
            formationState={formationState}
          />
          <DroneList
            drones={drones}
            selectedDrone={selectedDrone}
            onSelect={setSelectedDrone}
          />
        </>
      )
    },
    {
      key: 'obstacle',
      label: 'AI避障',
      children: (
        <ObstaclePanel
          socket={socket}
          formation={formation}
          noFlyZones={noFlyZones}
          dynamicObstacles={dynamicObstacles}
          plannedPath={plannedPath}
          formationState={formationState}
          currentObstacleEvents={currentObstacleEvents}
          pathResult={pathResult}
        />
      )
    },
    {
      key: 'drone',
      label: '单机控制',
      children: (
        <DroneControl
          drones={drones}
          selectedDrone={selectedDrone}
          onSelect={setSelectedDrone}
          onLock={handleDroneLock}
          onManual={handleDroneManual}
          socket={socket}
        />
      )
    },
    {
      key: 'replay',
      label: '任务回放',
      children: (
        <ReplayPanel socket={socket} />
      )
    }
  ];

  return (
    <div className="app-container">
      <div className="main-content">
        <MapView
          drones={drones}
          formation={formation}
          selectedDrone={selectedDrone}
          onSelectDrone={setSelectedDrone}
          isReplayMode={isReplayMode}
          noFlyZones={noFlyZones}
          dynamicObstacles={dynamicObstacles}
          plannedPath={plannedPath}
          formationState={formationState}
          onMapClick={handleMapClick}
        />
        {isReplayMode && replayState && (
          <div className="replay-controls">
            <span>回放中...</span>
            <span>时间: {Math.floor(replayState.currentTime / 1000)}s</span>
            <span>速度: {replayState.speed}x</span>
          </div>
        )}
        {formationState?.isAvoiding && !isReplayMode && (
          <div className="warning-banner">
            ⚠ 编队正在{formationState.isFormationBroken ? '避障（队形已解散）' : '降级运行'}，
            原因: {formationState.avoidReason === 'gps_degradation' ? 'GPS信号丢失' : 
                   formationState.avoidReason === 'dynamic_obstacle' ? '动态障碍物' : 
                   formationState.avoidReason === 'nfz_violation' ? '禁飞区违规' : formationState.avoidReason}
          </div>
        )}
      </div>
      <div className="sidebar">
        <Tabs 
          defaultActiveKey="formation" 
          items={tabItems}
          style={{ height: '100%', display: 'flex', flexDirection: 'column' }}
          itemsStyle={{ flex: 1, overflow: 'auto' }}
        />
      </div>
      <style>{`
        .warning-banner {
          position: absolute;
          top: 10px;
          left: 50%;
          transform: translateX(-50%);
          background: rgba(250, 140, 22, 0.95);
          color: white;
          padding: 10px 20px;
          border-radius: 20px;
          font-weight: bold;
          z-index: 1000;
          animation: pulse 2s infinite;
          box-shadow: 0 4px 12px rgba(0,0,0,0.3);
        }
        .replay-controls {
          position: absolute;
          bottom: 20px;
          left: 50%;
          transform: translateX(-50%);
          background: rgba(0, 0, 0, 0.85);
          color: #fa8c16;
          padding: 10px 20px;
          border-radius: 20px;
          display: flex;
          gap: 20px;
          font-weight: bold;
          z-index: 1000;
          animation: blink 1.5s infinite;
        }
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.8; }
        }
        @keyframes blink {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.6; }
        }
      `}</style>
    </div>
  );
}

export default App;
