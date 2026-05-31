import React, { useEffect, useRef, useState } from 'react';
import { WorkerNode } from '../utils/WorkerNode.js';
import { fmtMs, shortId } from '../utils/utils.js';
import LogConsole from './LogConsole.jsx';

export default function WorkerView({ serverUrl, nodeId, name }) {
  const [status, setStatus] = useState('disconnected');
  const [nodes, setNodes] = useState([]);
  const [masters, setMasters] = useState([]);
  const [currentTask, setCurrentTask] = useState(null);
  const [completed, setCompleted] = useState([]);
  const [logs, setLogs] = useState([]);
  const [, setTick] = useState(0);
  const nodeRef = useRef(null);

  const log = (msg, level = 'info') => {
    setLogs((prev) => [...prev, { msg, level, t: Date.now() }].slice(-100));
  };

  useEffect(() => {
    const node = new WorkerNode({ signalingUrl: serverUrl, id: nodeId, name });
    nodeRef.current = node;
    node.on('status', (s) => setStatus(s));
    node.on('nodes:update', (list) => setNodes(list));
    node.on('task:start', (t) => {
      setCurrentTask({ ...t, progress: 0 });
      log(`Task chunk ${shortId(t.chunk.id)} received (${t.taskType})${t.resumed ? ' — resuming from checkpoint' : ''}`, 'info');
    });
    node.on('task:progress', (t) => {
      setCurrentTask((prev) => prev ? { ...prev, progress: t.progress } : prev);
    });
    node.on('task:done', (t) => {
      setCurrentTask(null);
      setCompleted((prev) => [{ ...t, finishedAt: Date.now() }, ...prev].slice(0, 20));
      log(`Chunk ${shortId(t.chunk.id)} done in ${fmtMs(t.duration)}`, 'success');
    });
    node.on('task:error', (t) => {
      setCurrentTask(null);
      log(`Chunk ${shortId(t.chunk.id)} failed: ${t.error}`, 'error');
    });
    node.on('task:aborted', ({ taskId }) => {
      setCurrentTask(null);
      log(`Task ${shortId(taskId)} aborted`, 'info');
    });
    node.start();
    log('Worker node started — waiting for tasks', 'info');
    return () => node.stop();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [serverUrl, nodeId, name]);

  const masterNodes = nodes.filter((n) => n.role === 'master');

  useEffect(() => {
    if (!currentTask) return;
    const id = setInterval(() => setTick((t) => (t + 1) & 0xffff), 1000);
    return () => clearInterval(id);
  }, [currentTask]);

  return (
    <main>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
        <div className="panel">
          <h2>Worker Status</h2>
          <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
            <span className={`badge ${status === 'connected' ? 'worker' : ''}`}>{status}</span>
            <span className="badge">{masterNodes.length} master(s) online</span>
            <span style={{ color: '#a5b4ff', fontSize: 13 }}>ID: {shortId(nodeId)}</span>
          </div>
        </div>

        <div className="panel">
          <h2>Current Task</h2>
          {currentTask ? (
            <div>
              <div>
                <span className="badge worker">{currentTask.taskType}</span>{' '}
                chunk {shortId(currentTask.chunk.id)}
                {currentTask.resumed && <span className="badge" style={{ marginLeft: 6 }}>resumed</span>}
              </div>
              <div className="meta" style={{ marginTop: 6 }}>
                Running for {fmtMs(Date.now() - currentTask.startedAt)}
              </div>
              <div className="progress-bar" style={{ marginTop: 10 }}>
                <div style={{ width: `${currentTask.progress || 0}%` }} />
              </div>
              <div className="meta" style={{ marginTop: 4, fontSize: 12 }}>
                Progress: {currentTask.progress || 0}%
              </div>
            </div>
          ) : (
            <div className="meta">Idle — waiting for master to assign a chunk...</div>
          )}
        </div>

        <div className="panel">
          <h2>Recent Results</h2>
          {completed.length === 0 ? (
            <div className="meta">None yet.</div>
          ) : (
            <ul className="node-list">
              {completed.map((t) => (
                <li key={`${t.chunk.id}-${t.finishedAt}`} className="node-item done">
                  <div>
                    <div style={{ fontWeight: 600 }}>
                      <span className="badge worker">{t.taskType}</span>{' '}
                      chunk {shortId(t.chunk.id)}
                    </div>
                    <div className="meta">duration {fmtMs(t.duration)}</div>
                  </div>
                  <div className="status-dot" />
                </li>
              ))}
            </ul>
          )}
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
