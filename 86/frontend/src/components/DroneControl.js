import React, { useState, useEffect } from 'react';
import { Card, Button, Select, Space, Switch, message, Row, Col } from 'antd';
import { LockOutlined, UnlockOutlined, ArrowUpOutlined, ArrowDownOutlined, ArrowLeftOutlined, ArrowRightOutlined } from '@ant-design/icons';

function DroneControl({ drones, selectedDrone, onSelect, onLock, onManual, socket }) {
  const [controlSpeed, setControlSpeed] = useState(1);
  const [selectedDroneData, setSelectedDroneData] = useState(null);

  useEffect(() => {
    if (selectedDrone) {
      const drone = drones.find(d => d.id === selectedDrone);
      setSelectedDroneData(drone);
    }
  }, [selectedDrone, drones]);

  const handleLockChange = (locked) => {
    if (selectedDrone) {
      onLock(selectedDrone, locked);
      message.success(locked ? '无人机已锁定' : '无人机已解锁');
    }
  };

  const handleManualControl = (direction) => {
    if (!selectedDrone) {
      message.warning('请先选择一架无人机');
      return;
    }
    if (selectedDroneData?.locked) {
      message.warning('该无人机已锁定，请先解锁');
      return;
    }
    onManual(selectedDrone, direction, controlSpeed);
  };

  const droneOptions = drones.map(drone => ({
    label: `${drone.name} (${drone.connected ? '在线' : '离线'})`,
    value: drone.id,
    disabled: !drone.connected
  }));

  return (
    <div style={{ padding: 12, height: '100%', overflow: 'auto' }}>
      <Card title="选择无人机" size="small" style={{ marginBottom: 12 }}>
        <Select
          placeholder="请选择无人机"
          value={selectedDrone}
          onChange={onSelect}
          options={droneOptions}
          style={{ width: '100%' }}
        />
      </Card>

      {selectedDroneData && (
        <>
          <Card title="状态信息" size="small" style={{ marginBottom: 12 }}>
            <Space direction="vertical" style={{ width: '100%' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>连接状态:</span>
                <span style={{ color: selectedDroneData.connected ? '#52c41a' : '#ff4d4f' }}>
                  {selectedDroneData.connected ? '已连接' : '未连接'}
                </span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>飞行状态:</span>
                <span style={{ color: selectedDroneData.armed ? '#52c41a' : '#faad14' }}>
                  {selectedDroneData.armed ? '已起飞' : '地面'}
                </span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span>锁定状态:</span>
                <Switch
                  checked={selectedDroneData.locked}
                  onChange={handleLockChange}
                  checkedChildren={<LockOutlined />}
                  unCheckedChildren={<UnlockOutlined />}
                />
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>当前高度:</span>
                <span>{selectedDroneData.position?.alt?.toFixed(1) || 0} 米</span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>剩余电量:</span>
                <span>{selectedDroneData.battery?.toFixed(1)}%</span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>GPS卫星:</span>
                <span style={{ 
                  color: selectedDroneData.gpsSatellites < 5 ? '#ff4d4f' : 
                         selectedDroneData.gpsSatellites < 8 ? '#faad14' : '#52c41a' 
                }}>
                  {selectedDroneData.gpsSatellites} 颗
                </span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>定位模式:</span>
                <span style={{ 
                  color: selectedDroneData.positionMode === 'gps' ? '#52c41a' :
                         selectedDroneData.positionMode === 'optical_flow' ? '#fa8c16' : '#722ed1'
                }}>
                  {selectedDroneData.positionMode === 'gps' ? 'GPS定位' :
                   selectedDroneData.positionMode === 'optical_flow' ? '视觉定位' : '惯性导航'}
                </span>
              </div>
              {selectedDroneData.gpsLost && (
                <div style={{ 
                  background: 'rgba(255,77,79,0.1)',
                  border: '1px solid #ff4d4f',
                  borderRadius: 4,
                  padding: 6,
                  fontSize: 11,
                  color: '#ff4d4f',
                  textAlign: 'center'
                }}>
                  ⚠ GPS信号丢失，使用{selectedDroneData.positionMode === 'optical_flow' ? '视觉定位' : '惯性导航'}
                </div>
              )}
              {selectedDroneData.opticalFlow && selectedDroneData.positionMode === 'optical_flow' && (
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span>光流质量:</span>
                  <span>{selectedDroneData.opticalFlow.quality}%</span>
                </div>
              )}
            </Space>
          </Card>

          <Card title="GPS模拟测试" size="small" style={{ marginBottom: 12 }}>
            <Space direction="vertical" style={{ width: '100%' }}>
              <div style={{ fontSize: 11, color: '#666', marginBottom: 8 }}>
                模拟GPS信号丢失，测试降级策略
              </div>
              <Row gutter={8}>
                <Col span={12}>
                  <Button 
                    danger 
                    block
                    onClick={() => {
                      if (socket) {
                        socket.emit('drone:simulateGpsLoss', selectedDrone);
                        message.warning('已模拟GPS信号丢失');
                      }
                    }}
                    disabled={selectedDroneData.gpsLost}
                  >
                    模拟GPS丢失
                  </Button>
                </Col>
                <Col span={12}>
                  <Button 
                    type="primary"
                    block
                    onClick={() => {
                      if (socket) {
                        socket.emit('drone:simulateGpsRecovery', selectedDrone);
                        message.success('已模拟GPS信号恢复');
                      }
                    }}
                    disabled={!selectedDroneData.gpsLost}
                  >
                    模拟GPS恢复
                  </Button>
                </Col>
              </Row>
            </Space>
          </Card>

          <Card title="手动控制" size="small" style={{ marginBottom: 12 }}>
            <Space direction="vertical" style={{ width: '100%' }}>
              <div>
                <span style={{ marginRight: 8 }}>控制速度:</span>
                <Select
                  value={controlSpeed}
                  onChange={setControlSpeed}
                  style={{ width: 120 }}
                  options={[
                    { label: '慢速', value: 0.5 },
                    { label: '正常', value: 1 },
                    { label: '快速', value: 2 }
                  ]}
                />
              </div>

              <div style={{ 
                display: 'grid', 
                gridTemplateColumns: 'repeat(3, 1fr)', 
                gap: 8,
                maxWidth: 200,
                margin: '0 auto'
              }}>
                <div></div>
                <Button 
                  icon={<ArrowUpOutlined />} 
                  onClick={() => handleManualControl('forward')}
                  disabled={selectedDroneData.locked}
                  size="large"
                >
                  前
                </Button>
                <div></div>
                <Button 
                  icon={<ArrowLeftOutlined />} 
                  onClick={() => handleManualControl('left')}
                  disabled={selectedDroneData.locked}
                  size="large"
                >
                  左
                </Button>
                <div style={{ 
                  display: 'flex', 
                  flexDirection: 'column', 
                  gap: 4 
                }}>
                  <Button 
                    size="small" 
                    onClick={() => handleManualControl('up')}
                    disabled={selectedDroneData.locked}
                    style={{ fontSize: 10 }}
                  >
                    上升
                  </Button>
                  <Button 
                    size="small" 
                    onClick={() => handleManualControl('down')}
                    disabled={selectedDroneData.locked}
                    style={{ fontSize: 10 }}
                  >
                    下降
                  </Button>
                </div>
                <Button 
                  icon={<ArrowRightOutlined />} 
                  onClick={() => handleManualControl('right')}
                  disabled={selectedDroneData.locked}
                  size="large"
                >
                  右
                </Button>
                <div></div>
                <Button 
                  icon={<ArrowDownOutlined />} 
                  onClick={() => handleManualControl('backward')}
                  disabled={selectedDroneData.locked}
                  size="large"
                >
                  后
                </Button>
                <div></div>
              </div>

              <div style={{ fontSize: 12, color: '#666', textAlign: 'center', marginTop: 8 }}>
                {selectedDroneData.locked ? 
                  <span style={{ color: '#faad14' }}>无人机已锁定，无法控制</span> : 
                  '按住按钮持续移动，点击单次移动'
                }
              </div>
            </Space>
          </Card>
        </>
      )}

      {!selectedDrone && (
        <div style={{ textAlign: 'center', padding: 40, color: '#999' }}>
          请从上方选择一架无人机进行控制
        </div>
      )}
    </div>
  );
}

export default DroneControl;