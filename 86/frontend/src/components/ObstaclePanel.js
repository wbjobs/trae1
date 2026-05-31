import React, { useState, useEffect } from 'react';

function ObstaclePanel({ socket, formation, noFlyZones, dynamicObstacles, plannedPath, formationState, currentObstacleEvents, pathResult }) {
  const [nfzType, setNfzType] = useState('circle');
  const [nfzName, setNfzName] = useState('');
  const [nfzRadius, setNfzRadius] = useState(20);
  const [obstacleCount, setObstacleCount] = useState(2);
  const [selectedDroneForGpsTest, setSelectedDroneForGpsTest] = useState('');
  const [pathGoal, setPathGoal] = useState({ lat: 31.2310, lng: 121.4745, alt: 15 });

  const handleAddNFZ = () => {
    if (!socket || !formation) return;

    const center = formation.center || { lat: 31.2304, lng: 121.4737 };
    
    const zone = {
      type: nfzType,
      name: nfzName || `禁飞区 ${noFlyZones.length + 1}`,
      minAlt: 0,
      maxAlt: 50
    };

    if (nfzType === 'circle') {
      zone.center = {
        lat: center.lat + (Math.random() - 0.5) * 0.001,
        lng: center.lng + (Math.random() - 0.5) * 0.001,
        alt: 0
      };
      zone.radius = nfzRadius;
    } else if (nfzType === 'polygon') {
      const baseLat = center.lat + (Math.random() - 0.5) * 0.0005;
      const baseLng = center.lng + (Math.random() - 0.5) * 0.0005;
      const size = 0.0005;
      zone.coordinates = [
        { lat: baseLat, lng: baseLng },
        { lat: baseLat + size, lng: baseLng },
        { lat: baseLat + size * 0.8, lng: baseLng + size * 1.2 },
        { lat: baseLat - size * 0.2, lng: baseLng + size * 0.8 }
      ];
    }

    socket.emit('nfz:add', zone);
    setNfzName('');
  };

  const handleRemoveNFZ = (zoneId) => {
    if (socket) {
      socket.emit('nfz:remove', zoneId);
    }
  };

  const handleSimulateObstacles = () => {
    if (socket) {
      socket.emit('obstacle:simulate', obstacleCount);
    }
  };

  const handleSimulateGpsLoss = () => {
    if (socket && selectedDroneForGpsTest) {
      socket.emit('drone:simulateGpsLoss', selectedDroneForGpsTest);
    }
  };

  const handleSimulateGpsRecovery = () => {
    if (socket && selectedDroneForGpsTest) {
      socket.emit('drone:simulateGpsRecovery', selectedDroneForGpsTest);
    }
  };

  const handlePlanPath = () => {
    if (socket) {
      socket.emit('formation:planPath', pathGoal);
    }
  };

  const getPositionModeText = (mode) => {
    const modes = {
      'gps': 'GPS定位',
      'optical_flow': '视觉定位',
      'imu': '惯性导航'
    };
    return modes[mode] || mode;
  };

  const getPositionModeColor = (mode) => {
    const colors = {
      'gps': '#52c41a',
      'optical_flow': '#fa8c16',
      'imu': '#722ed1'
    };
    return colors[mode] || '#8c8c8c';
  };

  const getEventTypeText = (type) => {
    const types = {
      'detection': '障碍物检测',
      'path_planning': '路径规划',
      'formation_action': '编队动作',
      'collision_avoidance': '避障动作',
      'nfz_violation': '禁飞区违规',
      'dynamic_obstacle': '动态障碍物',
      'formation_reassembly': '编队重组',
      'gps_degradation': 'GPS降级'
    };
    return types[type] || type;
  };

  const getEventTypeColor = (type) => {
    const colors = {
      'detection': '#fa8c16',
      'path_planning': '#1890ff',
      'formation_action': '#1890ff',
      'collision_avoidance': '#ff4d4f',
      'nfz_violation': '#ff4d4f',
      'dynamic_obstacle': '#fa8c16',
      'formation_reassembly': '#52c41a',
      'gps_degradation': '#fa8c16'
    };
    return colors[type] || '#8c8c8c';
  };

  return (
    <div className="control-panel">
      <h3>AI避障与路径规划</h3>
      
      <div className="panel-section">
        <h4>禁飞区管理</h4>
        <div className="input-row">
          <label>类型:</label>
          <select value={nfzType} onChange={(e) => setNfzType(e.target.value)}>
            <option value="circle">圆形</option>
            <option value="polygon">多边形</option>
          </select>
        </div>
        <div className="input-row">
          <label>名称:</label>
          <input 
            type="text" 
            value={nfzName} 
            onChange={(e) => setNfzName(e.target.value)}
            placeholder="可选"
          />
        </div>
        {nfzType === 'circle' && (
          <div className="input-row">
            <label>半径(米):</label>
            <input 
              type="number" 
              value={nfzRadius} 
              onChange={(e) => setNfzRadius(parseInt(e.target.value))}
              min="5"
              max="200"
            />
          </div>
        )}
        <button className="btn btn-warning" onClick={handleAddNFZ}>
          添加随机禁飞区
        </button>
        
        {noFlyZones.length > 0 && (
          <div className="zone-list">
            <h5>禁飞区列表:</h5>
            {noFlyZones.map(zone => (
              <div key={zone.id} className="zone-item">
                <span style={{ color: '#ff4d4f' }}>●</span>
                <span>{zone.name} ({zone.type === 'circle' ? '圆形' : '多边形'})</span>
                <button className="btn btn-small btn-danger" onClick={() => handleRemoveNFZ(zone.id)}>删除</button>
              </div>
            ))}
          </div>
        )}
      </div>

      <div className="panel-section">
        <h4>动态障碍物</h4>
        <div className="input-row">
          <label>模拟数量:</label>
          <input 
            type="number" 
            value={obstacleCount} 
            onChange={(e) => setObstacleCount(parseInt(e.target.value))}
            min="1"
            max="5"
          />
        </div>
        <button className="btn btn-danger" onClick={handleSimulateObstacles}>
          模拟入侵无人机群
        </button>
        
        {dynamicObstacles.length > 0 && (
          <div className="obstacle-list">
            <h5>动态障碍物:</h5>
            {dynamicObstacles.map(obs => (
              <div key={obs.id} className="obstacle-item">
                <span className="blink">⚠</span>
                <span>{obs.name || obs.id}</span>
                <span style={{ fontSize: '10px', color: '#8c8c8c' }}>
                  速度: {Math.sqrt(obs.velocity.x**2 + obs.velocity.y**2).toFixed(1)}m/s
                </span>
              </div>
            ))}
          </div>
        )}
      </div>

      <div className="panel-section">
        <h4>路径规划预览</h4>
        <div className="input-row">
          <label>目标纬度:</label>
          <input 
            type="number" 
            step="0.0001"
            value={pathGoal.lat} 
            onChange={(e) => setPathGoal({...pathGoal, lat: parseFloat(e.target.value)})}
          />
        </div>
        <div className="input-row">
          <label>目标经度:</label>
          <input 
            type="number" 
            step="0.0001"
            value={pathGoal.lng} 
            onChange={(e) => setPathGoal({...pathGoal, lng: parseFloat(e.target.value)})}
          />
        </div>
        <div className="input-row">
          <label>高度(米):</label>
          <input 
            type="number" 
            value={pathGoal.alt} 
            onChange={(e) => setPathGoal({...pathGoal, alt: parseFloat(e.target.value)})}
            min="5"
            max="100"
          />
        </div>
        <button className="btn btn-success" onClick={handlePlanPath}>
          规划路径 (A*算法)
        </button>
        
        {pathResult && (
          <div className={`path-result ${pathResult.success ? 'success' : 'error'}`}>
            {pathResult.success ? (
              <>
                <div>✓ 路径规划成功</div>
                <div className="path-info">
                  <span>路径点数: {pathResult.path?.length || 0}</span>
                  <span>耗时: {pathResult.planningTime}ms</span>
                  <span>算法: {pathResult.method === 'a_star' ? 'A*算法' : '直线路径'}</span>
                </div>
              </>
            ) : (
              <div>✗ {pathResult.error}</div>
            )}
          </div>
        )}
        
        {plannedPath && plannedPath.length > 1 && (
          <div className="path-preview">
            <div>规划路径点: {plannedPath.length}个</div>
            <div className="path-points">
              {plannedPath.map((point, idx) => (
                <span key={idx} className="path-point">
                  {idx + 1}
                </span>
              ))}
            </div>
          </div>
        )}
      </div>

      <div className="panel-section">
        <h4>GPS降级模拟</h4>
        <div className="input-row">
          <label>选择无人机:</label>
          <select 
            value={selectedDroneForGpsTest} 
            onChange={(e) => setSelectedDroneForGpsTest(e.target.value)}
          >
            <option value="">请选择</option>
            {[1,2,3,4,5].map(i => (
              <option key={i} value={`drone-${i}`}>无人机 {i}</option>
            ))}
          </select>
        </div>
        <div className="button-row">
          <button 
            className="btn btn-warning" 
            onClick={handleSimulateGpsLoss}
            disabled={!selectedDroneForGpsTest}
          >
            模拟GPS丢失
          </button>
          <button 
            className="btn btn-success" 
            onClick={handleSimulateGpsRecovery}
            disabled={!selectedDroneForGpsTest}
          >
            模拟GPS恢复
          </button>
        </div>
      </div>

      <div className="panel-section">
        <h4>避障事件日志</h4>
        {currentObstacleEvents && currentObstacleEvents.length > 0 ? (
          <div className="event-log">
            {currentObstacleEvents.slice(-10).reverse().map((event, idx) => (
              <div key={idx} className="event-item">
                <span 
                  className="event-type" 
                  style={{ backgroundColor: getEventTypeColor(event.type) }}
                >
                  {getEventTypeText(event.type)}
                </span>
                <span className="event-time">
                  {new Date(event.timestamp).toLocaleTimeString()}
                </span>
                <span className="event-details">
                  {event.droneId || ''} 
                  {event.reason || event.obstacle?.name || ''}
                </span>
              </div>
            ))}
          </div>
        ) : (
          <div className="empty-state">暂无避障事件</div>
        )}
      </div>

      <div className="panel-section">
        <h4>系统状态</h4>
        <div className="status-grid">
          <div className="status-item">
            <span>禁飞区</span>
            <span className="status-value">{noFlyZones.length}</span>
          </div>
          <div className="status-item">
            <span>动态障碍物</span>
            <span className="status-value" style={{ color: dynamicObstacles.length > 0 ? '#ff4d4f' : '#52c41a' }}>
              {dynamicObstacles.length}
            </span>
          </div>
          <div className="status-item">
            <span>编队状态</span>
            <span className="status-value" style={{ 
              color: formationState?.isFormationBroken ? '#ff4d4f' : 
                     formationState?.isAvoiding ? '#fa8c16' : '#52c41a' 
            }}>
              {formationState?.isFormationBroken ? '已解散' : 
               formationState?.isAvoiding ? '避障中' : '正常'}
            </span>
          </div>
          <div className="status-item">
            <span>GPS异常</span>
            <span className="status-value" style={{ 
              color: formationState?.gpsDegradedDrones?.length > 0 ? '#fa8c16' : '#52c41a' 
            }}>
              {formationState?.gpsDegradedDrones?.length || 0}
            </span>
          </div>
        </div>
      </div>

      <style>{`
        .zone-list, .obstacle-list, .event-log {
          max-height: 150px;
          overflow-y: auto;
          margin-top: 8px;
          border: 1px solid #333;
          border-radius: 4px;
          padding: 4px;
        }
        .zone-item, .obstacle-item, .event-item {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding: 4px 8px;
          font-size: 11px;
          gap: 8px;
        }
        .zone-item:nth-child(odd), .obstacle-item:nth-child(odd), .event-item:nth-child(odd) {
          background: #1e1e1e;
        }
        .path-result {
          margin-top: 8px;
          padding: 8px;
          border-radius: 4px;
          font-size: 12px;
        }
        .path-result.success {
          background: rgba(82, 196, 26, 0.2);
          border: 1px solid #52c41a;
        }
        .path-result.error {
          background: rgba(255, 77, 79, 0.2);
          border: 1px solid #ff4d4f;
        }
        .path-info {
          display: flex;
          justify-content: space-between;
          margin-top: 4px;
          font-size: 10px;
          color: #8c8c8c;
        }
        .path-preview {
          margin-top: 8px;
          padding: 8px;
          background: rgba(82, 196, 26, 0.1);
          border-radius: 4px;
          font-size: 11px;
        }
        .path-points {
          display: flex;
          flex-wrap: wrap;
          gap: 4px;
          margin-top: 4px;
        }
        .path-point {
          width: 20px;
          height: 20px;
          background: #52c41a;
          border-radius: 50%;
          display: flex;
          align-items: center;
          justify-content: center;
          font-size: 9px;
          color: white;
        }
        .event-type {
          padding: 2px 6px;
          border-radius: 3px;
          color: white;
          font-size: 10px;
          white-space: nowrap;
        }
        .event-time {
          color: #8c8c8c;
          font-size: 10px;
        }
        .event-details {
          flex: 1;
          text-align: right;
          color: #bfbfbf;
        }
        .blink {
          animation: blink 1s infinite;
          color: #ff4d4f;
        }
        @keyframes blink {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.3; }
        }
        .status-grid {
          display: grid;
          grid-template-columns: 1fr 1fr;
          gap: 8px;
        }
        .status-item {
          display: flex;
          justify-content: space-between;
          padding: 6px 10px;
          background: #1e1e1e;
          border-radius: 4px;
          font-size: 11px;
        }
        .status-value {
          font-weight: bold;
          color: #1890ff;
        }
        .empty-state {
          text-align: center;
          color: #595959;
          padding: 20px;
          font-size: 12px;
        }
        .button-row {
          display: flex;
          gap: 8px;
        }
        .button-row .btn {
          flex: 1;
        }
      `}</style>
    </div>
  );
}

export default ObstaclePanel;
