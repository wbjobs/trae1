import { randomId } from './utils.js';

export const TASK_TYPES = {
  FIBONACCI: 'fibonacci',
  MATRIX: 'matrix',
  PRIMES: 'primes',
};

export function generateTask(type, params) {
  const id = randomId(10);
  const created = Date.now();

  switch (type) {
    case TASK_TYPES.FIBONACCI: {
      const n = Math.max(1, parseInt(params.n, 10) || 100);
      const chunkSize = Math.max(1, parseInt(params.chunkSize, 10) || 20);
      const chunks = [];
      for (let start = 0; start < n; start += chunkSize) {
        chunks.push({
          id: `${id}-${start}`,
          start,
          end: Math.min(start + chunkSize, n),
        });
      }
      return {
        id,
        type,
        description: `Compute Fibonacci numbers [0, ${n})`,
        params: { n },
        chunks,
        created,
      };
    }

    case TASK_TYPES.MATRIX: {
      const n = Math.max(1, parseInt(params.n, 10) || 256);
      const A = randomMatrix(n, n);
      const B = randomMatrix(n, n);
      const chunkSize = Math.max(1, parseInt(params.chunkSize, 10) || 32);
      const chunks = [];
      for (let rowStart = 0; rowStart < n; rowStart += chunkSize) {
        chunks.push({
          id: `${id}-r${rowStart}`,
          rowStart,
          rowEnd: Math.min(rowStart + chunkSize, n),
          n,
          A: A.slice(rowStart, Math.min(rowStart + chunkSize, n)).map((r) => r.slice()),
          B,
        });
      }
      return {
        id,
        type,
        description: `Multiply ${n}x${n} matrices A * B`,
        params: { n },
        chunks,
        created,
      };
    }

    case TASK_TYPES.PRIMES: {
      const n = Math.max(2, parseInt(params.n, 10) || 1000000);
      const workers = Math.max(1, parseInt(params.workers, 10) || 4);
      const chunkSize = Math.ceil(n / workers);
      const chunks = [];
      for (let i = 0; i < workers; i++) {
        const start = i * chunkSize + 1;
        const end = Math.min((i + 1) * chunkSize, n);
        if (start > n) break;
        chunks.push({ id: `${id}-${i}`, start, end });
      }
      return {
        id,
        type,
        description: `Find all primes ≤ ${n}`,
        params: { n, workers },
        chunks,
        created,
      };
    }

    default:
      throw new Error(`Unknown task type: ${type}`);
  }
}

function randomMatrix(rows, cols) {
  const m = new Array(rows);
  for (let i = 0; i < rows; i++) {
    const row = new Array(cols);
    for (let j = 0; j < cols; j++) row[j] = Math.random();
    m[i] = row;
  }
  return m;
}

export function executeChunk(type, chunk) {
  const t0 = performance.now();
  let result;
  switch (type) {
    case TASK_TYPES.FIBONACCI: {
      result = [];
      for (let i = chunk.start; i < chunk.end; i++) {
        result.push(fib(i));
      }
      break;
    }
    case TASK_TYPES.MATRIX: {
      const n = chunk.n;
      const A = chunk.A;
      const B = chunk.B;
      const rows = A.length;
      const C = new Array(rows);
      for (let i = 0; i < rows; i++) {
        const row = new Array(n);
        for (let j = 0; j < n; j++) {
          let s = 0;
          for (let k = 0; k < n; k++) s += A[i][k] * B[k][j];
          row[j] = s;
        }
        C[i] = row;
      }
      result = { rowStart: chunk.rowStart, rowEnd: chunk.rowEnd, rows: C };
      break;
    }
    case TASK_TYPES.PRIMES: {
      result = [];
      for (let x = chunk.start; x <= chunk.end; x++) {
        if (isPrime(x)) result.push(x);
      }
      break;
    }
    default:
      throw new Error(`Unknown task type: ${type}`);
  }
  return { result, duration: performance.now() - t0 };
}

function fib(n) {
  if (n < 2) return n;
  let a = 0, b = 1;
  for (let i = 2; i <= n; i++) {
    const c = a + b;
    a = b;
    b = c;
  }
  return b;
}

