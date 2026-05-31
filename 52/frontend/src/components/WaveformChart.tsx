import { useEffect, useRef, useState } from 'react'
import type { SensorData } from '../hooks/useWebTransport'
import type { AnomalySegment } from '../hooks/useAnomalyDetection'

interface WaveformChartProps {
  data: SensorData[]
  title: string
  color: string
  maxDataPoints?: number
  anomalySegments?: AnomalySegment[]
  showAnomalyHighlight?: boolean
}

const metrics = [
  { key: 'temp', label: '温度', unit: '°C', color: '#ff6b6b' },
  { key: 'pressure', label: '压力', unit: 'kPa', color: '#4ecdc4' },
  { key: 'flow', label: '流量', unit: 'L/min', color: '#45b7d1' },
  { key: 'vibration', label: '振动', unit: 'mm/s', color: '#96ceb4' }
] as const

export function WaveformChart({
  data,
  title,
  color,
  maxDataPoints = 200,
  anomalySegments = [],
  showAnomalyHighlight = true
}: WaveformChartProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return

    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const dpr = window.devicePixelRatio || 1
    const rect = canvas.getBoundingClientRect()
    canvas.width = rect.width * dpr
    canvas.height = rect.height * dpr
    ctx.scale(dpr, dpr)

    const width = rect.width
    const height = rect.height
    const padding = { top: 20, right: 60, bottom: 30, left: 50 }
    const chartWidth = width - padding.left - padding.right
    const chartHeight = height - padding.top - padding.bottom

    ctx.fillStyle = '#0a0e27'
    ctx.fillRect(0, 0, width, height)

    if (showAnomalyHighlight && anomalySegments.length > 0 && data.length > 0) {
      const displayData = data.slice(-maxDataPoints)
      const startTime = displayData[0]?.timestamp || 0
      const endTime = displayData[displayData.length - 1]?.timestamp || Date.now()
      const timeRange = endTime - startTime || 1

      anomalySegments.forEach(segment => {
        const relativeStart = Math.max(0, (segment.startTime - startTime) / timeRange)
        const relativeEnd = Math.min(1, (segment.endTime - startTime) / timeRange)

        if (relativeEnd < 0 || relativeStart > 1) return

        const xStart = padding.left + relativeStart * chartWidth
        const xEnd = padding.left + relativeEnd * chartWidth

        let alpha: number
        switch (segment.severity) {
          case 'severe':
            alpha = 0.3
            break
          case 'moderate':
            alpha = 0.2
            break
          default:
            alpha = 0.15
        }

        ctx.fillStyle = `rgba(255, 107, 107, ${alpha})`
        ctx.fillRect(xStart, padding.top, xEnd - xStart, chartHeight)

        ctx.setLineDash([4, 4])
        ctx.strokeStyle = 'rgba(255, 107, 107, 0.5)'
        ctx.lineWidth = 1

        ctx.beginPath()
        ctx.moveTo(xStart, padding.top)
        ctx.lineTo(xStart, padding.top + chartHeight)
        ctx.stroke()

        ctx.beginPath()
        ctx.moveTo(xEnd, padding.top)
        ctx.lineTo(xEnd, padding.top + chartHeight)
        ctx.stroke()

        ctx.setLineDash([])
      })
    }

    ctx.strokeStyle = '#1a1f3a'
    ctx.lineWidth = 1
    for (let i = 0; i <= 5; i++) {
      const y = padding.top + (chartHeight / 5) * i
      ctx.beginPath()
      ctx.moveTo(padding.left, y)
      ctx.lineTo(padding.left + chartWidth, y)
      ctx.stroke()
    }
    for (let i = 0; i <= 5; i++) {
      const x = padding.left + (chartWidth / 5) * i
      ctx.beginPath()
      ctx.moveTo(x, padding.top)
      ctx.lineTo(x, padding.top + chartHeight)
      ctx.stroke()
    }

    if (data.length === 0) {
      ctx.fillStyle = '#666'
      ctx.font = '14px sans-serif'
      ctx.textAlign = 'center'
      ctx.fillText('等待数据...', width / 2, height / 2)
      return
    }

    const displayData = data.slice(-maxDataPoints)

    metrics.forEach((metric, metricIndex) => {
      const values = displayData.map(d => d[metric.key as keyof SensorData] as number)
      const minVal = Math.min(...values)
      const maxVal = Math.max(...values)
      const range = maxVal - minVal || 1

      ctx.beginPath()
      ctx.strokeStyle = metric.color
      ctx.lineWidth = 2

      displayData.forEach((point, i) => {
        const x = padding.left + (i / (maxDataPoints - 1)) * chartWidth
        const normalizedValue = ((point[metric.key as keyof SensorData] as number) - minVal) / range
        const y = padding.top + chartHeight - normalizedValue * chartHeight

        if (i === 0) {
          ctx.moveTo(x, y)
        } else {
          ctx.lineTo(x, y)
        }
      })
      ctx.stroke()

      if (showAnomalyHighlight && anomalySegments.length > 0) {
        anomalySegments.forEach(segment => {
          if (segment.metric !== metric.key) return

          for (let i = 0; i < displayData.length; i++) {
            const pointTime = displayData[i].timestamp
            if (pointTime >= segment.startTime && pointTime <= segment.endTime) {
              const x = padding.left + (i / (maxDataPoints - 1)) * chartWidth
              const normalizedValue = ((displayData[i][metric.key as keyof SensorData] as number) - minVal) / range
              const y = padding.top + chartHeight - normalizedValue * chartHeight

              ctx.beginPath()
              ctx.arc(x, y, 3, 0, Math.PI * 2)
              ctx.fillStyle = '#ff6b6b'
              ctx.fill()
            }
          }
        })
      }

      if (hoveredIndex !== null && hoveredIndex < displayData.length) {
        const x = padding.left + (hoveredIndex / (maxDataPoints - 1)) * chartWidth
        const point = displayData[hoveredIndex]
        const normalizedValue = ((point[metric.key as keyof SensorData] as number) - minVal) / range
        const y = padding.top + chartHeight - normalizedValue * chartHeight

        ctx.beginPath()
        ctx.arc(x, y, 4, 0, Math.PI * 2)
        ctx.fillStyle = metric.color
        ctx.fill()
      }

      const legendX = padding.left + 10 + metricIndex * 80
      const legendY = padding.top + 15
      ctx.fillStyle = metric.color
      ctx.fillRect(legendX, legendY, 12, 3)
      ctx.fillStyle = '#aaa'
      ctx.font = '10px sans-serif'
      ctx.textAlign = 'left'
      ctx.fillText(metric.label, legendX + 16, legendY + 4)
    })

    if (hoveredIndex !== null && hoveredIndex < displayData.length) {
      const point = displayData[hoveredIndex]
      const x = padding.left + (hoveredIndex / (maxDataPoints - 1)) * chartWidth

      ctx.setLineDash([5, 5])
      ctx.strokeStyle = '#555'
      ctx.beginPath()
      ctx.moveTo(x, padding.top)
      ctx.lineTo(x, padding.top + chartHeight)
      ctx.stroke()
      ctx.setLineDash([])

      const tooltipX = Math.min(x + 10, width - 120)
      const tooltipY = padding.top + 30

      ctx.fillStyle = 'rgba(30, 30, 50, 0.95)'
      ctx.fillRect(tooltipX, tooltipY, 110, 90)
      ctx.strokeStyle = '#444'
      ctx.strokeRect(tooltipX, tooltipY, 110, 90)

      ctx.fillStyle = '#fff'
      ctx.font = 'bold 11px sans-serif'
      ctx.textAlign = 'left'
      ctx.fillText(`时间: ${new Date(point.timestamp).toLocaleTimeString()}`, tooltipX + 5, tooltipY + 15)

      metrics.forEach((metric, i) => {
        ctx.fillStyle = metric.color
        ctx.font = '10px sans-serif'
        ctx.fillText(
          `${metric.label}: ${(point[metric.key as keyof SensorData] as number).toFixed(2)} ${metric.unit}`,
          tooltipX + 5,
          tooltipY + 32 + i * 15
        )
      })

      if (showAnomalyHighlight && anomalySegments.length > 0) {
        const hoveredTime = point.timestamp
        const anomalyAtPoint = anomalySegments.find(
          s => hoveredTime >= s.startTime && hoveredTime <= s.endTime
        )
        if (anomalyAtPoint) {
          ctx.fillStyle = '#ff6b6b'
          ctx.font = 'bold 10px sans-serif'
          ctx.fillText(`⚠ ${anomalyAtPoint.anomalyType}`, tooltipX + 5, tooltipY + 85)
        }
      }
    }

    ctx.fillStyle = '#888'
    ctx.font = '12px sans-serif'
    ctx.textAlign = 'left'
    ctx.fillText(title, padding.left, 15)

    if (showAnomalyHighlight && anomalySegments.length > 0) {
      const activeAnomalies = anomalySegments.filter(s => {
        const now = Date.now()
        return now - s.endTime < 30000
      })

      if (activeAnomalies.length > 0) {
        ctx.fillStyle = 'rgba(255, 107, 107, 0.9)'
        ctx.font = 'bold 10px sans-serif'
        ctx.textAlign = 'right'

        const severeCount = activeAnomalies.filter(a => a.severity === 'severe').length
        const moderateCount = activeAnomalies.filter(a => a.severity === 'moderate').length

        let statusText = ''
        if (severeCount > 0) {
          statusText = `⚠ 严重异常 (${severeCount})`
        } else if (moderateCount > 0) {
          statusText = `⚠ 中度异常 (${moderateCount})`
        } else {
          statusText = `⚠ 轻微异常 (${activeAnomalies.length})`
        }

        ctx.fillText(statusText, width - padding.right, 15)
      }
    }
  }, [data, title, color, maxDataPoints, hoveredIndex, anomalySegments, showAnomalyHighlight])

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current
    if (!canvas) return

    const rect = canvas.getBoundingClientRect()
    const padding = { left: 50, right: 60 }
    const chartWidth = rect.width - padding.left - padding.right
    const x = e.clientX - rect.left - padding.left

    if (x >= 0 && x <= chartWidth) {
      const index = Math.round((x / chartWidth) * (maxDataPoints - 1))
      setHoveredIndex(index)
    } else {
      setHoveredIndex(null)
    }
  }

  const handleMouseLeave = () => {
    setHoveredIndex(null)
  }

  return (
    <canvas
      ref={canvasRef}
      className="waveform-canvas"
      onMouseMove={handleMouseMove}
      onMouseLeave={handleMouseLeave}
      style={{
        width: '100%',
        height: '100%',
        background: '#0a0e27',
        borderRadius: '8px',
        border: '1px solid #1a1f3a'
      }}
    />
  )
}
