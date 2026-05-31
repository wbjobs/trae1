import React, { useState } from 'react';
import { Card, Button, Select, Slider, Space, Row, Col, InputNumber, message } from 'antd';
import { RocketOutlined, VerticalAlignBottomOutlined, ArrowUpOutlined, ArrowDownOutlined, ArrowLeftOutlined, ArrowRightOutlined } from '@ant-design/icons';

function FormationControl({ formation, onChange, onTakeoff, onLand, onMove, formationState }) {
  const [formationType, setFormationType] = useState('line');
  const [spacing, setSpacing] = useState(10);
  const [altitude, setAltitude] = useState(10);

  const formationOptions = [
    { label: '一字形', value: 'line' },
    { label: 'V字形', value: 'v_shape' },
    { label: '圆形', value: 'circle' },
    { label: '三角形', value: 'triangle' }
  ];

  const handleFormationChange = (type) => {
    setFormationType(type);
    onChange(type, spacing);
    message.success(`已切换为${formationOptions.find(o => o.value === type).label}`);
  };

  const handleSpacingChange = (value) => {
    setSpacing(value);
    onChange(formationType, value);
  };

  const handleTakeoff = () => {
    onTakeoff(altitude);
    message.success(`编队起飞，目标高度 ${altitude} 米`);
  };

  const handleLand = () => {
    onLand();
    message.success('编队降落');
  };

  const handleMove = (direction) => {
    const moveDistance = 5;
    const vectors = {
      forward: { dx: 0, dy: moveDistance, dz: 0 },
      backward: { dx: 0, dy: -moveDistance, dz: 0 },
      left: { dx: -moveDistance, dy: 0, dz: 0 },
      right: { dx: moveDistance, dy: 0, dz: 0 },
      up: { dx: 0, dy: 0, dz: 2 },
      down: { dx: 0, dy: 0, dz: -2 }
    };
    onMove(vectors[direction]);
  };

  const getStatusColor = () => {
    if (!formationState) return '#52c41a';
    if (formationState.isFormationBroken) return '#ff4d4f';
    if (formationState.isAvoiding) return '#fa8c16';
    return '#52c41a';
  };

  const getStatusText = () => {
    if (!formationState) return '正常';
    if (formationState.isFormationBroken) return '编队解散（避障中）';
    if (formationState.isAvoiding && formationState.avoidReason === 'gps_degradation') return 'GPS降级模式';
    if (formationState.isAvoiding) return '避障中';
    return '正常';
  };

  return (
    <Card 
      title="编队控制" 
      size="small" 
      style={{ margin: 12 }}
      extra={
        <span style={{ 
          color: getStatusColor(), 
          fontWeight: 'bold',
          fontSize: '12px'
        }}>
          ● {getStatusText()}
        </span>
      }
    >
      <Space direction="vertical" style={{ width: '100%' }} size="middle">
        {formationState && (formationState.isAvoiding || formationState.gpsDegradedDrones?.length > 0) && (
          <div style={{ 
            background: formationState.isFormationBroken ? 'rgba(255,77,79,0.1)' : 'rgba(250,140,22,0.1)',
            border: `1px solid ${formationState.isFormationBroken ? '#ff4d4f' : '#fa8c16'}`,
            borderRadius: 4,
            padding: 8,
            fontSize: 11
          }}>
            <div style={{ color: formationState.isFormationBroken ? '#ff4d4f' : '#fa8c16', fontWeight: 'bold', marginBottom: 4 }}>
              ⚠ {formationState.avoidReason === 'gps_degradation' ? 'GPS信号丢失' : 
                 formationState.avoidReason === 'dynamic_obstacle' ? '检测到动态障碍物' :
                 formationState.avoidReason === 'nfz_violation' ? '禁飞区违规' : '编队异常'}
            </div>
            {formationState.formationSpeedFactor !== undefined && formationState.formationSpeedFactor !== 1 && (
              <div>速度降至: {formationState.formationSpeedFactor.toFixed(1)}x</div>
            )}
            {formationState.gpsDegradedDrones?.length > 0 && (
              <div>受影响无人机: {formationState.gpsDegradedDrones.join(', ')}</div>
            )}
          </div>
        )}
        <div>
          <label style={{ display: 'block', marginBottom: 8 }}>队形选择</label>
          <Select
            value={formationType}
            onChange={handleFormationChange}
            options={formationOptions}
            style={{ width: '100%' }}
          />
        </div>

        <div>
          <label style={{ display: 'block', marginBottom: 8 }}>
            队形间距: {spacing} 米
          </label>
          <Slider
            min={5}
            max={50}
            value={spacing}
            onChange={handleSpacingChange}
          />
        </div>

        <Row gutter={8}>
          <Col span={12}>
            <label style={{ display: 'block', marginBottom: 8 }}>起飞高度</label>
            <InputNumber
              min={5}
              max={100}
              value={altitude}
              onChange={setAltitude}
              style={{ width: '100%' }}
              addonAfter="米"
            />
          </Col>
          <Col span={12}>
            <label style={{ display: 'block', marginBottom: 8 }}>&nbsp;</label>
            <Button
              type="primary"
              icon={<RocketOutlined />}
              onClick={handleTakeoff}
              block
              style={{ background: '#52c41a', borderColor: '#52c41a' }}
            >
              一键起飞
            </Button>
          </Col>
        </Row>

        <Button
          danger
          icon={<VerticalAlignBottomOutlined />}
          onClick={handleLand}
          block
        >
          一键降落
        </Button>

        <div>
          <label style={{ display: 'block', marginBottom: 8 }}>编队移动</label>
          <div style={{ 
            display: 'grid', 
            gridTemplateColumns: 'repeat(3, 1fr)', 
            gap: 8,
            maxWidth: 200,
            margin: '0 auto'
          }}>
            <div></div>
            <Button icon={<ArrowUpOutlined />} onClick={() => handleMove('forward')}>前</Button>
            <div></div>
            <Button icon={<ArrowLeftOutlined />} onClick={() => handleMove('left')}>左</Button>
            <div style={{ 
              display: 'flex', 
              flexDirection: 'column', 
              gap: 4 
            }}>
              <Button size="small" onClick={() => handleMove('up')} style={{ fontSize: 10 }}>上升</Button>
              <Button size="small" onClick={() => handleMove('down')} style={{ fontSize: 10 }}>下降</Button>
            </div>
            <Button icon={<ArrowRightOutlined />} onClick={() => handleMove('right')}>右</Button>
            <div></div>
            <Button onClick={() => handleMove('backward')}>后</Button>
            <div></div>
          </div>
        </div>
      </Space>
    </Card>
  );
}

export default FormationControl;