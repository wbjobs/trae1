import { useState, useCallback, useRef, useEffect } from 'react'
import type { SensorData } from './useWebTransport'
import { saveAnomaly, type AnomalyRecord } from '../services/indexedDB'

export interface AnomalySegment {
  id: string
  sensorId: number
  startTime: number
  endTime: number
  anomalyType: string
  severity: 'mild' | 'moderate' | 'severe'
  metric: string
  startIndex: number
  endIndex: number
}

interface SensorStats {
  mean: number
  stdDev: number
  slope: number
  lastValue: number
  values: number[]
  timestamps: number[]
}

const CONSECUTIVE_THRESHOLD = 10
const STD_DEV_MULTIPLIER = 3
const WINDOW_SIZE = 100
const PRE_ANOMALY_MS = 2000
const POST_ANOMALY_MS = 2000
const MIN_ANOMALY_INTERVAL_MS = 5000

const METRICS = ['temp', 'pressure', 'flow', 'vibration'] as const
type MetricKey = typeof METRICS[number]

export function useAnomalyDetection(
  orderedDataMap: Map<number, SensorData[]>,
  isEnabled: boolean = true
) {
  const [anomalySegments, setAnomalySegments] = useState<Map<number, AnomalySegment[]>>(new Map())
  const [anomalyReports, setAnomalyReports] = useState<AnomalyRecord[]>([])
  const [isAnalyzing, setIsAnalyzing] = useState<boolean>(false)

  const statsRef = useRef<Map<number, Map<MetricKey, SensorStats>>>(new Map())
  const lastAnomalyTimeRef = useRef<Map<number, number>>(new Map())
  const isProcessingRef = useRef<boolean>(false)

  const calculateStats = useCallback((values: number[]): { mean: number; stdDev: number; slope: number } => {
    if (values.length === 0) {
      return { mean: 0, stdDev: 0, slope: 0 }
    }

    const n = values.length
    const sum = values.reduce((a, b) => a + b, 0)
    const mean = sum / n

    const squaredDiffs = values.map(v => (v - mean) ** 2)
    const variance = squaredDiffs.reduce((a, b) => a + b, 0) / n
    const stdDev = Math.sqrt(variance)

    let slope = 0
    if (n >= 2) {
      const xMean = (n - 1) / 2
      let numerator = 0
      let denominator = 0
      for (let i = 0; i < n; i++) {
        numerator += (i - xMean) * (values[i] - mean)
        denominator += (i - xMean) ** 2
      }
      slope = denominator !== 0 ? numerator / denominator : 0
    }

    return { mean, stdDev, slope }
  }, [])

  const getAnomalyType = useCallback((metric: string, deviation: number, slope: number): string => {
    const types: string[] = []

    if (deviation > 0) {
      types.push(`${metric.toUpperCase()}_SPIKE`)
    } else {
      types.push(`${metric.toUpperCase()}_DIP`)
    }

    if (Math.abs(slope) > 1) {
      types.push('TREND_CHANGE')
    }

    return types.join('_')
  }, [])

  const getSeverity = useCallback((stdDeviations: number): 'mild' | 'moderate' | 'severe' => {
    const absDeviation = Math.abs(stdDeviations)
    if (absDeviation > 5) {
      return 'severe'
    } else if (absDeviation > 4) {
      return 'moderate'
    }
    return 'mild'
  }, [])

  const detectAnomalies = useCallback(async (
    sensorId: number,
    data: SensorData[]
  ): Promise<AnomalySegment[]> => {
    if (!isEnabled || data.length < WINDOW_SIZE) {
      return []
    }

    const detectedSegments: AnomalySegment[] = []

    for (const metric of METRICS) {
      const values = data.map(d => d[metric] as number)
      const timestamps = data.map(d => d.timestamp)

      const windowValues = values.slice(-WINDOW_SIZE)
      const windowTimestamps = timestamps.slice(-WINDOW_SIZE)

      const { mean, stdDev, slope } = calculateStats(windowValues)

      if (stdDev === 0) continue

      let consecutiveOutliers = 0
      let anomalyStartIndex = -1
      let maxDeviation = 0

      for (let i = 0; i < windowValues.length; i++) {
        const deviation = (windowValues[i] - mean) / stdDev

        if (Math.abs(deviation) > STD_DEV_MULTIPLIER) {
          if (consecutiveOutliers === 0) {
            anomalyStartIndex = i
          }
          consecutiveOutliers++
          maxDeviation = Math.max(maxDeviation, Math.abs(deviation))
        } else {
          if (consecutiveOutliers >= CONSECUTIVE_THRESHOLD) {
            const now = Date.now()
            const lastAnomalyTime = lastAnomalyTimeRef.current.get(sensorId) || 0

            if (now - lastAnomalyTime > MIN_ANOMALY_INTERVAL_MS) {
              const globalStartIndex = data.length - WINDOW_SIZE + anomalyStartIndex
              const globalEndIndex = data.length - WINDOW_SIZE + i - 1

              const startTime = windowTimestamps[anomalyStartIndex]
              const endTime = windowTimestamps[i - 1]

              const severity = getSeverity(maxDeviation)
              const anomalyType = getAnomalyType(metric, maxDeviation, slope)

              const segment: AnomalySegment = {
                id: `${sensorId}-${startTime}-${metric}`,
                sensorId,
                startTime,
                endTime,
                anomalyType,
                severity,
                metric,
                startIndex: globalStartIndex,
                endIndex: globalEndIndex
              }

              detectedSegments.push(segment)

              const preCutoff = startTime - PRE_ANOMALY_MS
              const postCutoff = endTime + POST_ANOMALY_MS

              const dataBefore = data.filter(d => d.timestamp >= preCutoff && d.timestamp < startTime)
              const dataDuring = data.slice(globalStartIndex, globalEndIndex + 1)
              const dataAfter = data.filter(d => d.timestamp > endTime && d.timestamp <= postCutoff)

              try {
                await saveAnomaly({
                  sensorId,
                  timestamp: startTime,
                  endTimestamp: endTime,
                  anomalyType,
                  severity,
                  stats: { mean, stdDev, metric },
                  dataBefore,
                  dataDuring,
                  dataAfter,
                  createdAt: Date.now()
                })
              } catch (err) {
                console.error('Failed to save anomaly:', err)
              }

              lastAnomalyTimeRef.current.set(sensorId, now)
            }
          }
          consecutiveOutliers = 0
          anomalyStartIndex = -1
          maxDeviation = 0
        }
      }

      if (consecutiveOutliers >= CONSECUTIVE_THRESHOLD) {
        const now = Date.now()
        const lastAnomalyTime = lastAnomalyTimeRef.current.get(sensorId) || 0

        if (now - lastAnomalyTime > MIN_ANOMALY_INTERVAL_MS) {
          const globalStartIndex = data.length - WINDOW_SIZE + anomalyStartIndex
          const globalEndIndex = data.length - 1

          const startTime = windowTimestamps[anomalyStartIndex]
          const endTime = windowTimestamps[windowValues.length - 1]

          const severity = getSeverity(maxDeviation)
          const anomalyType = getAnomalyType(metric, maxDeviation, slope)

          const segment: AnomalySegment = {
            id: `${sensorId}-${startTime}-${metric}`,
            sensorId,
            startTime,
            endTime,
            anomalyType,
            severity,
            metric,
            startIndex: globalStartIndex,
            endIndex: globalEndIndex
          }

          detectedSegments.push(segment)

          const preCutoff = startTime - PRE_ANOMALY_MS
          const postCutoff = endTime + POST_ANOMALY_MS

          const dataBefore = data.filter(d => d.timestamp >= preCutoff && d.timestamp < startTime)
          const dataDuring = data.slice(globalStartIndex, globalEndIndex + 1)
          const dataAfter = data.filter(d => d.timestamp > endTime && d.timestamp <= postCutoff)

          try {
            await saveAnomaly({
              sensorId,
              timestamp: startTime,
              endTimestamp: endTime,
              anomalyType,
              severity,
              stats: { mean, stdDev, metric },
              dataBefore,
              dataDuring,
              dataAfter,
              createdAt: Date.now()
            })
          } catch (err) {
            console.error('Failed to save anomaly:', err)
          }

          lastAnomalyTimeRef.current.set(sensorId, now)
        }
      }

      const sensorStats = statsRef.current.get(sensorId) || new Map()
      sensorStats.set(metric, {
        mean,
        stdDev,
        slope,
        lastValue: windowValues[windowValues.length - 1],
        values: windowValues,
        timestamps: windowTimestamps
      })
      statsRef.current.set(sensorId, sensorStats)
    }

    return detectedSegments
  }, [isEnabled, calculateStats, getAnomalyType, getSeverity])

  const analyzeAllSensors = useCallback(async () => {
    if (isProcessingRef.current) return

    isProcessingRef.current = true
    setIsAnalyzing(true)

    try {
      const newSegments = new Map<number, AnomalySegment[]>()

      for (const [sensorId, data] of orderedDataMap) {
        const segments = await detectAnomalies(sensorId, data)
        if (segments.length > 0) {
          newSegments.set(sensorId, segments)
        }
      }

      if (newSegments.size > 0) {
        setAnomalySegments(prev => {
          const updated = new Map(prev)
          for (const [sensorId, segments] of newSegments) {
            const existing = updated.get(sensorId) || []
            const now = Date.now()
            const filtered = existing.filter(s => now - s.endTime < 30000)
            const newSegmentsFiltered = segments.filter(s => {
              return !filtered.some(f =>
                f.startTime === s.startTime && f.metric === s.metric
              )
            })
            updated.set(sensorId, [...filtered, ...newSegmentsFiltered])
          }
          return updated
        })
      }
    } finally {
      isProcessingRef.current = false
      setIsAnalyzing(false)
    }
  }, [orderedDataMap, detectAnomalies])

  useEffect(() => {
    if (!isEnabled) return

    const interval = setInterval(() => {
      analyzeAllSensors()
    }, 100)

    return () => clearInterval(interval)
  }, [isEnabled, analyzeAllSensors])

  const getActiveAnomalies = useCallback((sensorId: number): AnomalySegment[] => {
    const now = Date.now()
    const segments = anomalySegments.get(sensorId) || []
    return segments.filter(s => now - s.endTime < 30000)
  }, [anomalySegments])

  const hasActiveAnomaly = useCallback((sensorId: number): boolean => {
    return getActiveAnomalies(sensorId).length > 0
  }, [getActiveAnomalies])

  const clearAnomalies = useCallback((sensorId?: number) => {
    if (sensorId !== undefined) {
      setAnomalySegments(prev => {
        const updated = new Map(prev)
        updated.delete(sensorId)
        return updated
      })
    } else {
      setAnomalySegments(new Map())
    }
  }, [])

  const loadAnomalyReports = useCallback(async (): Promise<AnomalyRecord[]> => {
    try {
      const reports = await import('../services/indexedDB').then(m => m.getAnomalies())
      setAnomalyReports(reports)
      return reports
    } catch (err) {
      console.error('Failed to load anomaly reports:', err)
      return []
    }
  }, [])

  return {
    anomalySegments,
    anomalyReports,
    isAnalyzing,
    getActiveAnomalies,
    hasActiveAnomaly,
    clearAnomalies,
    loadAnomalyReports
  }
}
