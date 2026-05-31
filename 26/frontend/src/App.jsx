import React, { useState, useEffect, useRef, useCallback } from 'react'
import ZoneChart from './components/ZoneChart.jsx'

const RANGE_OPTIONS = [
  { label: '最近5分钟', value: '5m' },
  { label: '最近1小时', value: '1h' },
  { label: '最近24小时', value: '24h' },
]

const METRICS = [
  { key: 'temperature', name: '温度', unit: '°C', color: '#ff6b6b' },
  { key: 'humidity', name: '湿度', unit: '%', color: '#4ecdc4' },
  { key: 'light', name: '光照强度', unit: 'lx', color: '#ffd93d' },
  { key: 'co2', name: 'CO₂浓度', unit: 'ppm', color: '#a78bfa' },
]

const ALERT_LABELS = {
  temp_high: '🌡️ 温度过高',
  humid_low: '💧 湿度过低',
}

export default function App() {
  const [zones, setZones] = useState([])
  const [selectedRange, setSelectedRange] = useState('5m')
  const [alertList, setAlertList] = useState([])
  const wsRef = useRef(null)
  const dataMapRef = useRef({})
  const alertsMapRef = useRef({})
  const recentTimestampsRef = useRef({})
  const notifiedAlertIdsRef = useRef({})
  const DEDUP_WINDOW = 300

  const isDuplicate = (zoneId, timestamp) => {
    const key = `${zoneId}_${timestamp}`
    const set = recentTimestampsRef.current
    if (set[key]) return true
    const keys = Object.keys(set)
    if (keys.length > DEDUP_WINDOW * 5) {
      const cutoff = Date.now() / 1000 - DEDUP_WINDOW
      for (const k of keys) {
        const ts = parseInt(k.split('_')[1], 10)
        if (ts < cutoff) delete set[k]
      }
    }
    set[key] = true
    return false
  }

  const showNotification = useCallback((alert) => {
    if (notifiedAlertIdsRef.current[alert.id] && !alert.resolved) return
    notifiedAlertIdsRef.current[alert.id] = true

    if (!('Notification' in window)) return
    if (Notification.permission !== 'granted') return

    const title = alert.resolved
      ? `✅ ${alert.zone_name} - 已恢复`
      : `⚠️ ${alert.zone_name} - ${ALERT_LABELS[alert.type] || alert.type}`
    const body = alert.resolved
      ? `异常已消除，持续时长 ${Math.round(alert.end_time - alert.start_time)} 秒`
      : alert.message

    try {
      new Notification(title, {
        body,
        icon: 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><text y="80" font-size="80">🌱</text></svg>',
        tag: alert.id,
      })
    } catch (e) {
      console.warn('通知失败:', e)
    }
  }, [])

  const handleIncomingAlert = useCallback((alert) => {
    const map = alertsMapRef.current
    if (!map[alert.zone_id]) map[alert.zone_id] = []
    const arr = map[alert.zone_id]

    const existingIdx = arr.findIndex(a => a.id === alert.id)
    if (existingIdx >= 0) {
      arr[existingIdx] = alert
    } else {
      arr.unshift(alert)
      if (arr.length > 100) arr.pop()
    }

    setAlertList(prev => {
      const filtered = prev.filter(a => a.id !== alert.id)
      const next = [alert, ...filtered]
      return next.slice(0, 50)
    })

    showNotification(alert)
    window.dispatchEvent(new CustomEvent('alert-update', { detail: alert }))
  }, [showNotification])

  useEffect(() => {
    fetch('/api/zones')
      .then(r => r.json())
      .then(data => setZones(data.zones || []))
      .catch(err => console.error('加载区域失败:', err))
  }, [])

  useEffect(() => {
    fetch(`/api/alerts?range=${selectedRange}`)
      .then(r => r.json())
      .then(data => {
        const alerts = data.alerts || []
        setAlertList(alerts)
        const map = {}
        alerts.forEach(a => {
          if (!map[a.zone_id]) map[a.zone_id] = []
          map[a.zone_id].push(a)
        })
        alertsMapRef.current = map
      })
      .catch(err => console.error('加载预警历史失败:', err))
  }, [selectedRange])

  useEffect(() => {
    if ('Notification' in window && Notification.permission === 'default') {
      Notification.requestPermission().then(perm => {
        console.log('通知权限:', perm)
      })
    }

    const init = () => {
      const proto = location.protocol === 'https:' ? 'wss' : 'ws'
      const ws = new WebSocket(`${proto}://${location.host}/api/realtime`)
      wsRef.current = ws

      ws.onopen = () => console.log('[WS] 已连接')

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data)

          if (msg.type === 'alert') {
            handleIncomingAlert(msg.data)
            return
          }

          if (msg.type !== 'sensor') return

          const reading = msg.data
          if (isDuplicate(reading.zone_id, reading.timestamp)) {
            console.log(`[去重] 丢弃重复点 zone=${reading.zone_id} ts=${reading.timestamp}`)
            return
          }
          const map = dataMapRef.current
          if (!map[reading.zone_id]) map[reading.zone_id] = []
          const arr = map[reading.zone_id]
          arr.push(reading)
          const maxPoints = selectedRange === '5m' ? 300 : selectedRange === '1h' ? 3600 : 86400
          if (arr.length > maxPoints) arr.splice(0, arr.length - maxPoints)
          window.dispatchEvent(new CustomEvent('sensor-update', { detail: reading }))
        } catch (e) {
          console.error('解析消息失败:', e)
        }
      }

      ws.onerror = (err) => console.error('[WS] 错误:', err)

      ws.onclose = () => {
        console.log('[WS] 连接断开，3秒后重连...')
        setTimeout(init, 3000)
      }
    }

    init()
    return () => {
      if (wsRef.current) wsRef.current.close()
    }
  }, [selectedRange, handleIncomingAlert])

  const handleRangeChange = (val) => {
    setSelectedRange(val)
    Object.keys(dataMapRef.current).forEach(k => {
      dataMapRef.current[k] = []
    })
    recentTimestampsRef.current = {}
  }

  return (
    <div style={{ padding: '20px', minHeight: '100vh' }}>
      <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
        <h1 style={{ fontSize: '24px', fontWeight: 600 }}>🌱 温室环境模拟系统</h1>
        <div style={{ display: 'flex', gap: '8px' }}>
          {RANGE_OPTIONS.map(opt => (
            <button
              key={opt.value}
              onClick={() => handleRangeChange(opt.value)}
              style={{
                padding: '8px 16px',
                border: '1px solid #2a3a4a',
                borderRadius: '6px',
                background: selectedRange === opt.value ? '#1a73e8' : '#1a2938',
                color: '#fff',
                cursor: 'pointer',
                fontSize: '13px',
              }}
            >
              {opt.label}
            </button>
          ))}
        </div>
      </header>

      <div style={{ display: 'flex', gap: '12px', flexWrap: 'wrap', marginBottom: '16px' }}>
        {METRICS.map(m => (
          <div key={m.key} style={{
            display: 'flex', alignItems: 'center', gap: '6px',
            background: '#1a2938', padding: '6px 12px', borderRadius: '6px', fontSize: '13px',
          }}>
            <span style={{ width: 10, height: 10, borderRadius: '50%', background: m.color }}></span>
            {m.name} ({m.unit})
          </div>
        ))}
        <div style={{
          display: 'flex', alignItems: 'center', gap: '6px',
          background: '#3a1a1a', padding: '6px 12px', borderRadius: '6px', fontSize: '13px',
        }}>
          <span style={{ width: 10, height: 10, borderRadius: '2px', background: 'rgba(255,80,80,0.25)', border: '1px solid rgba(255,80,80,0.5)' }}></span>
          异常时段
        </div>
      </div>

      {alertList.length > 0 && (
        <div style={{ marginBottom: '20px', background: '#1a2938', borderRadius: '8px', padding: '12px', maxHeight: '120px', overflowY: 'auto' }}>
          <div style={{ fontSize: '13px', color: '#9aa7b4', marginBottom: '8px', fontWeight: 600 }}>📢 预警事件 ({alertList.length})</div>
          {alertList.slice(0, 10).map(a => (
            <div key={a.id} style={{
              fontSize: '12px',
              padding: '4px 8px',
              marginBottom: '4px',
              borderRadius: '4px',
              background: a.resolved ? 'rgba(80,200,120,0.1)' : 'rgba(255,80,80,0.15)',
              borderLeft: `3px solid ${a.resolved ? '#50c878' : '#ff5050'}`,
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center',
            }}>
              <span>
                {a.resolved ? '✅' : '⚠️'} [{a.zone_name}] {ALERT_LABELS[a.type] || a.type} - {a.message}
              </span>
              <span style={{ color: '#6a7a8a', fontSize: '11px' }}>
                {new Date(a.start_time * 1000).toLocaleTimeString()}
              </span>
            </div>
          ))}
        </div>
      )}

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(520px, 1fr))', gap: '20px' }}>
        {zones.map(zone => (
          <ZoneChart
            key={zone.id}
            zone={zone}
            metrics={METRICS}
            range={selectedRange}
            dataMapRef={dataMapRef}
            alertsMapRef={alertsMapRef}
          />
        ))}
      </div>
    </div>
  )
}
