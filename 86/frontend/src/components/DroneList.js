import React from 'react';
import { Card, Progress, Tag, Space } from 'antd';
import { BatteryOutlined, RocketOutlined, LockOutlined, UnlockOutlined, EnvironmentOutlined, AlertOutlined } from '@ant-design/icons';

function DroneList({ drones, selectedDrone, onSelect }) {
  const getBatteryColor = (battery) => {
    if (battery > 70) return '#52c41a';
    if (battery > 30) return '#faad14';
    return '#ff4d4f';
  };

  const getPositionModeText = (mode) => {
    const modes = {
      'gps': 'GPS',
      'optical_flow': '视觉',
      'imu': '惯性'
    };
    return modes[mode] || mode;
  };

  const getPositionModeColor = (mode) => {
    const colors = {
      'gps': 'green',
      'optical_flow': 'orange',
      'imu': 'purple'
    };
    return colors[mode] || 'default';
  };

  return (
    <div className="drone-list">
      <h4 style={{ margin: '0 0 12px 0' }}>无人机列表</h4>
      {drones.map(drone => (
        <Card
          key={drone.id}
          size="small"
          className={`drone-card ${selectedDrone === drone.id ? 'selected' : ''}`}
          onClick={() => onSelect(drone.id)}
          style={{ cursor: 'pointer' }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
            <Space>
              <span className={`status-dot ${drone.connected ? 'online' : 'offline'} ${drone.gpsLost ? 'gps-lost' : ''}`}></span>
              <strong>{drone.name}</strong>
            </Space>
            <Space size={4}>
              {drone.gpsLost && <Tag color="red" icon={<AlertOutlined />}>GPS丢失</Tag>}
              {drone.positionMode && !drone.gpsLost && (
                <Tag color={getPositionModeColor(drone.positionMode)} icon={<EnvironmentOutlined />}>
                  {getPositionModeText(drone.positionMode)}
                </Tag>
              )}
              {drone.locked && <Tag color="orange" icon={<LockOutlined />}>锁定</Tag>}
              {drone.armed && <Tag color="green" icon={<RocketOutlined />}>飞行</Tag>}
            </Space>
          </div>
          
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8, fontSize: 12 }}>
            <div>
              <span style={{ color: '#666' }}>高度:</span>
              <span style={{ float: 'right' }}>{drone.position?.alt?.toFixed(1) || 0} m</span>
            </div>
            <div>
              <span style={{ color: '#666' }}>模式:</span>
              <span style={{ float: 'right' }}>{drone.mode}</span>
            </div>
            <div>
              <span style={{ color: '#666' }}>GPS:</span>
              <span style={{ 
                float: 'right', 
                color: drone.gpsSatellites < 5 ? '#ff4d4f' : 
                       drone.gpsSatellites < 8 ? '#faad14' : '#52c41a' 
              }}>
                {drone.gpsSatellites} 星
              </span>
            </div>
            <div>
              <span style={{ color: '#666' }}>航向:</span>
              <span style={{ float: 'right' }}>{drone.heading?.toFixed(0) || 0}°</span>
            </div>
            {drone.positionMode !== 'gps' && (
              <>
                <div style={{ gridColumn: 'span 2' }}>
                  <span style={{ color: '#666' }}>定位:</span>
                  <span style={{ 
                    float: 'right', 
                    color: getPositionModeColor(drone.positionMode) 
                  }}>
                    {getPositionModeText(drone.positionMode)}
                  </span>
                </div>
                {drone.opticalFlow && (
                  <div style={{ gridColumn: 'span 2' }}>
                    <span style={{ color: '#666' }}>光流质量:</span>
                    <span style={{ float: 'right' }}>{drone.opticalFlow.quality}%</span>
                  </div>
                )}
              </>
            )}
          </div>

          <div style={{ marginTop: 8 }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, marginBottom: 4 }}>
              <span><BatteryOutlined /> 电量</span>
              <span style={{ color: getBatteryColor(drone.battery) }}>{drone.battery?.toFixed(1)}%</span>
            </div>
            <Progress
              percent={drone.battery || 0}
              size="small"
              showInfo={false}
              strokeColor={getBatteryColor(drone.battery)}
            />
          </div>
        </Card>
      ))}
    </div>
  );
}

export default DroneList;