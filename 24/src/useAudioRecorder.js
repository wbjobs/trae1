import { useState, useRef, useCallback, useEffect } from 'react';

const VALID_FFT_SIZES = [256, 512, 1024, 2048, 4096];

export function useAudioRecorder(initialFFTSize = 512) {
  const [isRecording, setIsRecording] = useState(false);
  const [error, setError] = useState(null);
  const [currentFFTSize, setCurrentFFTSize] = useState(initialFFTSize);
  
  const audioContextRef = useRef(null);
  const analyserRef = useRef(null);
  const streamRef = useRef(null);
  const workerRef = useRef(null);
  const animationFrameRef = useRef(null);
  const bufferRef = useRef(null);
  const isProcessingRef = useRef(false);
  const fftSizeRef = useRef(initialFFTSize);

  useEffect(() => {
    fftSizeRef.current = initialFFTSize;
    setCurrentFFTSize(initialFFTSize);
  }, [initialFFTSize]);

  useEffect(() => {
    workerRef.current = new Worker(
      new URL('./audio.worker.js', import.meta.url),
      { type: 'module' }
    );

    workerRef.current.postMessage({
      type: 'init',
      size: fftSizeRef.current
    });

    workerRef.current.onmessage = (event) => {
      if (event.data.type === 'error') {
        console.error('[AudioRecorder] Worker error:', event.data.message);
      }
    };

    return () => {
      if (workerRef.current) {
        workerRef.current.terminate();
        workerRef.current = null;
      }
      cleanupRecording();
    };
  }, []);

  const cleanupRecording = useCallback(() => {
    if (animationFrameRef.current) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }

    if (streamRef.current) {
      streamRef.current.getTracks().forEach(track => track.stop());
      streamRef.current = null;
    }

    if (audioContextRef.current) {
      audioContextRef.current.close().catch(() => {});
      audioContextRef.current = null;
    }

    analyserRef.current = null;
    bufferRef.current = null;
    isProcessingRef.current = false;
  }, []);

  const validateFFTSize = useCallback((size) => {
    if (VALID_FFT_SIZES.includes(size)) {
      return size;
    }
    console.warn('[AudioRecorder] Invalid FFT size:', size, 'using 512 instead');
    return 512;
  }, []);

  const startRecording = useCallback(async () => {
    if (isRecording) {
      console.warn('[AudioRecorder] Already recording');
      return;
    }

    try {
      setError(null);

      const stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false,
          channelCount: 1
        }
      });

      streamRef.current = stream;

      const audioContext = new AudioContext({
        latencyHint: 'interactive'
      });
      audioContextRef.current = audioContext;

      const sampleRate = audioContext.sampleRate;
      console.log('[AudioRecorder] Sample rate:', sampleRate);

      const source = audioContext.createMediaStreamSource(stream);
      const analyser = audioContext.createAnalyser();
      
      const fftSize = fftSizeRef.current;
      analyser.fftSize = fftSize;
      analyser.smoothingTimeConstant = 0.3;
      
      source.connect(analyser);
      analyserRef.current = analyser;

      const bufferSize = analyser.fftSize;
      bufferRef.current = new Float32Array(bufferSize);

      console.log('[AudioRecorder] Started with FFT size:', fftSize);
      console.log('[AudioRecorder] Buffer size:', bufferSize);

      setIsRecording(true);
      isProcessingRef.current = true;

      const processAudio = () => {
        if (!isProcessingRef.current) return;
        if (!analyserRef.current || !workerRef.current) return;
        if (analyserRef.current.fftSize !== fftSizeRef.current) {
          analyserRef.current.fftSize = fftSizeRef.current;
          bufferRef.current = new Float32Array(analyserRef.current.fftSize);
        }

        analyserRef.current.getFloatTimeDomainData(bufferRef.current);

        if (!validateAudioBuffer(bufferRef.current)) {
          console.warn('[AudioRecorder] Invalid audio buffer, skipping frame');
          animationFrameRef.current = requestAnimationFrame(processAudio);
          return;
        }

        const dataCopy = new Float32Array(bufferRef.current);
        
        try {
          workerRef.current.postMessage({
            type: 'process',
            data: dataCopy.buffer,
            fftSize: fftSizeRef.current
          }, [dataCopy.buffer]);
        } catch (err) {
          console.error('[AudioRecorder] Failed to send to worker:', err.message);
          workerRef.current.postMessage({
            type: 'process',
            data: new Float32Array(bufferRef.current).buffer,
            fftSize: fftSizeRef.current
          });
        }

        animationFrameRef.current = requestAnimationFrame(processAudio);
      };

      animationFrameRef.current = requestAnimationFrame(processAudio);

    } catch (err) {
      console.error('[AudioRecorder] Recording error:', err);
      setError(err.message || 'Failed to access microphone');
      cleanupRecording();
      setIsRecording(false);
    }
  }, [isRecording, cleanupRecording]);

  const stopRecording = useCallback(() => {
    isProcessingRef.current = false;
    cleanupRecording();
    setIsRecording(false);
  }, [cleanupRecording]);

  const setFFTSize = useCallback((newSize) => {
    const validSize = validateFFTSize(newSize);
    fftSizeRef.current = validSize;
    setCurrentFFTSize(validSize);

    if (workerRef.current) {
      workerRef.current.postMessage({
        type: 'setSize',
        size: validSize
      });
    }

    if (analyserRef.current && isRecording) {
      analyserRef.current.fftSize = validSize;
      bufferRef.current = new Float32Array(analyserRef.current.fftSize);
      console.log('[AudioRecorder] Updated analyser FFT size to:', validSize);
    }
  }, [validateFFTSize, isRecording]);

  const onSpectrumData = useCallback((callback) => {
    if (workerRef.current) {
      workerRef.current.onmessage = (event) => {
        if (event.data.type === 'spectrum') {
          try {
            const spectrum = new Float64Array(event.data.data);
            callback(spectrum);
          } catch (err) {
            console.error('[AudioRecorder] Failed to process spectrum data:', err);
          }
        } else if (event.data.type === 'error') {
          console.error('[AudioRecorder] Worker error:', event.data.message);
        }
      };
    }
  }, []);

  return {
    isRecording,
    error,
    currentFFTSize,
    startRecording,
    stopRecording,
    setFFTSize,
    onSpectrumData
  };
}

function validateAudioBuffer(buffer) {
  if (!buffer || buffer.length === 0) {
    return false;
  }

  for (let i = 0; i < buffer.length; i++) {
    if (!isFinite(buffer[i])) {
      return false;
    }
  }

  return true;
}
