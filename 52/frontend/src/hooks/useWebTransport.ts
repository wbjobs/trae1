import { useState, useCallback, useRef, useEffect } from 'react'

export interface SensorData {
  sensor_id: number
  timestamp: number
  temp: number
  pressure: number
  flow: number
  vibration: number
  seq: number
}

export type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'error'

interface PendingMessage {
  type: string
  sensor_id?: number
  resolve?: (value: SensorData[]) => void
  reject?: (reason: Error) => void
}

interface BufferEntry {
  data: SensorData
  arrivedAt: number
}

const MAX_BUFFER_SIZE = 2000
const TIME_WINDOW_MS = 5000
const JITTER_THRESHOLD_MS = 50
const SORT_INTERVAL_MS = 16

export function useWebTransport(url: string) {
  const [status, setStatus] = useState<ConnectionStatus>('disconnected')
  const [error, setError] = useState<string | null>(null)
  const [dataMap, setDataMap] = useState<Map<number, SensorData>>(new Map())
  const [latency, setLatency] = useState<number>(0)
  const [isJitterDetected, setIsJitterDetected] = useState<boolean>(false)
  const [jitterLevel, setJitterLevel] = useState<'none' | 'mild' | 'severe'>('none')
  const [orderedDataMap, setOrderedDataMap] = useState<Map<number, SensorData[]>>(new Map())

  const transportRef = useRef<WebTransport | null>(null)
  const streamsRef = useRef<Map<number, ReadableStreamDefaultReader<Uint8Array>>>(new Map())
  const controlStreamRef = useRef<WritableStreamDefaultWriter<Uint8Array> | null>(null)

  const rawBufferRef = useRef<Map<number, BufferEntry[]>>(new Map())
  const lastSeqRef = useRef<Map<number, number>>(new Map())
  const jitterCountRef = useRef<number>(0)
  const lastJitterCheckRef = useRef<number>(0)
  const sortIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null)

  const processBuffer = useCallback(() => {
    const now = Date.now()
    let hasOutOfOrder = false

    const newOrderedData = new Map<number, SensorData[]>()

    rawBufferRef.current.forEach((buffer, sensorId) => {
      if (buffer.length === 0) return

      const cutoffTime = now - TIME_WINDOW_MS

      const filtered = buffer.filter(entry => entry.data.timestamp >= cutoffTime)

      filtered.sort((a, b) => {
        if (a.data.timestamp !== b.data.timestamp) {
          return a.data.timestamp - b.data.timestamp
        }
        return a.data.seq - b.data.seq
      })

      const lastSeq = lastSeqRef.current.get(sensorId) || 0
      for (let i = 0; i < filtered.length; i++) {
        if (filtered[i].data.seq < lastSeq && filtered[i].data.timestamp < cutoffTime + 1000) {
          hasOutOfOrder = true
        }
      }

      if (filtered.length > 0) {
        const newLastSeq = filtered[filtered.length - 1].data.seq
        lastSeqRef.current.set(sensorId, newLastSeq)
      }

      const ordered = filtered
        .slice(-MAX_BUFFER_SIZE)
        .map(entry => entry.data)

      newOrderedData.set(sensorId, ordered)

      if (filtered.length > 0) {
        const latest = filtered[filtered.length - 1].data
        setDataMap(prev => {
          const newMap = new Map(prev)
          newMap.set(sensorId, latest)
          return newMap
        })
      }
    })

    setOrderedDataMap(newOrderedData)

    const nowMs = Date.now()
    if (hasOutOfOrder) {
      jitterCountRef.current++
    }

    if (nowMs - lastJitterCheckRef.current > 1000) {
      const jitterRate = jitterCountRef.current / ((nowMs - lastJitterCheckRef.current) / 1000)

      if (jitterRate > 5) {
        setJitterLevel('severe')
        setIsJitterDetected(true)
      } else if (jitterRate > 1) {
        setJitterLevel('mild')
        setIsJitterDetected(true)
      } else {
        setJitterLevel('none')
        setIsJitterDetected(false)
      }

      jitterCountRef.current = 0
      lastJitterCheckRef.current = nowMs
    }
  }, [])

  const addToBuffer = useCallback((data: SensorData) => {
    const sensorId = data.sensor_id
    const arrivedAt = Date.now()

    let buffer = rawBufferRef.current.get(sensorId)
    if (!buffer) {
      buffer = []
      rawBufferRef.current.set(sensorId, buffer)
    }

    buffer.push({ data, arrivedAt })

    if (buffer.length > MAX_BUFFER_SIZE * 2) {
      buffer.sort((a, b) => a.data.timestamp - b.data.timestamp)
      const cutoffTime = arrivedAt - TIME_WINDOW_MS
      const filtered = buffer.filter(entry => entry.data.timestamp >= cutoffTime)
      rawBufferRef.current.set(sensorId, filtered.slice(-MAX_BUFFER_SIZE * 2))
    }

    const sendTime = data.timestamp
    const arrivalDelay = arrivedAt - sendTime
    if (arrivalDelay > JITTER_THRESHOLD_MS) {
      jitterCountRef.current++
    }
  }, [])

  const connect = useCallback(async () => {
    if (status === 'connecting' || status === 'connected') return

    setStatus('connecting')
    setError(null)

    try {
      const transport = new WebTransport(url, {
        serverCertificateHashes: [
          {
            algorithm: 'sha-256',
            value: new Uint8Array(32)
          }
        ]
      })

      transport.closed.then(() => {
        setStatus('disconnected')
      }).catch((err) => {
        setError(`Connection closed: ${err}`)
        setStatus('error')
      })

      await transport.ready
      transportRef.current = transport

      const { readable, writable } = await transport.createBidirectionalStream()
      controlStreamRef.current = writable.getWriter()

      setStatus('connected')
      setError(null)

      sortIntervalRef.current = setInterval(processBuffer, SORT_INTERVAL_MS)

      const reader = readable.getReader()
      const readControl = async () => {
        try {
          while (true) {
            const { done } = await reader.read()
            if (done) break
          }
        } catch {
          // Stream closed
        }
      }
      readControl()

    } catch (err) {
      setError(`Failed to connect: ${err}`)
      setStatus('error')
    }
  }, [url, status, processBuffer])

  const disconnect = useCallback(() => {
    if (sortIntervalRef.current) {
      clearInterval(sortIntervalRef.current)
      sortIntervalRef.current = null
    }
    if (transportRef.current) {
      transportRef.current.close()
      transportRef.current = null
    }
    streamsRef.current.clear()
    controlStreamRef.current = null
    rawBufferRef.current.clear()
    lastSeqRef.current.clear()
    jitterCountRef.current = 0
    setStatus('disconnected')
    setIsJitterDetected(false)
    setJitterLevel('none')
  }, [])

  const sendControl = useCallback(async (message: PendingMessage): Promise<SensorData[] | void> => {
    if (!controlStreamRef.current || !transportRef.current) {
      throw new Error('Not connected')
    }

    const encoder = new TextEncoder()
    const data = encoder.encode(JSON.stringify(message))

    const lengthBuffer = new ArrayBuffer(4)
    new DataView(lengthBuffer).setUint32(0, data.length)
    const lengthBytes = new Uint8Array(lengthBuffer)

    await controlStreamRef.current.write(new Uint8Array([...lengthBytes, ...data]))

    if (message.type === 'replay' || message.type === 'replay_all') {
      return new Promise<SensorData[]>((resolve, reject) => {
        const startReplay = async () => {
          if (!transportRef.current) {
            reject(new Error('Not connected'))
            return
          }
          try {
            const { readable } = await transportRef.current.createBidirectionalStream()
            const reader = readable.getReader()
            const results: SensorData[] = []

            while (true) {
              const { value, done } = await reader.read()
              if (done) break

              const view = new DataView(value.buffer)
              let offset = 0
              while (offset < value.length) {
                const length = view.getUint32(offset)
                offset += 4
                const jsonBytes = value.slice(offset, offset + length)
                const text = new TextDecoder().decode(jsonBytes)
                const parsed = JSON.parse(text) as SensorData
                results.push(parsed)
                offset += length
              }
            }
            resolve(results)
          } catch (err) {
            reject(err as Error)
          }
        }
        startReplay()
      })
    }
  }, [])

  const subscribeSensor = useCallback(async (sensorId: number): Promise<boolean> => {
    if (!transportRef.current) return false

    try {
      await sendControl({ type: 'subscribe', sensor_id: sensorId })

      const { readable } = await transportRef.current.createBidirectionalStream()
      const reader = readable.getReader()
      streamsRef.current.set(sensorId, reader)

      const readData = async () => {
        try {
          while (true) {
            const { value, done } = await reader.read()
            if (done) break

            const view = new DataView(value.buffer)
            let offset = 0
            while (offset < value.length) {
              const length = view.getUint32(offset)
              offset += 4
              const jsonBytes = value.slice(offset, offset + length)
              const text = new TextDecoder().decode(jsonBytes)
              const data = JSON.parse(text) as SensorData

              const now = performance.now()
              const sendTime = data.timestamp
              const currentLatency = now - (sendTime / 1000)
              if (currentLatency > 0) {
                setLatency(Math.round(currentLatency))
              }

              addToBuffer(data)
              offset += length
            }
          }
        } catch {
          // Stream closed
        }
      }
      readData()

      return true
    } catch {
      return false
    }
  }, [sendControl, addToBuffer])

  const unsubscribeSensor = useCallback(async (sensorId: number) => {
    try {
      await sendControl({ type: 'unsubscribe', sensor_id: sensorId })
      const reader = streamsRef.current.get(sensorId)
      if (reader) {
        await reader.cancel()
        streamsRef.current.delete(sensorId)
      }
      rawBufferRef.current.delete(sensorId)
      lastSeqRef.current.delete(sensorId)
      setOrderedDataMap(prev => {
        const newMap = new Map(prev)
        newMap.delete(sensorId)
        return newMap
      })
    } catch {
      // Ignore errors on unsubscribe
    }
  }, [sendControl])

  const replaySensor = useCallback(async (sensorId: number): Promise<SensorData[]> => {
    const result = await sendControl({ type: 'replay', sensor_id: sensorId })
    return result as SensorData[] || []
  }, [sendControl])

  const replayAll = useCallback(async (): Promise<SensorData[]> => {
    const result = await sendControl({ type: 'replay_all' })
    return result as SensorData[] || []
  }, [sendControl])

  useEffect(() => {
    return () => {
      if (sortIntervalRef.current) {
        clearInterval(sortIntervalRef.current)
      }
      if (transportRef.current) {
        transportRef.current.close()
      }
    }
  }, [])

  return {
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
  }
}
