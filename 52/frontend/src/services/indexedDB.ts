import type { SensorData } from '../hooks/useWebTransport'

export interface AnomalyRecord {
  id?: string
  sensorId: number
  timestamp: number
  endTimestamp: number
  anomalyType: string
  severity: 'mild' | 'moderate' | 'severe'
  stats: {
    mean: number
    stdDev: number
    metric: string
  }
  dataBefore: SensorData[]
  dataDuring: SensorData[]
  dataAfter: SensorData[]
  createdAt: number
}

const DB_NAME = 'IndustrialSensorDB'
const DB_VERSION = 1
const STORE_NAME = 'anomalies'

let dbInstance: IDBDatabase | null = null

function openDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    if (dbInstance) {
      resolve(dbInstance)
      return
    }

    const request = indexedDB.open(DB_NAME, DB_VERSION)

    request.onerror = () => {
      reject(request.error)
    }

    request.onsuccess = () => {
      dbInstance = request.result
      resolve(dbInstance)
    }

    request.onupgradeneeded = (event) => {
      const db = (event.target as IDBOpenDBRequest).result

      if (!db.objectStoreNames.contains(STORE_NAME)) {
        const store = db.createObjectStore(STORE_NAME, { keyPath: 'id', autoIncrement: true })
        store.createIndex('sensorId', 'sensorId', { unique: false })
        store.createIndex('timestamp', 'timestamp', { unique: false })
        store.createIndex('anomalyType', 'anomalyType', { unique: false })
        store.createIndex('createdAt', 'createdAt', { unique: false })
      }
    }
  })
}

export async function saveAnomaly(record: Omit<AnomalyRecord, 'id'>): Promise<string> {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(STORE_NAME, 'readwrite')
    const store = transaction.objectStore(STORE_NAME)
    const recordToSave = {
      ...record,
      id: `${record.sensorId}-${record.timestamp}-${Date.now()}`
    }
    const request = store.add(recordToSave)

    request.onsuccess = () => {
      resolve(request.result as string)
    }

    request.onerror = () => {
      reject(request.error)
    }
  })
}

export async function getAnomalies(
  sensorId?: number,
  limit: number = 100
): Promise<AnomalyRecord[]> {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(STORE_NAME, 'readonly')
    const store = transaction.objectStore(STORE_NAME)

    let request: IDBRequest
    if (sensorId !== undefined) {
      const index = store.index('sensorId')
      request = index.getAll(sensorId)
    } else {
      request = store.getAll()
    }

    request.onsuccess = () => {
      const results = (request.result as AnomalyRecord[])
        .sort((a, b) => b.createdAt - a.createdAt)
        .slice(0, limit)
      resolve(results)
    }

    request.onerror = () => {
      reject(request.error)
    }
  })
}

export async function getAnomalyById(id: string): Promise<AnomalyRecord | undefined> {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(STORE_NAME, 'readonly')
    const store = transaction.objectStore(STORE_NAME)
    const request = store.get(id)

    request.onsuccess = () => {
      resolve(request.result as AnomalyRecord | undefined)
    }

    request.onerror = () => {
      reject(request.error)
    }
  })
}

export async function deleteAnomaly(id: string): Promise<void> {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(STORE_NAME, 'readwrite')
    const store = transaction.objectStore(STORE_NAME)
    const request = store.delete(id)

    request.onsuccess = () => {
      resolve()
    }

    request.onerror = () => {
      reject(request.error)
    }
  })
}

export async function clearAnomalies(): Promise<void> {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(STORE_NAME, 'readwrite')
    const store = transaction.objectStore(STORE_NAME)
    const request = store.clear()

    request.onsuccess = () => {
      resolve()
    }

    request.onerror = () => {
      reject(request.error)
    }
  })
}

export function closeDB(): void {
  if (dbInstance) {
    dbInstance.close()
    dbInstance = null
  }
}
