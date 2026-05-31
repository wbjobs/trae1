const DB_NAME = 'distributed-compute';
const DB_VERSION = 1;
const STORE = 'checkpoints';

function openDB() {
  return new Promise((resolve, reject) => {
    if (typeof indexedDB === 'undefined') {
      reject(new Error('IndexedDB not available'));
      return;
    }
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) {
        const store = db.createObjectStore(STORE, { keyPath: 'key' });
        store.createIndex('workerId', 'workerId', { unique: false });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function tx(db, mode) {
  return db.transaction(STORE, mode).objectStore(STORE);
}

export async function saveCheckpoint(key, data) {
  try {
    const db = await openDB();
    const store = tx(db, 'readwrite');
    store.put({ key, updatedAt: Date.now(), ...data });
  } catch (e) {
    console.warn('idb save failed', e);
  }
}

export async function loadCheckpoint(key) {
  return new Promise((resolve) => {
    openDB()
      .then((db) => {
        const req = tx(db, 'readonly').get(key);
        req.onsuccess = () => resolve(req.result || null);
        req.onerror = () => resolve(null);
      })
      .catch(() => resolve(null));
  });
}

export async function deleteCheckpoint(key) {
  try {
    const db = await openDB();
    tx(db, 'readwrite').delete(key);
  } catch (e) {
    console.warn('idb delete failed', e);
  }
}

export async function loadWorkerCheckpoints(workerId) {
  return new Promise((resolve) => {
    openDB()
      .then((db) => {
        const store = tx(db, 'readonly');
        const index = store.index('workerId');
        const req = index.getAll(workerId);
        req.onsuccess = () => resolve(req.result || []);
        req.onerror = () => resolve([]);
      })
      .catch(() => resolve([]));
  });
}

export function ckptKey(workerId, chunkId) {
  return `${workerId}:${chunkId}`;
}
