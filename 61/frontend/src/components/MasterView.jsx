import React, { useEffect, useMemo, useRef, useState } from 'react';
import { MasterNode } from '../utils/MasterNode.js';
import { TASK_TYPES, generateTask, aggregateResults, summarizeResult } from '../utils/tasks.js';
import { fmtMs, shortId } from '../utils/utils.js';
import NodeList from './NodeList.jsx';
import LogConsole from './LogConsole.jsx';

const MAX_RETRIES = 3;

export default function MasterView({ serverUrl, nodeId, name }) {
  const [nodes, setNodes] = useState([]);
  const [status, setStatus] = useState('disconnected');
  const [peers, setPeers] = useState([]);

  const [taskType, setTaskType] = useState(TASK_TYPES.PRIMES);
  const [fibN, setFibN] = useState(40);
  const [fibChunk, setFibChunk] = useState(8);
  const [matrixN, setMatrixN] = useState(128);
  const [matrixChunk, setMatrixChunk] = useState(32);
  const [primesN, setPrimesN] = useState(200000);
  const [primesWorkers, setPrimesWorkers] = useState(4);

  const [task, setTask] = useState(null);
  const [chunkStates, setChunkStates] = useState({});
  const [results, setResults] = useState([]);
  const [finalResult, setFinalResult] = useState(null);
  const [startedAt, setStartedAt] = useState(null);
  const [finishedAt, setFinishedAt] = useState(null);
  const [logs, setLogs] = useState([]);
  const [failureReport, setFailureReport] = useState(null);
  const [, setTick] = useState(0);

  const nodeRef = useRef(null);
  const taskRef = useRef(null);
  const resultsRef = useRef([]);
  taskRef.current = task;
  resultsRef.current = results;

  const queueRef = useRef([]);
  const completedRef = useRef(new Set());
  const retryCountsRef = useRef({});
  const taskActiveRef = useRef(false);

  const log = (msg, level = 'info') => {
    setLogs((prev) => {
      const next = [...prev, { msg, level, t: Date.now() }];
      return next.slice(-100);
    });
  };

  useEffect(() => {
    const node = new MasterNode({ signalingUrl: serverUrl, id: nodeId, name });
    nodeRef.current = node;

    node.on('status', (s) => setStatus(s));
    node.on('nodes:update', (list) => setNodes(list));
    node.on('peer:state', () => setPeers(node.getWorkers()));
    node.on('chunk:progress', (payload) => {
      setChunkStates((prev) => ({
        ...prev,
        [payload.chunkId]: {
          ...(prev[payload.chunkId] || {}),
          status: 'busy',
          workerId: payload.workerId,
          progress: payload.progress,
        },
      }));
      setPeers(node.getWorkers());
    });

    node.on('chunk:resume', (payload) => {
      log(`Resumed chunk ${shortId(payload.chunkId)} on worker ${shortId(payload.workerId)} from checkpoint`, 'info');
      setChunkStates((prev) => ({
        ...prev,
        [payload.chunkId]: {
          ...(prev[payload.chunkId] || {}),
          status: 'busy',
          workerId: payload.workerId,
          resumed: true,
        },
      }));
      setPeers(node.getWorkers());
    });

    node.on('chunk:result', (payload) => {
      setChunkStates((prev) => ({
        ...prev,
        [payload.chunkId]: {
          status: 'done',
          workerId: payload.workerId,
          duration: payload.duration,
        },
      }));
      setResults((prev) => {
        const filtered = prev.filter((r) => r.chunk.id !== payload.chunkId);
        const chunk = taskRef.current?.chunks.find((c) => c.id === payload.chunkId);
        return [...filtered, {
          chunk,
          result: payload.result,
          duration: payload.duration,
          workerId: payload.workerId,
        }];
      });
      log(`Chunk ${shortId(payload.chunkId)} done by ${shortId(payload.workerId)} in ${fmtMs(payload.duration)}`, 'success');
      setPeers(node.getWorkers());

      if (taskActiveRef.current && taskRef.current) {
        completedRef.current.add(payload.chunkId);
        tryAssignNext(node, payload.workerId);
        checkTaskComplete(node);
      }
    });

    node.on('chunk:timeout', (payload) => {
      const { chunkId, workerId, reason, error } = payload;
      const retryCount = (retryCountsRef.current[chunkId] || 0) + 1;
      retryCountsRef.current[chunkId] = retryCount;

      log(
        `Chunk ${shortId(chunkId)} ${reason} on worker ${shortId(workerId)} (retry ${retryCount}/${MAX_RETRIES})${error ? ': ' + error : ''}`,
        'error'
      );

      setChunkStates((prev) => ({
        ...prev,
        [chunkId]: {
          status: 'pending',
          workerId: null,
          retryCount,
          lastFailure: { workerId, reason, error },
        },
      }));

      if (taskActiveRef.current && taskRef.current) {
        if (retryCount > MAX_RETRIES) {
          log(`Chunk ${shortId(chunkId)} exceeded max retries (${MAX_RETRIES}), marking as failed`, 'error');
          completedRef.current.add(chunkId);
          setChunkStates((prev) => ({
            ...prev,
            [chunkId]: {
              ...prev[chunkId],
              status: 'error',
            },
          }));
          checkTaskComplete(node);
        } else {
          const chunk = taskRef.current.chunks.find((c) => c.id === chunkId);
          if (chunk) {
            queueRef.current.push(chunk);
            setPeers(node.getWorkers());
            flushQueue(node);
          }
        }
      }
    });

    node.start();
    log('Master node started', 'info');

    return () => {
      node.stop();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [serverUrl, nodeId, name]);

  function tryAssignNext(node, workerId) {
    if (!node.isWorkerIdle(workerId)) return;
    if (queueRef.current.length === 0) return;
    const chunk = queueRef.current.shift();
    setChunkStates((prev) => ({
      ...prev,
      [chunk.id]: {
        status: 'busy',
        workerId,
        retryCount: retryCountsRef.current[chunk.id] || 0,
      },
    }));
    try {
      node.sendTaskTo(workerId, { task: taskRef.current, chunk });
      log(`Assigned chunk ${shortId(chunk.id)} to ${shortId(workerId)}`, 'info');
    } catch (e) {
      log(`Failed to assign chunk ${shortId(chunk.id)}: ${e.message}`, 'error');
      queueRef.current.unshift(chunk);
      setChunkStates((prev) => ({
        ...prev,
        [chunk.id]: { status: 'pending', retryCount: retryCountsRef.current[chunk.id] || 0 },
      }));
    }
  }

  function flushQueue(node) {
    const idleWorkers = node.getWorkers().filter((w) => node.isWorkerIdle(w.id));
    for (const w of idleWorkers) {
      if (queueRef.current.length === 0) break;
      tryAssignNext(node, w.id);
    }
  }

  function checkTaskComplete(node) {
    const t = taskRef.current;
    if (!t) return;
    const total = t.chunks.length;
    const done = completedRef.current.size;
    const pending = queueRef.current.length;
    const busyWorkers = node.getWorkers().filter((w) => w.status === 'busy').length;

    if (done >= total || (pending === 0 && busyWorkers === 0 && done > 0)) {
      taskActiveRef.current = false;
      const allResults = resultsRef.current;
      const successfulIds = new Set(allResults.filter((r) => !r.error).map((r) => r.chunk.id));
      const aggregated = aggregateResults(t, allResults.filter((r) => !r.error));
      setFinalResult(aggregated);
      setFinishedAt(Date.now());

      const failures = node.getFailures();
      const failedChunkIds = Object.keys(retryCountsRef.current).filter(
        (id) => (retryCountsRef.current[id] || 0) > 0
      );
      const report = {
        totalChunks: total,
        completedChunks: completedRef.current.size,
        failedChunks: failedChunkIds.map((id) => ({
          chunkId: id,
          retries: retryCountsRef.current[id],
          lastState: successfulIds.has(id) ? 'done' : 'error',
        })),
        failedNodes: failures,
        totalRetries: Object.values(retryCountsRef.current).reduce((a, b) => a + b, 0),
      };
      setFailureReport(report);
      log(`Task ${shortId(t.id)} finished! Completed: ${report.completedChunks}/${total}`, 'success');
    }
  }

  const completedCount = useMemo(() =>
    Object.values(chunkStates).filter((s) => s.status === 'done').length,
    [chunkStates]
  );
  const totalChunks = task?.chunks.length || 0;
  const progress = totalChunks ? Math.round((completedCount / totalChunks) * 100) : 0;

  const allWorkers = nodes.filter((n) => n.role === 'worker');
  const connectedWorkers = peers.filter((p) => p.channelState === 'open');

  const connectAllWorkers = async () => {
    const node = nodeRef.current;
    if (!node) return;
    for (const w of allWorkers) {
      if (!node.hasWorker(w.id)) {
        log(`Connecting to worker ${w.name || shortId(w.id)}...`, 'info');
        try {
          await node.connectToWorker(w.id, w.name);
        } catch (e) {
          log(`Failed to connect: ${e.message}`, 'error');
        }
      }
    }
    setTimeout(() => setPeers(node.getWorkers()), 500);
  };

  const runTask = async () => {
    const node = nodeRef.current;
    if (!node) return;
    const idleWorkers = node.getWorkers().filter((w) => node.isWorkerIdle(w.id));
    if (idleWorkers.length === 0) {
      log('No idle workers. Connect to some workers first.', 'error');
      return;
    }
    let t;
    try {
      t = generateTask(taskType, taskType === TASK_TYPES.FIBONACCI
        ? { n: fibN, chunkSize: fibChunk }
        : taskType === TASK_TYPES.MATRIX
          ? { n: matrixN, chunkSize: matrixChunk }
          : { n: primesN, workers: primesWorkers });
    } catch (e) {
      log(`Task creation failed: ${e.message}`, 'error');
      return;
    }

    node.resetFailures();
    setTask(t);
    taskRef.current = t;
    setResults([]);
    resultsRef.current = [];
    setFinalResult(null);
    setFinishedAt(null);
    setFailureReport(null);

    queueRef.current = [...t.chunks];
    completedRef.current = new Set();
    retryCountsRef.current = {};
    taskActiveRef.current = true;

    const initialStates = {};
    for (const c of t.chunks) initialStates[c.id] = { status: 'pending', retryCount: 0 };
    setChunkStates(initialStates);

    setStartedAt(Date.now());
    log(`Task ${t.id} started: ${t.description} (${t.chunks.length} chunks)`, 'info');

    flushQueue(node);
    setPeers(node.getWorkers());
  };

  const stopTask = () => {
    taskActiveRef.current = false;
    queueRef.current = [];
    completedRef.current = new Set();
    retryCountsRef.current = {};
    setTask(null);
    taskRef.current = null;
    setChunkStates({});
    setResults([]);
    resultsRef.current = [];
    setFinalResult(null);
    setStartedAt(null);
    setFinishedAt(null);
    setFailureReport(null);
    log('Task aborted', 'info');
  };

  useEffect(() => {
    if (!task || finishedAt) return;
    const id = setInterval(() => setTick((t) => (t + 1) & 0xffff), 1000);
    return () => clearInterval(id);
  }, [task, finishedAt]);

  return (
    <main>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
        <div className="panel">
          <h2>Control Panel</h2>
          <div className="task-form">
            <div className="row">
              <label>Status</label>
              <span className={`badge ${status === 'connected' ? 'master' : ''}`}>{status}</span>
              <label style={{ marginLeft: 20 }}>Workers</label>
              <span className="badge">{allWorkers.length} online / {connectedWorkers.length} connected</span>
            </div>
            <div className="row">
              <label>Task Type</label>
              <select value={taskType} onChange={(e) => setTaskType(e.target.value)}>
                <option value={TASK_TYPES.PRIMES}>Prime Sieve</option>
                <option value={TASK_TYPES.FIBONACCI}>Fibonacci</option>
                <option value={TASK_TYPES.MATRIX}>Matrix Multiplication</option>
              </select>
              <button onClick={connectAllWorkers}>Connect All Workers</button>
              {!task ? (
                <button onClick={runTask}>▶ Run Task</button>
              ) : (
                <button className="danger" onClick={stopTask}>■ Abort</button>
              )}
            </div>
            {taskType === TASK_TYPES.PRIMES && (
              <>
                <div className="row">
                  <label>N (max prime)</label>
                  <input type="number" value={primesN} onChange={(e) => setPrimesN(+e.target.value)} min={2} />
                  <label>Workers</label>
                  <input type="number" value={primesWorkers} onChange={(e) => setPrimesWorkers(+e.target.value)} min={1} />
                </div>
              </>
            )}
            {taskType === TASK_TYPES.FIBONACCI && (
              <>
                <div className="row">
                  <label>N (count)</label>
                  <input type="number" value={fibN} onChange={(e) => setFibN(+e.target.value)} min={1} />
                  <label>Chunk Size</label>
                  <input type="number" value={fibChunk} onChange={(e) => setFibChunk(+e.target.value)} min={1} />
                </div>
              </>
            )}
            {taskType === TASK_TYPES.MATRIX && (
              <>
                <div className="row">
                  <label>Matrix Size N</label>
                  <input type="number" value={matrixN} onChange={(e) => setMatrixN(+e.target.value)} min={1} />
                  <label>Rows per Chunk</label>
                  <input type="number" value={matrixChunk} onChange={(e) => setMatrixChunk(+e.target.value)} min={1} />
                </div>
              </>
            )}
          </div>
        </div>

        {task && (
          <div className="panel">
            <h2>Task Progress · {shortId(task.id)}</h2>
            <div style={{ fontSize: 13, color: '#a5b4ff' }}>{task.description}</div>
            <div className="progress-bar">
              <div style={{ width: `${progress}%` }} />
            </div>
            <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, color: '#a5b4ff' }}>
              <span>Chunks: {completedCount} / {totalChunks}</span>
              <span>Elapsed: {fmtMs(startedAt ? Date.now() - startedAt : 0)}</span>
              {finishedAt && <span>Total: {fmtMs(finishedAt - startedAt)}</span>}
            </div>
          </div>
        )}

        {task && (
          <div className="panel">
            <h2>Active Chunks</h2>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(180px, 1fr))', gap: 8 }}>
              {task.chunks.map((c) => {
                const s = chunkStates[c.id] || {};
                const pct = typeof s.progress === 'number' ? s.progress : (s.status === 'done' ? 100 : 0);
                const cls = s.status === 'done' ? 'done' : s.status === 'error' ? 'error' : s.status === 'busy' ? 'busy' : 'idle';
                return (
                  <div key={c.id} className={`node-item ${cls}`} style={{ flexDirection: 'column', alignItems: 'stretch', padding: '8px 10px' }}>
                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                      <span style={{ fontSize: 12, fontWeight: 600 }}>{shortId(c.id)}</span>
                      {s.resumed && <span className="badge">resumed</span>}
                    </div>
                    <div className="progress-bar" style={{ height: 6, margin: '6px 0' }}>
                      <div style={{ width: `${pct}%` }} />
                    </div>
                    <div style={{ fontSize: 11, color: '#8a93c8' }}>
                      {s.status === 'done' ? 'done' : s.status === 'error' ? 'failed' : `${pct}%`}
                      {s.workerId && ` · ${shortId(s.workerId)}`}
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {finalResult && (
          <div className="panel">
            <h2>Result</h2>
            <pre className="result-box">{summarizeResult(task, finalResult)}</pre>
          </div>
        )}

        {failureReport && (
          <div className="panel">
            <h2>Fault Report</h2>
            <div style={{ fontSize: 13, lineHeight: 1.7 }}>
              <div>
                <strong>Total chunks:</strong> {failureReport.totalChunks} ·{' '}
                <strong>Completed:</strong> {failureReport.completedChunks} ·{' '}
                <strong>Total retries:</strong> {failureReport.totalRetries}
              </div>
              {failureReport.failedNodes.length > 0 ? (
                <div>
                  <div style={{ marginTop: 8, fontWeight: 600, color: '#a5b4ff' }}>Failed Nodes</div>
                  <ul className="node-list" style={{ marginTop: 6 }}>
                    {failureReport.failedNodes.map((f) => (
                      <li key={f.workerId} className="node-item error">
                        <div>
                          <div style={{ fontWeight: 600 }}>
                            Worker {shortId(f.workerId)}
                          </div>
                          <div className="meta">
                            timeouts: {f.timeouts} · disconnects: {f.disconnects} · errors: {f.errors}
                            {f.lastReason && ` · last: ${f.lastReason}`}
                          </div>
                        </div>
                        <div className="status-dot" />
                      </li>
                    ))}
                  </ul>
                </div>
              ) : (
                <div className="meta" style={{ marginTop: 8 }}>No node failures.</div>
              )}
              {failureReport.failedChunks.length > 0 && (
                <div style={{ marginTop: 8 }}>
                  <div style={{ fontWeight: 600, color: '#a5b4ff' }}>Retried Chunks</div>
                  <div className="meta">
                    {failureReport.failedChunks.map((c) => (
                      <span key={c.chunkId} style={{ marginRight: 8 }}>
                        {shortId(c.chunkId)} (×{c.retries}) [{c.lastState}]
                      </span>
                    ))}
                  </div>
                </div>
              )}
            </div>
          </div>
        )}

        <div className="panel">
          <h2>Worker Peers</h2>
          <NodeList peers={peers} chunkStates={chunkStates} />
        </div>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
        <div className="panel">
          <h2>Network Nodes</h2>
          <ul className="node-list">
            {nodes.map((n) => (
              <li key={n.id} className="node-item idle">
                <div>
                  <div style={{ fontWeight: 600 }}>{n.name}</div>
                  <div className="meta">
                    <span className={`badge ${n.role}`}>{n.role}</span>{' '}
                    {shortId(n.id)}
                  </div>
                </div>
                <div className="status-dot" />
              </li>
            ))}
            {nodes.length === 0 && <div className="meta">No nodes registered.</div>}
          </ul>
        </div>

        <div className="panel">
          <h2>Logs</h2>
          <LogConsole logs={logs} />
        </div>
      </div>
    </main>
  );
}
