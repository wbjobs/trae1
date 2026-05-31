const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');
const crypto = require('crypto');
const os = require('os');
const path = require('path');

const WORKER_COUNT = Math.min(os.cpus().length, 4);

class CryptoWorkerPool {
  constructor() {
    this.workers = [];
    this.taskQueue = [];
    this.busyWorkers = new Set();
    this.workerFilePath = __filename;
  }

  init() {
    if (!isMainThread) return;
    
    for (let i = 0; i < WORKER_COUNT; i++) {
      const worker = new Worker(this.workerFilePath, {
        workerData: { workerId: i }
      });
      
      worker.on('message', (message) => {
        this.busyWorkers.delete(worker);
        this.processTask(message);
      });
      
      worker.on('error', (error) => {
        console.error(`Worker ${i} error:`, error);
        this.busyWorkers.delete(worker);
      });
      
      this.workers.push(worker);
    }
    
    console.log(`[CryptoWorkerPool] Initialized ${WORKER_COUNT} workers`);
  }

  processTask(result) {
    if (this.taskQueue.length > 0) {
      const nextTask = this.taskQueue.shift();
      this.executeTask(nextTask.worker, nextTask.task, nextTask.resolve, nextTask.reject);
    }
  }

  executeTask(worker, task, resolve, reject) {
    this.busyWorkers.add(worker);
    
    const onMessage = (message) => {
      worker.removeListener('message', onMessage);
      worker.removeListener('error', onError);
      
      if (message.error) {
        reject(new Error(message.error));
      } else {
        resolve(message.result);
      }
    };
    
    const onError = (error) => {
      worker.removeListener('message', onMessage);
      worker.removeListener('error', onError);
      reject(error);
    };
    
    worker.once('message', onMessage);
    worker.once('error', onError);
    worker.postMessage(task);
  }

  encrypt(buffer, key, iv, algorithm = 'aes-256-cbc') {
    return new Promise((resolve, reject) => {
      const idleWorker = this.workers.find(w => !this.busyWorkers.has(w));
      
      const task = {
        type: 'encrypt',
        buffer: buffer.toString('base64'),
        key,
        iv,
        algorithm
      };
      
      if (idleWorker) {
        this.executeTask(idleWorker, task, resolve, reject);
      } else {
        const worker = this.workers[this.taskQueue.length % this.workers.length];
        this.taskQueue.push({ worker, task, resolve, reject });
      }
    });
  }

  decrypt(buffer, key, iv, algorithm = 'aes-256-cbc') {
    return new Promise((resolve, reject) => {
      const idleWorker = this.workers.find(w => !this.busyWorkers.has(w));
      
      const task = {
        type: 'decrypt',
        buffer: buffer.toString('base64'),
        key,
        iv,
        algorithm
      };
      
      if (idleWorker) {
        this.executeTask(idleWorker, task, resolve, reject);
      } else {
        const worker = this.workers[this.taskQueue.length % this.workers.length];
        this.taskQueue.push({ worker, task, resolve, reject });
      }
    });
  }

  destroy() {
    this.workers.forEach(worker => {
      try {
        worker.terminate();
      } catch (e) {}
    });
    this.workers = [];
    this.taskQueue = [];
    this.busyWorkers.clear();
  }
}

const workerPool = new CryptoWorkerPool();

if (isMainThread) {
  module.exports = workerPool;
} else {
  parentPort.on('message', (task) => {
    try {
      const { type, buffer, key, iv, algorithm } = task;
      const keyBuffer = Buffer.from(key, 'hex');
      const ivBuffer = Buffer.from(iv, 'hex');
      const dataBuffer = Buffer.from(buffer, 'base64');
      
      let result;
      
      if (type === 'encrypt') {
        const cipher = crypto.createCipheriv(algorithm, keyBuffer, ivBuffer);
        result = Buffer.concat([cipher.update(dataBuffer), cipher.final()]).toString('base64');
      } else if (type === 'decrypt') {
        const decipher = crypto.createDecipheriv(algorithm, keyBuffer, ivBuffer);
        result = Buffer.concat([decipher.update(dataBuffer), decipher.final()]).toString('base64');
      }
      
      parentPort.postMessage({ result });
    } catch (error) {
      parentPort.postMessage({ error: error.message });
    }
  });
}