function isPrime(n) {
  if (n < 2) return false;
  if (n < 4) return true;
  if (n % 2 === 0) return false;
  for (let i = 3; i * i <= n; i += 2) {
    if (n % i === 0) return false;
  }
  return true;
}

export function createStepExecutor(type, chunk, checkpoint) {
  let state;
  switch (type) {
    case TASK_TYPES.FIBONACCI: {
      if (checkpoint) {
        state = {
          nextIndex: checkpoint.nextIndex,
          a: checkpoint.a,
          b: checkpoint.b,
          collected: checkpoint.collected || [],
          total: chunk.end - chunk.start,
          chunk,
        };
      } else {
        const start = chunk.start;
        let a, b;
        if (start === 0) { a = 0; b = 0; }
        else if (start === 1) { a = 0; b = 1; }
        else {
          a = fib(start - 2);
          b = fib(start - 1);
        }
        state = {
          nextIndex: start,
          a,
          b,
          collected: [],
          total: chunk.end - chunk.start,
          chunk,
        };
      }
      break;
    }
    case TASK_TYPES.PRIMES: {
      state = {
        lastChecked: checkpoint ? checkpoint.lastChecked : chunk.start - 1,
        collected: checkpoint ? [...checkpoint.collected] : [],
        total: chunk.end - chunk.start + 1,
        chunk,
      };
      break;
    }
    case TASK_TYPES.MATRIX: {
      if (checkpoint) {
        state = {
          nextRow: checkpoint.nextRow,
          rows: checkpoint.rows || [],
          total: chunk.rowEnd - chunk.rowStart,
          chunk,
          n: chunk.n,
          A: chunk.A,
          B: chunk.B,
        };
      } else {
        state = {
          nextRow: chunk.rowStart,
          rows: [],
          total: chunk.rowEnd - chunk.rowStart,
          chunk,
          n: chunk.n,
          A: chunk.A,
          B: chunk.B,
        };
      }
      break;
    }
    default:
      throw new Error(`Unknown task type: ${type}`);
  }

  const batchSizes = {
    [TASK_TYPES.FIBONACCI]: 500,
    [TASK_TYPES.PRIMES]: 5000,
    [TASK_TYPES.MATRIX]: 4,
  };
  const batchSize = batchSizes[type] || 100;

  function progressPct() {
    switch (type) {
      case TASK_TYPES.FIBONACCI: {
        const done = state.collected.length;
        return Math.min(100, Math.round((done / state.total) * 100));
      }
      case TASK_TYPES.PRIMES: {
        const done = state.lastChecked - state.chunk.start + 1;
        return Math.min(100, Math.round((done / state.total) * 100));
      }
      case TASK_TYPES.MATRIX: {
        const done = state.nextRow - state.chunk.rowStart;
        return Math.min(100, Math.round((done / state.total) * 100));
      }
    }
  }

  function isDone() {
    switch (type) {
      case TASK_TYPES.FIBONACCI: return state.nextIndex >= state.chunk.end;
      case TASK_TYPES.PRIMES: return state.lastChecked >= state.chunk.end;
      case TASK_TYPES.MATRIX: return state.nextRow >= state.chunk.rowEnd;
    }
  }

  function step() {
    switch (type) {
      case TASK_TYPES.FIBONACCI: {
        const limit = Math.min(state.nextIndex + batchSize, state.chunk.end);
        for (let i = state.nextIndex; i < limit; i++) {
          if (i === 0) { state.collected.push(0); }
          else if (i === 1) { state.collected.push(1); }
          else {
            const c = state.a + state.b;
            state.a = state.b;
            state.b = c;
            state.collected.push(c);
          }
        }
        state.nextIndex = limit;
        break;
      }
      case TASK_TYPES.PRIMES: {
        const limit = Math.min(state.lastChecked + batchSize, state.chunk.end);
        for (let x = state.lastChecked + 1; x <= limit; x++) {
          if (isPrime(x)) state.collected.push(x);
        }
        state.lastChecked = limit;
        break;
      }
      case TASK_TYPES.MATRIX: {
        const limit = Math.min(state.nextRow + batchSize, state.chunk.rowEnd);
        const n = state.n;
        const A = state.A;
        const B = state.B;
        for (let r = state.nextRow; r < limit; r++) {
          const localI = r - state.chunk.rowStart;
          const row = new Array(n);
          for (let j = 0; j < n; j++) {
            let s = 0;
            for (let k = 0; k < n; k++) s += A[localI][k] * B[k][j];
            row[j] = s;
          }
          state.rows.push(row);
        }
        state.nextRow = limit;
        break;
      }
    }
    return {
      progress: progressPct(),
      partialResult: getPartialResult(),
      done: isDone(),
    };
  }

  function getPartialResult() {
    switch (type) {
      case TASK_TYPES.FIBONACCI:
        return { values: state.collected };
      case TASK_TYPES.PRIMES:
        return { primes: state.collected };
      case TASK_TYPES.MATRIX:
        return { rows: state.rows };
    }
  }

  function getCheckpoint() {
    switch (type) {
      case TASK_TYPES.FIBONACCI:
        return {
          type,
          nextIndex: state.nextIndex,
          a: state.a,
          b: state.b,
          collected: state.collected,
        };
      case TASK_TYPES.PRIMES:
        return {
          type,
          lastChecked: state.lastChecked,
          collected: state.collected,
        };
      case TASK_TYPES.MATRIX:
        return {
          type,
          nextRow: state.nextRow,
          rows: state.rows,
        };
    }
  }

  function getResult() {
    switch (type) {
      case TASK_TYPES.FIBONACCI:
        return state.collected;
      case TASK_TYPES.PRIMES:
        return state.collected;
      case TASK_TYPES.MATRIX:
        return {
          rowStart: state.chunk.rowStart,
          rowEnd: state.chunk.rowEnd,
          rows: state.rows,
        };
    }
  }

  return { step, getCheckpoint, getResult, getPartialResult };
}

