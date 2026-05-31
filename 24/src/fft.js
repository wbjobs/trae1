class FFT {
  constructor(size) {
    this.size = size;
    this.cosTable = new Float64Array(size);
    this.sinTable = new Float64Array(size);
    this.reverseTable = new Uint32Array(size);
    
    this.buildTables();
  }

  buildTables() {
    const n = this.size;
    
    for (let i = 0; i < n; i++) {
      this.cosTable[i] = Math.cos(-2 * Math.PI * i / n);
      this.sinTable[i] = Math.sin(-2 * Math.PI * i / n);
    }
    
    let bits = 0;
    while ((1 << bits) < n) bits++;
    
    for (let i = 0; i < n; i++) {
      let reversed = 0;
      let val = i;
      for (let j = 0; j < bits; j++) {
        reversed = (reversed << 1) | (val & 1);
        val >>= 1;
      }
      this.reverseTable[i] = reversed;
    }
  }

  setSize(size) {
    this.size = size;
    this.cosTable = new Float64Array(size);
    this.sinTable = new Float64Array(size);
    this.reverseTable = new Uint32Array(size);
    this.buildTables();
  }

  getSize() {
    return this.size;
  }

  compute(input) {
    const n = this.size;
    const real = new Float64Array(n);
    const imag = new Float64Array(n);
    
    for (let i = 0; i < n && i < input.length; i++) {
      real[this.reverseTable[i]] = input[i];
    }
    
    for (let len = 2; len <= n; len *= 2) {
      const halfLen = len / 2;
      const step = n / len;
      
      for (let i = 0; i < n; i += len) {
        for (let j = 0; j < halfLen; j++) {
          const idx = j * step;
          const cos = this.cosTable[idx];
          const sin = this.sinTable[idx];
          
          const tReal = real[i + j + halfLen] * cos - imag[i + j + halfLen] * sin;
          const tImag = real[i + j + halfLen] * sin + imag[i + j + halfLen] * cos;
          
          real[i + j + halfLen] = real[i + j] - tReal;
          imag[i + j + halfLen] = imag[i + j] - tImag;
          real[i + j] += tReal;
          imag[i + j] += tImag;
        }
      }
    }
    
    const magnitudes = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) {
      magnitudes[i] = Math.sqrt(real[i] * real[i] + imag[i] * imag[i]);
    }
    
    return magnitudes;
  }
}

export default FFT;
