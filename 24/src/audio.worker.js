import FFT from './fft.js';

let fft = null;
let fftSize = 512;
let isInitialized = false;

self.onmessage = function(event) {
  const { type, data, size } = event.data;

  switch (type) {
    case 'init':
      fftSize = validateFFTSize(size || 512);
      fft = new FFT(fftSize);
      isInitialized = true;
      self.postMessage({ type: 'ready', fftSize });
      break;

    case 'setSize':
      fftSize = validateFFTSize(size);
      if (fft) {
        fft.setSize(fftSize);
      } else {
        fft = new FFT(fftSize);
      }
      isInitialized = true;
      self.postMessage({ type: 'sizeChanged', fftSize });
      break;

    case 'process':
      if (!isInitialized || !fft) {
        fft = new FFT(fftSize);
        isInitialized = true;
      }
      
      try {
        const audioData = convertToFloat64(data, fftSize);
        
        if (!validateAudioData(audioData)) {
          console.warn('[Worker] Invalid audio data detected');
          return;
        }
        
        const spectrum = fft.compute(audioData);
        
        if (!validateSpectrum(spectrum)) {
          console.warn('[Worker] Invalid spectrum result');
          return;
        }
        
        const normalizedSpectrum = normalizeSpectrum(spectrum);
        
        self.postMessage({
          type: 'spectrum',
          data: normalizedSpectrum.buffer,
          fftSize
        }, [normalizedSpectrum.buffer]);
      } catch (error) {
        console.error('[Worker] Processing error:', error.message);
        self.postMessage({ 
          type: 'error', 
          message: error.message 
        });
      }
      break;

    case 'stop':
      isInitialized = false;
      self.postMessage({ type: 'stopped' });
      break;

    default:
      break;
  }
};

function validateFFTSize(size) {
  const validSizes = [256, 512, 1024, 2048, 4096];
  if (validSizes.includes(size)) {
    return size;
  }
  console.warn('[Worker] Invalid FFT size:', size, 'using 512 instead');
  return 512;
}

function convertToFloat64(buffer, expectedSize) {
  if (!buffer) {
    throw new Error('Empty audio buffer received');
  }

  const float32Data = new Float32Array(buffer);
  const actualLength = float32Data.length;

  if (actualLength === 0) {
    throw new Error('Zero-length audio data');
  }

  const float64Data = new Float64Array(expectedSize);
  
  const copyLength = Math.min(actualLength, expectedSize);
  
  for (let i = 0; i < copyLength; i++) {
    const value = float32Data[i];
    
    if (!isFinite(value)) {
      float64Data[i] = 0;
    } else {
      float64Data[i] = Math.max(-1.0, Math.min(1.0, value));
    }
  }
  
  return float64Data;
}

function validateAudioData(data) {
  if (!data || data.length === 0) {
    return false;
  }
  
  let hasNonZero = false;
  for (let i = 0; i < data.length; i++) {
    if (!isFinite(data[i])) {
      return false;
    }
    if (data[i] !== 0) {
      hasNonZero = true;
    }
  }
  
  return hasNonZero || data.length > 0;
}

function validateSpectrum(spectrum) {
  if (!spectrum || spectrum.length === 0) {
    return false;
  }
  
  for (let i = 0; i < spectrum.length; i++) {
    if (!isFinite(spectrum[i])) {
      return false;
    }
  }
  
  return true;
}

function normalizeSpectrum(spectrum) {
  const len = spectrum.length;
  const normalized = new Float64Array(len);
  
  let max = 0;
  for (let i = 0; i < len; i++) {
    const val = Math.abs(spectrum[i]);
    if (val > max) max = val;
  }
  
  if (max > 0 && isFinite(max)) {
    for (let i = 0; i < len; i++) {
      normalized[i] = Math.max(0, Math.min(1, spectrum[i] / max));
    }
  }
  
  return normalized;
}

self.onerror = function(error) {
  console.error('[Worker] Error:', error);
  self.postMessage({ type: 'error', message: error.message });
};
