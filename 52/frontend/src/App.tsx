import { useState, useEffect, useCallback, useMemo } from 'react'
import { useWebTransport, type SensorData } from './hooks/useWebTransport'
import { useAnomalyDetection } from './hooks/useAnomalyDetection'
import { WaveformChart } from './components/WaveformChart'
import { SensorSelector } from './components/SensorSelector'
import type { AnomalyRecord } from './services/indexedDB'

const SERVER_URL = 'https://localhost:4433/webtransport'
const TOTAL_SENSORS = 100
const MAX_SELECTED = 4
const CHART_COLORS = ['#ff6b6b', '#4ecdc4', '#45b7d1', '#96ceb4']

export default function App() {
  const {
    status,
    error,
    dataMap,
    orderedDataMap,
    latency,
    isJitterDetected,
    jitterLevel,
    connect,
    disconnect,
    subscribeSensor,
    unsubscribeSensor,
    replaySensor,
    replayAll
  } = useWebTransport(SERVER_URL)

  const [selectedSensors, setSelectedSensors] = useState<number[]>([])
  const [replayMode, setReplayMode] = useState(false)
  const [replayData, setReplayData] = useState<Map<number, SensorData[]>>(new Map())
  const [replayIndex, setReplayIndex] = useState(0)
  const [isPlaying, setIsPlaying] = useState(false)
  const [showReplayPanel, setShowReplayPanel] = useState(false)
  const [replaySensorId, setReplaySensorId] = useState<number | null>(null)
  const [showAnomalyPanel, setShowAnomalyPanel] = useState(false)
  const [anomalyHistory, setAnomalyHistory] = useState<AnomalyRecord[]>([])
  const [selectedAnomaly, setSelectedAnomaly] = useState<AnomalyRecord | null>(null)

  const {
    anomalySegments,
    isAnalyzing,
    loadAnomalyReports
  } = useAnomalyDetection(orderedDataMap, status === 'connected' && !replayMode)

  const allSensors = useMemo(() =>
    Array.from({ length: TOTAL_SENSORS }, (_, i) => i),
    []
  )

  const handleToggleSensor = useCallback(async (sensorId: number) => {
    if (replayMode) return

    const isSelected = selectedSensors.includes(sensorId)

    if (isSelected) {
      await unsubscribeSensor(sensorId)
      setSelectedSensors(prev => prev.filter(id => id !== sensorId))
    } else {
      if (selectedSensors.length >= MAX_SELECTED) return
      const success = await subscribeSensor(sensorId)
      if (success) {
        setSelectedSensors(prev => [...prev, sensorId])
      }
    }
  }, [selectedSensors, replayMode, subscribeSensor, unsubscribeSensor])

  const handleStartReplay = useCallback(async (sensorId: number) => {
    try {
      const data = await replaySensor(sensorId)
      if (data.length > 0) {
        setReplayData(new Map([[sensorId, data]]))
        setReplayIndex(0)
        setIsPlaying(true)
        setReplayMode(true)
        setReplaySensorId(sensorId)
      }
    } catch (err) {
      console.error('Replay failed:', err)
    }
  }, [replaySensor])

  const handleStartAllReplay = useCallback(async () => {
    try {
      const data = await replayAll()
      if (data.length > 0) {
        const grouped = new Map<number, SensorData[]>()
        data.forEach(d => {
          const arr = grouped.get(d.sensor_id) || []
          arr.push(d)
          grouped.set(d.sensor_id, arr)
        })
        setReplayData(grouped)
        setReplayIndex(0)
        setIsPlaying(true)
        setReplayMode(true)
        setReplaySensorId(null)
        const firstSensor = Array.from(grouped.keys())[0]
        if (firstSensor !== undefined) {
          setSelectedSensors([firstSensor])
        }
      }
    } catch (err) {
      console.error('Replay all failed:', err)
    }
  }, [replayAll])

  useEffect(() => {
    if (!replayMode || !isPlaying) return

    const interval = setInterval(() => {
      setReplayIndex(prev => {
        const maxIndex = Math.max(...Array.from(replayData.values()).map(arr => arr.length)) - 1
        if (prev >= maxIndex) {
          setIsPlaying(false)
          return prev
        }
        return prev + 1
      })
    }, 10)

    return () => clearInterval(interval)
  }, [replayMode, isPlaying, replayData])

  const handleStopReplay = useCallback(() => {
    setReplayMode(false)
    setIsPlaying(false)
    setReplayIndex(0)
    setReplayData(new Map())
    setReplaySensorId(null)
  }, [])

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.code === 'Space' && replayMode) {
        e.preventDefault()
        setIsPlaying(prev => !prev)
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [replayMode])

  useEffect(() => {
    if (showAnomalyPanel && status === 'connected') {
      loadAnomalyReports().then(reports => {
        setAnomalyHistory(reports)
      })
    }
  }, [showAnomalyPanel, status, loadAnomalyReports])

  const getDisplayData = (sensorId: number): SensorData[] => {
    if (replayMode) {
      const data = replayData.get(sensorId) || []
      return data.slice(0, replayIndex + 1)
    }
    return orderedDataMap.get(sensorId) || []
  }

  const getAnomalySegmentsForSensor = (sensorId: number) => {
    return anomalySegments.get(sensorId) || []
  }

  const getSeverityColor = (severity: string) => {
    switch (severity) {
      case 'severe': return '#ff6b6b'
      case 'moderate': return '#ffa500'
      default: return '#4ecdc4'
    }
  }

  const getSeverityText = (severity: string) => {
    switch (severity) {
      case 'severe': return '严重'
      case 'moderate': return '中度'
      default: return '轻微'
    }
  }

  const formatAnomalyType = (type: string) => {
    return type.replace(/_/g, ' ').toLowerCase().replace(/\b\w/g, c => c.toUpperCase())
  }

  const getJitterColor = () => {
    switch (jitterLevel) {
      case 'severe': return '#ff6b6b'
      case 'mild': return '#ffa500'
      default: return '#4ecdc4'
    }
  }

  const getJitterText = () => {
    switch (jitterLevel) {
      case 'severe': return '严重抖动'
      case 'mild': return '轻微抖动'
      default: return '网络正常'
    }
  }

  const getStatusColor = () => {
    switch (status) {
      case 'connected': return '#4ecdc4'
      case 'connecting': return '#ffa500'
      case 'error': return '#ff6b6b'
      default: return '#888'
    }
  }

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      minHeight: '100vh',
      background: '#0a0e27'
    }}>
      <header style={{
        padding: '16px 24px',
        background: '#121629',
        borderBottom: '1px solid #1a1f3a',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
          <h1 style={{ fontSize: '20px', color: '#e0e0e0', margin: 0 }}>
            工业仪表数据实时监控系统
          </h1>
          <div style={{
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
            padding: '4px 12px',
            background: '#1a1f3a',
            borderRadius: '12px'
          }}>
            <div style={{
              width: '8px',
              height: '8px',
              borderRadius: '50%',
              background: getStatusColor(),
              animation: status === 'connecting' ? 'pulse 1s infinite' : 'none'
            }} />
            <span style={{ fontSize: '12px', color: '#888' }}>
              {status === 'connected' ? '已连接' : status === 'connecting' ? '连接中...' : status === 'error' ? '连接错误' : '未连接'}
            </span>
          </div>
          {status === 'connected' && (
            <div style={{
              padding: '4px 12px',
              background: '#1a3a4a',
              borderRadius: '12px',
              fontSize: '12px',
              color: '#4ecdc4'
            }}>
              延迟: {latency}ms
            </div>
          )}
          {status === 'connected' && isJitterDetected && (
            <div style={{
              display: 'flex',
              alignItems: 'center',
              gap: '6px',
              padding: '4px 12px',
              background: jitterLevel === 'severe' ? '#3a1a1a' : '#3a2a1a',
              borderRadius: '12px',
              fontSize: '12px',
              color: getJitterColor(),
              animation: 'pulse 0.5s infinite'
            }}>
              <span style={{ fontSize: '14px' }}>⚠</span>
              <span>网络抖动: {getJitterText()}</span>
            </div>
          )}
          {status === 'connected' && isAnalyzing && (
            <div style={{
              display: 'flex',
              alignItems: 'center',
              gap: '6px',
              padding: '4px 12px',
              background: '#1a3a4a',
              borderRadius: '12px',
              fontSize: '12px',
              color: '#4ecdc4'
            }}>
              <span>异常分析中...</span>
            </div>
          )}
        </div>

        <div style={{ display: 'flex', gap: '8px' }}>
          {status !== 'connected' ? (
            <button
              onClick={connect}
              disabled={status === 'connecting'}
              style={{
                padding: '8px 16px',
                background: status === 'connecting' ? '#333' : '#4ecdc4',
                color: '#fff',
                border: 'none',
                borderRadius: '6px',
                cursor: status === 'connecting' ? 'not-allowed' : 'pointer',
                fontSize: '14px'
              }}
            >
              {status === 'connecting' ? '连接中...' : '连接'}
            </button>
          ) : (
            <button
              onClick={disconnect}
              style={{
                padding: '8px 16px',
                background: '#ff6b6b',
                color: '#fff',
                border: 'none',
                borderRadius: '6px',
                cursor: 'pointer',
                fontSize: '14px'
              }}
            >
              断开
            </button>
          )}

          {status === 'connected' && (
            <button
              onClick={() => setShowReplayPanel(prev => !prev)}
              style={{
                padding: '8px 16px',
                background: '#1a1f3a',
                color: showReplayPanel ? '#4ecdc4' : '#888',
                border: '1px solid #2a2f4a',
                borderRadius: '6px',
                cursor: 'pointer',
                fontSize: '14px'
              }}
            >
              数据回放
            </button>
          )}

          {status === 'connected' && (
            <button
              onClick={() => setShowAnomalyPanel(prev => !prev)}
              style={{
                padding: '8px 16px',
                background: anomalyHistory.length > 0 ? '#3a1a1a' : '#1a1f3a',
                color: anomalyHistory.length > 0 ? '#ff6b6b' : '#888',
                border: `1px solid ${anomalyHistory.length > 0 ? '#ff6b6b33' : '#2a2f4a'}`,
                borderRadius: '6px',
                cursor: 'pointer',
                fontSize: '14px'
              }}
            >
              异常报告 {anomalyHistory.length > 0 && `(${anomalyHistory.length})`}
            </button>
          )}
        </div>
      </header>

      {error && (
        <div style={{
          padding: '12px 24px',
          background: '#3a1a1a',
          borderBottom: '1px solid #4a2a2a',
          color: '#ff6b6b',
          fontSize: '14px'
        }}>
          错误: {error}
        </div>
      )}

      <main style={{
        flex: 1,
        padding: '20px 24px',
        display: 'flex',
        flexDirection: 'column',
        gap: '20px'
      }}>
        {showReplayPanel && status === 'connected' && (
          <div style={{
            background: '#121629',
            borderRadius: '8px',
            padding: '16px',
            border: '1px solid #1a1f3a'
          }}>
            <div style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
              <span style={{ color: '#888', fontSize: '14px' }}>回放控制:</span>
              <button
                onClick={handleStartAllReplay}
                disabled={replayMode}
                style={{
                  padding: '6px 12px',
                  background: replayMode ? '#333' : '#4ecdc4',
                  color: '#fff',
                  border: 'none',
                  borderRadius: '4px',
                  cursor: replayMode ? 'not-allowed' : 'pointer',
                  fontSize: '12px'
                }}
              >
                回放全部数据
              </button>
              <input
                type="number"
                placeholder="传感器ID"
                min="0"
                max={TOTAL_SENSORS - 1}
                onChange={(e) => setReplaySensorId(parseInt(e.target.value) || null)}
                style={{
                  padding: '6px 12px',
                  background: '#1a1f3a',
                  border: '1px solid #2a2f4a',
                  borderRadius: '4px',
                  color: '#e0e0e0',
                  fontSize: '12px',
                  width: '100px'
                }}
              />
              <button
                onClick={() => replaySensorId !== null && handleStartReplay(replaySensorId)}
                disabled={replayMode || replaySensorId === null}
                style={{
                  padding: '6px 12px',
                  background: (replayMode || replaySensorId === null) ? '#333' : '#4ecdc4',
                  color: '#fff',
                  border: 'none',
                  borderRadius: '4px',
                  cursor: (replayMode || replaySensorId === null) ? 'not-allowed' : 'pointer',
                  fontSize: '12px'
                }}
              >
                回放指定传感器
              </button>

              {replayMode && (
                <>
                  <button
                    onClick={() => setIsPlaying(prev => !prev)}
                    style={{
                      padding: '6px 12px',
                      background: '#ffa500',
                      color: '#fff',
                      border: 'none',
                      borderRadius: '4px',
                      cursor: 'pointer',
                      fontSize: '12px'
                    }}
                  >
                    {isPlaying ? '暂停' : '继续'}
                  </button>
                  <button
                    onClick={handleStopReplay}
                    style={{
                      padding: '6px 12px',
                      background: '#ff6b6b',
                      color: '#fff',
                      border: 'none',
                      borderRadius: '4px',
                      cursor: 'pointer',
                      fontSize: '12px'
                    }}
                  >
                    停止回放
                  </button>
                  <span style={{ color: '#888', fontSize: '12px' }}>
                    进度: {replayIndex}
                  </span>
                </>
              )}
            </div>
          </div>
        )}

        {showAnomalyPanel && status === 'connected' && (
          <div style={{
            background: '#121629',
            borderRadius: '8px',
            padding: '16px',
            border: '1px solid #1a1f3a'
          }}>
            <div style={{
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center',
              marginBottom: '12px'
            }}>
              <span style={{ color: '#888', fontSize: '14px' }}>
                异常报告历史 ({anomalyHistory.length} 条)
              </span>
              <button
                onClick={async () => {
                  const reports = await loadAnomalyReports()
                  setAnomalyHistory(reports)
                }}
                style={{
                  padding: '4px 12px',
                  background: '#1a1f3a',
                  color: '#4ecdc4',
                  border: '1px solid #2a2f4a',
                  borderRadius: '4px',
                  cursor: 'pointer',
                  fontSize: '12px'
                }}
              >
                刷新
              </button>
            </div>

            {anomalyHistory.length === 0 ? (
              <div style={{
                padding: '20px',
                textAlign: 'center',
                color: '#666',
                fontSize: '14px'
              }}>
                暂无异常记录
              </div>
            ) : (
              <div style={{
                maxHeight: '200px',
                overflowY: 'auto'
              }}>
                {anomalyHistory.slice(0, 20).map((record) => (
                  <div
                    key={record.id}
                    onClick={() => setSelectedAnomaly(record)}
                    style={{
                      padding: '8px 12px',
                      marginBottom: '4px',
                      background: selectedAnomaly?.id === record.id ? '#1a3a4a' : '#1a1f3a',
                      border: `1px solid ${selectedAnomaly?.id === record.id ? '#4ecdc4' : '#2a2f4a'}`,
                      borderRadius: '4px',
                      cursor: 'pointer',
                      display: 'flex',
                      justifyContent: 'space-between',
                      alignItems: 'center'
                    }}
                  >
                    <div style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
                      <span style={{
                        padding: '2px 8px',
                        background: getSeverityColor(record.severity) + '33',
                        color: getSeverityColor(record.severity),
                        borderRadius: '10px',
                        fontSize: '11px'
                      }}>
                        {getSeverityText(record.severity)}
                      </span>
                      <span style={{ color: '#aaa', fontSize: '12px' }}>
                        传感器 #{record.sensorId}
                      </span>
                      <span style={{ color: '#888', fontSize: '12px' }}>
                        {formatAnomalyType(record.anomalyType)}
                      </span>
                    </div>
                    <span style={{ color: '#666', fontSize: '11px' }}>
                      {new Date(record.timestamp).toLocaleTimeString()}
                    </span>
                  </div>
                ))}
              </div>
            )}

            {selectedAnomaly && (
              <div style={{
                marginTop: '12px',
                padding: '12px',
                background: '#0a0e27',
                borderRadius: '4px',
                border: '1px solid #2a2f4a'
              }}>
                <div style={{ color: '#4ecdc4', fontSize: '13px', marginBottom: '8px' }}>
                  异常详情
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px', fontSize: '12px' }}>
                  <div style={{ color: '#888' }}>传感器ID:</div>
                  <div style={{ color: '#e0e0e0' }}>#{selectedAnomaly.sensorId}</div>
                  <div style={{ color: '#888' }}>异常类型:</div>
                  <div style={{ color: '#ff6b6b' }}>{formatAnomalyType(selectedAnomaly.anomalyType)}</div>
                  <div style={{ color: '#888' }}>严重程度:</div>
                  <div style={{ color: getSeverityColor(selectedAnomaly.severity) }}>
                    {getSeverityText(selectedAnomaly.severity)}
                  </div>
                  <div style={{ color: '#888' }}>开始时间:</div>
                  <div style={{ color: '#e0e0e0' }}>
                    {new Date(selectedAnomaly.timestamp).toLocaleString()}
                  </div>
                  <div style={{ color: '#888' }}>结束时间:</div>
                  <div style={{ color: '#e0e0e0' }}>
                    {new Date(selectedAnomaly.endTimestamp).toLocaleString()}
                  </div>
                  <div style={{ color: '#888' }}>检测指标:</div>
                  <div style={{ color: '#e0e0e0' }}>{selectedAnomaly.stats.metric}</div>
                  <div style={{ color: '#888' }}>均值:</div>
                  <div style={{ color: '#e0e0e0' }}>{selectedAnomaly.stats.mean.toFixed(4)}</div>
                  <div style={{ color: '#888' }}>标准差:</div>
                  <div style={{ color: '#e0e0e0' }}>{selectedAnomaly.stats.stdDev.toFixed(4)}</div>
                </div>
                <div style={{ marginTop: '8px', fontSize: '12px', color: '#888' }}>
                  数据点数: {selectedAnomaly.dataBefore.length + selectedAnomaly.dataDuring.length + selectedAnomaly.dataAfter.length}
                  (前: {selectedAnomaly.dataBefore.length}, 异常: {selectedAnomaly.dataDuring.length}, 后: {selectedAnomaly.dataAfter.length})
                </div>
              </div>
            )}
          </div>
        )}

        <SensorSelector
          sensors={allSensors}
          selectedSensors={selectedSensors}
          onToggle={handleToggleSensor}
          maxSelected={MAX_SELECTED}
        />

        <div style={{
          display: 'grid',
          gridTemplateColumns: `repeat(${Math.min(selectedSensors.length, 2)}, 1fr)`,
          gap: '16px',
          flex: 1,
          minHeight: '400px'
        }}>
          {selectedSensors.map((sensorId, index) => (
            <div
              key={sensorId}
              style={{
                background: '#121629',
                borderRadius: '8px',
                padding: '12px',
                border: `1px solid ${CHART_COLORS[index % CHART_COLORS.length]}33`,
                display: 'flex',
                flexDirection: 'column'
              }}
            >
              <div style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                marginBottom: '8px'
              }}>
                <span style={{
                  color: CHART_COLORS[index % CHART_COLORS.length],
                  fontSize: '14px',
                  fontWeight: 'bold'
                }}>
                  传感器 #{sensorId}
                </span>
                {dataMap.get(sensorId) && (
                  <div style={{ display: 'flex', gap: '12px', fontSize: '11px' }}>
                    <span style={{ color: '#ff6b6b' }}>
                      温度: {(dataMap.get(sensorId)!.temp).toFixed(2)}°C
                    </span>
                    <span style={{ color: '#4ecdc4' }}>
                      压力: {(dataMap.get(sensorId)!.pressure).toFixed(2)}kPa
                    </span>
                    <span style={{ color: '#45b7d1' }}>
                      流量: {(dataMap.get(sensorId)!.flow).toFixed(2)}L/min
                    </span>
                    <span style={{ color: '#96ceb4' }}>
                      振动: {(dataMap.get(sensorId)!.vibration).toFixed(3)}mm/s
                    </span>
                  </div>
                )}
              </div>
              <div style={{ flex: 1, minHeight: '200px' }}>
                <WaveformChart
                  data={getDisplayData(sensorId)}
                  title={`传感器 #${sensorId} 实时波形`}
                  color={CHART_COLORS[index % CHART_COLORS.length]}
                  anomalySegments={getAnomalySegmentsForSensor(sensorId)}
                  showAnomalyHighlight={!replayMode}
                />
              </div>
            </div>
          ))}

          {selectedSensors.length === 0 && (
            <div style={{
              gridColumn: '1 / -1',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              color: '#666',
              fontSize: '16px',
              background: '#121629',
              borderRadius: '8px',
              padding: '40px'
            }}>
              {status === 'connected'
                ? '请选择上方的传感器以查看实时数据波形'
                : '请先连接服务器'}
            </div>
          )}
        </div>
      </main>

      <footer style={{
        padding: '12px 24px',
        background: '#121629',
        borderTop: '1px solid #1a1f3a',
        display: 'flex',
        justifyContent: 'space-between',
        fontSize: '12px',
        color: '#666'
      }}>
        <span>工业仪表数据实时监控系统 v1.0</span>
        <span>使用 WebTransport 协议 | 端到端延迟目标: {'<'} 50ms</span>
      </footer>

      <style>{`
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
        ::-webkit-scrollbar {
          width: 6px;
          height: 6px;
        }
        ::-webkit-scrollbar-track {
          background: #0a0e27;
        }
        ::-webkit-scrollbar-thumb {
          background: #2a2f4a;
          border-radius: 3px;
        }
        ::-webkit-scrollbar-thumb:hover {
          background: #3a3f5a;
        }
      `}</style>
    </div>
  )
}
