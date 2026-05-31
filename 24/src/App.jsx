import React, { useState, useCallback, useEffect, useRef } from 'react';
import SpectrumVisualizer from './SpectrumVisualizer.jsx';
import { useAudioRecorder } from './useAudioRecorder.js';
import FingerprintPanel from './FingerprintPanel.jsx';
import { FingerprintGenerator } from './fingerprint.js';

const FFT_SIZES = [256, 512, 1024];
const WS_URL = 'ws://localhost:8080/ws';
const WS_RECONNECT_INTERVAL = 3000;
const WS_RECONNECT_ATTEMPTS = 10;
const API_BASE = 'http://localhost:8080';

function App() {
  const [fftSize, setFftSize] = useState(512);
  const [spectrumData, setSpectrumData] = useState(null);
  const [wsConnected, setWsConnected] = useState(false);
  const [clientId] = useState(() => 'client_' + Math.random().toString(36).substring(2, 9));
  
  const [isFingerprinting, setIsFingerprinting] = useState(false);
  const [currentFingerprints, setCurrentFingerprints] = useState([]);
  const [storedClips, setStoredClips] = useState([]);
  const [matchResults, setMatchResults] = useState(null);
  const [matchStatus, setMatchStatus] = useState('idle');
  
  const wsRef = useRef(null);
  const spectrumRef = useRef(null);
  const reconnectAttemptsRef = useRef(0);
  const reconnectTimeoutRef = useRef(null);
  const isReconnectingRef = useRef(false);
  const fingerprintGenRef = useRef(null);
  const fingerprintTimeRef = useRef(0);

  const {
    isRecording,
    error,
    currentFFTSize,
    startRecording,
    stopRecording,
    setFFTSize,
    onSpectrumData
  } = useAudioRecorder(fftSize);

  useEffect(() => {
    fingerprintGenRef.current = new FingerprintGenerator({
      fftSize: 1024,
      samplingRate: 48000,
      minPeakDistance: 3,
      minPeakThreshold: 0.3,
      maxPeaks: 30,
      fanOut: 10
    });
  }, []);

  useEffect(() => {
    onSpectrumData((spectrum) => {
      if (!spectrum || spectrum.length === 0) return;
      
      try {
        const safeSpectrum = validateAndCopySpectrum(spectrum);
        if (safeSpectrum) {
          spectrumRef.current = safeSpectrum;
          setSpectrumData(safeSpectrum);

          if (isFingerprinting && fingerprintGenRef.current) {
            fingerprintTimeRef.current += 1;
            const result = fingerprintGenRef.current.processSpectrum(
              safeSpectrum,
              fingerprintTimeRef.current
            );

            if (result.hashes.length > 0) {
              setCurrentFingerprints(prev => {
                const updated = [...prev, ...result.hashes.slice(0, 5)];
                return updated.slice(-500);
              });
            }
          }
        }
      } catch (err) {
        console.error('[App] Spectrum processing error:', err);
      }
    });
  }, [onSpectrumData, isFingerprinting]);

  const connectWebSocket = useCallback(() => {
    if (isReconnectingRef.current) return;
    
    if (reconnectAttemptsRef.current >= WS_RECONNECT_ATTEMPTS) {
      console.warn('[WebSocket] Max reconnection attempts reached');
      return;
    }

    isReconnectingRef.current = true;

    try {
      const ws = new WebSocket(WS_URL);
      
      ws.onopen = () => {
        console.log('[WebSocket] Connected');
        setWsConnected(true);
        reconnectAttemptsRef.current = 0;
        isReconnectingRef.current = false;
        
        ws.send(JSON.stringify({
          type: 'register',
          clientId,
          role: 'client'
        }));
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          
          if (data.type === 'broadcast' && data.spectrum && Array.isArray(data.spectrum)) {
            const validSpectrum = validateReceivedSpectrum(data.spectrum);
            if (validSpectrum) {
              spectrumRef.current = validSpectrum;
              setSpectrumData(validSpectrum);
            }
          } else if (data.type === 'match_result') {
            setMatchResults(data);
            setMatchStatus('completed');
          } else if (data.type === 'recording_stopped') {
            fetchClips();
          }
        } catch (err) {
          console.error('[WebSocket] Parse error:', err);
        }
      };

      ws.onclose = () => {
        console.log('[WebSocket] Disconnected');
        setWsConnected(false);
        isReconnectingRef.current = false;
        
        scheduleReconnect();
      };

      ws.onerror = (err) => {
        console.error('[WebSocket] Error:', err);
        setWsConnected(false);
        isReconnectingRef.current = false;
      };

      wsRef.current = ws;
    } catch (err) {
      console.error('[WebSocket] Connection failed:', err);
      isReconnectingRef.current = false;
      scheduleReconnect();
    }
  }, [clientId]);

  const scheduleReconnect = useCallback(() => {
    if (reconnectTimeoutRef.current) {
      clearTimeout(reconnectTimeoutRef.current);
    }
    
    reconnectAttemptsRef.current++;
    
    if (reconnectAttemptsRef.current < WS_RECONNECT_ATTEMPTS) {
      console.log(`[WebSocket] Reconnecting (attempt ${reconnectAttemptsRef.current}/${WS_RECONNECT_ATTEMPTS})...`);
      reconnectTimeoutRef.current = setTimeout(connectWebSocket, WS_RECONNECT_INTERVAL);
    }
  }, [connectWebSocket]);

  useEffect(() => {
    connectWebSocket();
    fetchClips();

    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, [connectWebSocket]);

  useEffect(() => {
    if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN) return;
    if (!spectrumRef.current) return;

    const sendInterval = setInterval(() => {
      if (wsRef.current.readyState === WebSocket.OPEN && spectrumRef.current) {
        try {
          const spectrumArray = Array.from(spectrumRef.current);
          const payload = JSON.stringify({
            type: 'spectrum',
            clientId,
            fftSize: currentFFTSize,
            spectrum: spectrumArray
          });
          
          if (payload.length > 1024 * 1024) {
            console.warn('[WebSocket] Payload too large, skipping');
            return;
          }
          
          wsRef.current.send(payload);
        } catch (err) {
          console.error('[WebSocket] Send error:', err);
        }
      }
    }, 50);

    return () => clearInterval(sendInterval);
  }, [clientId, currentFFTSize]);

  const handleFFTSizeChange = useCallback((size) => {
    if (!FFT_SIZES.includes(size)) {
      console.warn('[App] Invalid FFT size:', size);
      return;
    }
    
    setFftSize(size);
    setFFTSize(size);
    
    if (spectrumRef.current) {
      spectrumRef.current = null;
      setSpectrumData(null);
    }
  }, [setFFTSize]);

  const handleStart = useCallback(() => {
    if (!isRecording) {
      startRecording();
    }
  }, [isRecording, startRecording]);

  const handleStop = useCallback(() => {
    stopRecording();
    spectrumRef.current = null;
    setSpectrumData(null);
  }, [stopRecording]);

  const handleStartFingerprinting = useCallback(() => {
    if (!isRecording) {
      alert('请先开始录音');
      return;
    }
    
    setIsFingerprinting(true);
    setCurrentFingerprints([]);
    fingerprintTimeRef.current = 0;
    
    if (fingerprintGenRef.current) {
      fingerprintGenRef.current.reset();
    }
    
    console.log('[App] Fingerprinting started');
  }, [isRecording]);

  const handleStopFingerprinting = useCallback(() => {
    setIsFingerprinting(false);
    console.log('[App] Fingerprinting stopped, collected:', currentFingerprints.length, 'hashes');
  }, [currentFingerprints]);

  const handleSaveClip = useCallback(async (clipName) => {
    if (!clipName || currentFingerprints.length === 0) {
      alert('请输入名称并确保有指纹数据');
      return;
    }

    try {
      const response = await fetch(`${API_BASE}/api/clips`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          name: clipName,
          duration: fingerprintTimeRef.current / 20,
          sampleRate: 48000,
          fftSize: 1024,
          fingerprints: currentFingerprints.slice(0, 1000)
        })
      });

      if (response.ok) {
        const result = await response.json();
        console.log('[App] Clip saved:', result);
        alert(`音频片段已保存！ID: ${result.id}`);
        fetchClips();
        setCurrentFingerprints([]);
      } else {
        const error = await response.json();
        alert(`保存失败: ${error.error}`);
      }
    } catch (err) {
      console.error('[App] Save error:', err);
      alert('保存失败，请检查服务器连接');
    }
  }, [currentFingerprints]);

  const handleMatch = useCallback(async () => {
    if (currentFingerprints.length === 0) {
      alert('请先采集指纹数据');
      return;
    }

    setMatchStatus('matching');
    setMatchResults(null);

    try {
      const response = await fetch(`${API_BASE}/api/match`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          fingerprints: currentFingerprints,
          timeTolerance: 3,
          minMatches: 5
        })
      });

      if (response.ok) {
        const result = await response.json();
        setMatchResults(result);
        setMatchStatus('completed');
        console.log('[App] Match results:', result);
      } else {
        setMatchStatus('error');
        alert('匹配失败');
      }
    } catch (err) {
      console.error('[App] Match error:', err);
      setMatchStatus('error');
      alert('匹配失败，请检查服务器连接');
    }
  }, [currentFingerprints]);

  const fetchClips = useCallback(async () => {
    try {
      const response = await fetch(`${API_BASE}/api/clips`);
      if (response.ok) {
        const data = await response.json();
        setStoredClips(data.clips || []);
      }
    } catch (err) {
      console.error('[App] Fetch clips error:', err);
    }
  }, []);

  return (
    <div className="app">
      <header className="app-header">
        <h1 className="app-title">Audio Spectrum Analyzer</h1>
        <p className="app-subtitle">Real-time FFT analysis with WebAssembly</p>
      </header>

      <div className="controls">
        <div className="control-group">
          <span className="control-label">Recording</span>
          {!isRecording ? (
            <button
              className="btn btn-start"
              onClick={handleStart}
              disabled={error !== null}
            >
              Start
            </button>
          ) : (
            <button
              className="btn btn-stop"
              onClick={handleStop}
            >
              Stop
            </button>
          )}
        </div>

        <div className="control-group">
          <span className="control-label">FFT Size</span>
          <div className="fft-selector">
            {FFT_SIZES.map((size) => (
              <button
                key={size}
                className={`fft-option ${fftSize === size ? 'active' : ''}`}
                onClick={() => handleFFTSizeChange(size)}
                disabled={isRecording}
              >
                {size}
              </button>
            ))}
          </div>
        </div>

        <div className="control-group">
          <span className="control-label">Status</span>
          <div className={`status ${isRecording ? 'status-active' : 'status-inactive'}`}>
            <span className="status-dot"></span>
            {isRecording ? 'Recording' : 'Idle'}
          </div>
        </div>
      </div>

      {error && (
        <div style={{ color: '#ff6464', marginBottom: '20px', textAlign: 'center' }}>
          Error: {error}
        </div>
      )}

      <SpectrumVisualizer data={spectrumData} fftSize={currentFFTSize} />

      <div className="info-panel">
        <div className="info-item">
          <div className="info-value">{currentFFTSize}</div>
          <div className="info-label">FFT Size</div>
        </div>
        <div className="info-item">
          <div className="info-value">{currentFFTSize / 2}</div>
          <div className="info-label">Frequency Bins</div>
        </div>
        <div className="info-item">
          <div className="info-value">48000 Hz</div>
          <div className="info-label">Sample Rate</div>
        </div>
        <div className="info-item">
          <div className="info-value">{Math.round(48000 / currentFFTSize)} Hz</div>
          <div className="info-label">Resolution</div>
        </div>
      </div>

      <FingerprintPanel
        isFingerprinting={isFingerprinting}
        fingerprintCount={currentFingerprints.length}
        matchResults={matchResults}
        matchStatus={matchStatus}
        storedClips={storedClips}
        onStartFingerprinting={handleStartFingerprinting}
        onStopFingerprinting={handleStopFingerprinting}
        onSaveClip={handleSaveClip}
        onMatch={handleMatch}
        onRefreshClips={fetchClips}
      />

      <div className={`websocket-status ${wsConnected ? 'connected' : 'disconnected'}`}>
        <span className="status-dot"></span>
        WebSocket: {wsConnected ? 'Connected' : 'Disconnected'}
      </div>
    </div>
  );
}

function validateAndCopySpectrum(spectrum) {
  if (!spectrum || spectrum.length === 0) {
    return null;
  }

  const maxLength = 4096;
  const copyLength = Math.min(spectrum.length, maxLength);
  const safeCopy = new Float64Array(copyLength);

  for (let i = 0; i < copyLength; i++) {
    const value = spectrum[i];
    if (!isFinite(value)) {
      safeCopy[i] = 0;
    } else {
      safeCopy[i] = Math.max(0, Math.min(1, value));
    }
  }

  return safeCopy;
}

function validateReceivedSpectrum(spectrum) {
  if (!Array.isArray(spectrum) || spectrum.length === 0) {
    return null;
  }

  const maxLength = 4096;
  const copyLength = Math.min(spectrum.length, maxLength);
  const safeCopy = new Float64Array(copyLength);

  for (let i = 0; i < copyLength; i++) {
    const value = spectrum[i];
    if (typeof value !== 'number' || !isFinite(value)) {
      safeCopy[i] = 0;
    } else {
      safeCopy[i] = Math.max(0, Math.min(1, value));
    }
  }

  return safeCopy;
}

export default App;
