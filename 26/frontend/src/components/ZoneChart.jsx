import React, { useEffect, useRef, useState, useCallback } from 'react'
import ReactECharts from 'echarts-for-react'

export default function ZoneChart({ zone, metrics, range, dataMapRef, alertsMapRef }) {
  const chartRef = useRef(null)
  const [option, setOption] = useState(null)
  const [historyLoaded, setHistoryLoaded] = useState(false)
  const [alertsLoaded, setAlertsLoaded] = useState(false)

  const formatTime = (ts) => {
    const d = new Date(ts * 1000)
    return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}:${String(d.getSeconds()).padStart(2, '0')}`
  }

  const findTimeIndex = (times, targetTs) => {
    const targetTime = formatTime(targetTs)
    for (let i = 0; i < times.length; i++) {
      if (times[i] >= targetTime) return i
    }
    return times.length - 1
  }

  const buildOption = useCallback((records, alerts) => {
    if (!records || records.length === 0) {
      return {
        backgroundColor: '#1a2938',
        title: {
          text: zone.name,
          textStyle: { color: '#e0e6ed', fontSize: 14, fontWeight: 600 },
          left: 12,
          top: 8,
        },
        tooltip: { trigger: 'axis' },
        legend: {
          data: metrics.map(m => m.name),
          textStyle: { color: '#9aa7b4', fontSize: 11 },
          top: 8, right: 12,
        },
        grid: { left: 50, right: 20, top: 48, bottom: 30 },
        xAxis: {
          type: 'category',
          data: [],
          axisLine: { lineStyle: { color: '#2a3a4a' } },
          axisLabel: { color: '#9aa7b4', fontSize: 10 },
        },
        yAxis: [
          {
            type: 'value', name: metrics[0].unit,
            nameTextStyle: { color: '#9aa7b4' },
            axisLabel: { color: '#9aa7b4' },
            splitLine: { lineStyle: { color: '#2a3a4a' } },
          },
          {
            type: 'value', name: metrics[2].unit,
            nameTextStyle: { color: '#9aa7b4' },
            axisLabel: { color: '#9aa7b4' },
            splitLine: { show: false },
          },
        ],
        series: metrics.map((m, idx) => ({
          name: m.name,
          type: 'line',
          yAxisIndex: idx === 2 || idx === 3 ? 1 : 0,
          smooth: true,
          symbol: 'none',
          lineStyle: { color: m.color, width: 1.5 },
          data: [],
        })),
      }
    }

    const times = records.map(r => formatTime(r.timestamp))

    const markAreaData = []
    if (alerts && alerts.length > 0) {
      const zoneAlerts = alerts.filter(a => a.zone_id === zone.id && !a.resolved)
      zoneAlerts.forEach(alert => {
        const startIdx = findTimeIndex(times, alert.start_time)
        const endIdx = alert.end_time ? findTimeIndex(times, alert.end_time) : times.length - 1
        if (startIdx < endIdx) {
          markAreaData.push([
            {
              xAxis: startIdx,
              itemStyle: { color: 'rgba(255, 80, 80, 0.12)', borderColor: 'rgba(255, 80, 80, 0.5)', borderWidth: 1 },
            },
            { xAxis: endIdx },
          ])
        }
      })
    }

    const series = metrics.map((m, idx) => ({
      name: m.name,
      type: 'line',
      yAxisIndex: idx === 2 || idx === 3 ? 1 : 0,
      smooth: true,
      symbol: 'none',
      lineStyle: { color: m.color, width: 1.5 },
      data: records.map(r => r[m.key]),
      markArea: idx === 0 && markAreaData.length > 0 ? { silent: true, data: markAreaData } : undefined,
    }))

    return {
      backgroundColor: '#1a2938',
      title: {
        text: zone.name,
        textStyle: { color: '#e0e6ed', fontSize: 14, fontWeight: 600 },
        left: 12,
        top: 8,
      },
      tooltip: {
        trigger: 'axis',
        backgroundColor: '#0f1923',
        borderColor: '#2a3a4a',
        textStyle: { color: '#e0e6ed' },
      },
      legend: {
        data: metrics.map(m => m.name),
        textStyle: { color: '#9aa7b4', fontSize: 11 },
        top: 8, right: 12,
      },
      grid: { left: 50, right: 20, top: 48, bottom: 30 },
      xAxis: {
        type: 'category',
        data: times,
        axisLine: { lineStyle: { color: '#2a3a4a' } },
        axisLabel: { color: '#9aa7b4', fontSize: 10 },
      },
      yAxis: [
        {
          type: 'value', name: metrics[0].unit,
          nameTextStyle: { color: '#9aa7b4' },
          axisLabel: { color: '#9aa7b4' },
          splitLine: { lineStyle: { color: '#2a3a4a' } },
        },
        {
          type: 'value', name: metrics[2].unit,
          nameTextStyle: { color: '#9aa7b4' },
          axisLabel: { color: '#9aa7b4' },
          splitLine: { show: false },
        },
      ],
      series,
    }
  }, [zone.id, zone.name, metrics])

  useEffect(() => {
    setHistoryLoaded(false)
    setAlertsLoaded(false)
    fetch(`/api/history?zone_id=${zone.id}&range=${range}`)
      .then(r => r.json())
      .then(data => {
        const records = data.records || []
        if (dataMapRef.current) {
          dataMapRef.current[zone.id] = records
        }
        const alerts = alertsMapRef.current?.[zone.id] || []
        setOption(buildOption(records, alerts))
        setHistoryLoaded(true)
        setAlertsLoaded(true)
      })
      .catch(err => {
        console.error(`加载 ${zone.name} 历史数据失败:`, err)
        setOption(buildOption([], []))
        setHistoryLoaded(true)
        setAlertsLoaded(true)
      })
  }, [zone.id, range, buildOption, dataMapRef, alertsMapRef])

  useEffect(() => {
    if (!historyLoaded) return

    const onUpdate = () => {
      const arr = dataMapRef.current?.[zone.id]
      if (!arr) return
      const alerts = alertsMapRef.current?.[zone.id] || []
      setOption(buildOption(arr, alerts))
    }

    const onAlertUpdate = (e) => {
      const alert = e.detail
      if (alert.zone_id !== zone.id) return
      const arr = dataMapRef.current?.[zone.id] || []
      const alerts = alertsMapRef.current?.[zone.id] || []
      setOption(buildOption(arr, alerts))
    }

    window.addEventListener('sensor-update', onUpdate)
    window.addEventListener('alert-update', onAlertUpdate)
    return () => {
      window.removeEventListener('sensor-update', onUpdate)
      window.removeEventListener('alert-update', onAlertUpdate)
    }
  }, [zone.id, historyLoaded, buildOption, dataMapRef, alertsMapRef])

  return (
    <div style={{
      background: '#1a2938',
      borderRadius: '10px',
      padding: '8px',
      boxShadow: '0 2px 8px rgba(0,0,0,0.3)',
      height: '280px',
    }}>
      {option ? (
        <ReactECharts
          ref={chartRef}
          option={option}
          style={{ height: '100%', width: '100%' }}
          opts={{ renderer: 'canvas' }}
          notMerge={true}
        />
      ) : (
        <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100%', color: '#9aa7b4' }}>
          加载中...
        </div>
      )}
    </div>
  )
}