export function aggregateResults(task, results) {
  switch (task.type) {
    case TASK_TYPES.FIBONACCI: {
      const n = task.params.n;
      const arr = new Array(n).fill(0);
      for (const res of results) {
        const chunk = res.chunk;
        const vals = res.result;
        for (let i = 0; i < vals.length; i++) {
          arr[chunk.start + i] = vals[i];
        }
      }
      return { values: arr };
    }
    case TASK_TYPES.MATRIX: {
      const n = task.params.n;
      const C = new Array(n);
      for (let i = 0; i < n; i++) C[i] = new Array(n).fill(0);
      for (const res of results) {
        const r = res.result;
        for (let i = 0; i < r.rows.length; i++) {
          C[r.rowStart + i] = r.rows[i];
        }
      }
      return { rows: C };
    }
    case TASK_TYPES.PRIMES: {
      const all = [];
      for (const res of results) {
        for (const p of res.result) all.push(p);
      }
      all.sort((a, b) => a - b);
      return { primes: all, count: all.length };
    }
    default:
      return { raw: results };
  }
}

export function summarizeResult(task, aggregated) {
  switch (task.type) {
    case TASK_TYPES.FIBONACCI: {
      const vals = aggregated.values;
      const preview = vals.slice(0, 20);
      return `Fibonacci [0..${vals.length}): ${preview.join(', ')}${vals.length > 20 ? '...' : ''}`;
    }
    case TASK_TYPES.MATRIX: {
      const C = aggregated.rows;
      const preview = C.slice(0, 3).map((r) => r.slice(0, 3).map((v) => v.toFixed(3)).join(', ')).join('\n');
      return `Matrix ${C.length}x${C.length} (top-left 3x3):\n${preview}`;
    }
    case TASK_TYPES.PRIMES: {
      const { primes, count } = aggregated;
      const preview = primes.slice(0, 20).join(', ');
      return `Found ${count} primes:\n${preview}${primes.length > 20 ? '...' : ''}`;
    }
    default:
      return JSON.stringify(aggregated);
  }
}
