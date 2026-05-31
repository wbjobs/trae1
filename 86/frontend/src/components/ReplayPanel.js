import React, { useState, useEffect } from 'react';
import { Card, Button, List, Slider, Space, Typography, message } from 'antd';
import { PlayCircleOutlined, PauseCircleOutlined, StopOutlined, ReloadOutlined } from '@ant-design/icons';
import dayjs from 'dayjs';
import axios from 'axios';

const { Title, Text } = Typography;

function ReplayPanel({ socket }) {
  const [logs, setLogs] = useState([]);
  const [selectedLog, setSelectedLog] = useState(null);
  const [logData, setLogData] = useState(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isPaused, setIsPaused] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [playbackSpeed, setPlaybackSpeed] = useState(1);

  useEffect(() => {
    loadLogs();

    if (socket) {
      socket.on('replay:started', () => {
        setIsPlaying(true);
        setIsPaused(false);
      });

      socket.on('replay:paused', () => {
        setIsPaused(true);
      });

      socket.on('replay:resumed', () => {
        setIsPaused(false);
      });

      socket.on('replay:stopped', () => {
        setIsPlaying(false);
        setIsPaused(false);
        setCurrentTime(0);
      });

      socket.on('replay:progress', (state) => {
        setCurrentTime(state.currentTime);
      });
    }

    return () => {
      if (socket) {
        socket.off('replay:started');
        socket.off('replay:paused');
        socket.off('replay:resumed');
        socket.off('replay:stopped');
        socket.off('replay:progress');
      }
    };
  }, [socket]);

  const loadLogs = async () => {
    try {
      const response = await axios.get('/api/logs');
      setLogs(response.data);
    } catch (error) {
      console.error('Failed to load logs:', error);
    }
  };

  const handleSelectLog = async (log) => {
    setSelectedLog(log);
    try {
      const response = await axios.get(`/api/logs/${log.id}`);
      setLogData(response.data);
    } catch (error) {
      console.error('Failed to load log data:', error);
      message.error('加载日志数据失败');
    }
  };

  const handleStartReplay = () => {
    if (!selectedLog) {
      message.warning('请先选择一个飞行日志');
      return;
    }
    socket.emit('replay:start', selectedLog.id);
    message.success('开始回放飞行任务');
  };

  const handlePauseReplay = () => {
    socket.emit('replay:pause');
  };

  const handleResumeReplay = () => {
    socket.emit('replay:resume');
  };

  const handleStopReplay = () => {
    socket.emit('replay:stop');
    message.info('回放已停止');
  };

  const handleSeek = (value) => {
    if (logData && logData.telemetry && logData.telemetry.length > 0) {
      const startTime = logData.telemetry[0].timestamp;
      const targetTime = startTime + value;
      socket.emit('replay:seek', targetTime);
    }
  };

  const formatDuration = (seconds) => {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return `${mins}:${secs.toString().padStart(2, '0')}`;
  };

  const totalDuration = logData && logData.telemetry && logData.telemetry.length > 0
    ? logData.telemetry[logData.telemetry.length - 1].timestamp - logData.telemetry[0].timestamp
    : 0;

  return (
    <div style={{ padding: 12, height: '100%', display: 'flex', flexDirection: 'column' }}>
      {isPlaying && (
        <Card size="small" style={{ marginBottom: 12 }}>
          <Space direction="vertical" style={{ width: '100%' }}>
            <Space style={{ width: '100%', justifyContent: 'center' }}>
              {isPaused ? (
                <Button 
                  type="primary" 
                  icon={<PlayCircleOutlined />} 
                  onClick={handleResumeReplay}
                  size="large"
                >
                  继续
                </Button>
              ) : (
                <Button 
                  icon={<PauseCircleOutlined />} 
                  onClick={handlePauseReplay}
                  size="large"
                >
                  暂停
                </Button>
              )}
              <Button 
                danger 
                icon={<StopOutlined />} 
                onClick={handleStopReplay}
                size="large"
              >
                停止
              </Button>
            </Space>
            
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
                <Text type="secondary">{formatDuration(Math.floor(currentTime / 1000))}</Text>
                <Text type="secondary">{formatDuration(Math.floor(totalDuration / 1000))}</Text>
              </div>
              <Slider
                min={0}
                max={totalDuration}
                value={currentTime}
                onChange={handleSeek}
                tooltip={{ formatter: (val) => formatDuration(Math.floor(val / 1000)) }}
              />
            </div>

            <div>
              <span style={{ marginRight: 8 }}>回放速度:</span>
              <Space>
                {[0.5, 1, 2, 4].map(speed => (
                  <Button 
                    key={speed}
                    size="small"
                    type={playbackSpeed === speed ? 'primary' : 'default'}
                    onClick={() => setPlaybackSpeed(speed)}
                  >
                    {speed}x
                  </Button>
                ))}
              </Space>
            </div>
          </Space>
        </Card>
      )}

      <Card 
        title="飞行日志列表" 
        size="small"
        style={{ flex: 1, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}
        extra={
          <Button 
            size="small" 
            icon={<ReloadOutlined />} 
            onClick={loadLogs}
          >
            刷新
          </Button>
        }
      >
        {!isPlaying && selectedLog && (
          <div style={{ marginBottom: 12, padding: 12, background: '#f5f5f5', borderRadius: 4 }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <div>
                <Text strong>已选择日志</Text>
                <div style={{ fontSize: 12, color: '#666' }}>
                  开始时间: {dayjs(selectedLog.startTime).format('YYYY-MM-DD HH:mm:ss')}
                </div>
                <div style={{ fontSize: 12, color: '#666' }}>
                  飞行时长: {formatDuration(selectedLog.duration)}
                </div>
              </div>
              <Button 
                type="primary" 
                icon={<PlayCircleOutlined />}
                onClick={handleStartReplay}
              >
                开始回放
              </Button>
            </div>
          </div>
        )}

        <div className="log-list" style={{ flex: 1, overflow: 'auto' }}>
          {logs.length === 0 ? (
            <div style={{ textAlign: 'center', padding: 40, color: '#999' }}>
              暂无飞行日志
              <div style={{ fontSize: 12, marginTop: 8 }}>
                完成一次飞行任务后将自动生成日志
              </div>
            </div>
          ) : (
            logs.map(log => (
              <div
                key={log.id}
                className={`log-item ${selectedLog?.id === log.id ? 'selected' : ''}`}
                onClick={() => handleSelectLog(log)}
              >
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <Text strong>
                    {dayjs(log.startTime).format('MM-DD HH:mm')}
                  </Text>
                  <Text type="secondary">
                    {formatDuration(log.duration)}
                  </Text>
                </div>
                <div style={{ fontSize: 12, color: '#666', marginTop: 4 }}>
                  开始: {dayjs(log.startTime).format('YYYY-MM-DD HH:mm:ss')}
                </div>
              </div>
            ))
          )}
        </div>
      </Card>
    </div>
  );
}

export default ReplayPanel;