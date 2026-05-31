class FingerprintGenerator {
  constructor(options = {}) {
    this.minPeakDistance = options.minPeakDistance || 3;
    this.minPeakThreshold = options.minPeakThreshold || 0.5;
    this.maxPeaks = options.maxPeaks || 50;
    this.fanOut = options.fanOut || 15;
    this.minHashTimeDelta = options.minHashTimeDelta || 1;
    this.maxHashTimeDelta = options.maxHashTimeDelta || 200;
    this.samplingRate = options.samplingRate || 48000;
    this.fftSize = options.fftSize || 1024;
    this.peakHistory = [];
    this.maxHistorySize = options.maxHistorySize || 100;
  }

  findPeaks(spectrum, timeIndex) {
    const peaks = [];
    const len = spectrum.length;

    for (let i = 1; i < len - 1; i++) {
      const value = spectrum[i];
      
      if (value < this.minPeakThreshold) continue;
      
      const leftNeighbor = spectrum[i - 1];
      const rightNeighbor = spectrum[i + 1];
      
      if (value > leftNeighbor && value > rightNeighbor) {
        let isPeak = true;
        
        if (this.minPeakDistance > 0) {
          for (const peak of peaks) {
            if (Math.abs(i - peak.freqBin) < this.minPeakDistance) {
              isPeak = false;
              break;
            }
          }
        }
        
        if (isPeak) {
          const frequency = this.calculateFrequency(i);
          peaks.push({
            time: timeIndex,
            freqBin: i,
            frequency,
            amplitude: value
          });
        }
      }
    }

    peaks.sort((a, b) => b.amplitude - a.amplitude);
    return peaks.slice(0, this.maxPeaks);
  }

  calculateFrequency(binIndex) {
    return (binIndex * this.samplingRate) / this.fftSize;
  }

  generateHashes(peaks, currentTime) {
    const hashes = [];

    this.peakHistory.push({
      time: currentTime,
      peaks: peaks.filter(p => p.amplitude > this.minPeakThreshold * 0.8)
    });

    if (this.peakHistory.length > this.maxHistorySize) {
      this.peakHistory.shift();
    }

    if (this.peakHistory.length < 2) return hashes;

    const currentPeaks = this.peakHistory[this.peakHistory.length - 1].peaks;
    const historyLimit = Math.min(this.peakHistory.length, 50);

    for (let h = this.peakHistory.length - historyLimit; h < this.peakHistory.length - 1; h++) {
      const pastEntry = this.peakHistory[h];
      const timeDelta = currentTime - pastEntry.time;

      if (timeDelta < this.minHashTimeDelta || timeDelta > this.maxHashTimeDelta) {
        continue;
      }

      const pastPeaks = pastEntry.peaks;
      const pairsGenerated = new Set();

      for (let i = 0; i < currentPeaks.length && pairsGenerated.size < this.fanOut; i++) {
        for (let j = 0; j < pastPeaks.length && pairsGenerated.size < this.fanOut; j++) {
          const pairKey = `${currentPeaks[i].freqBin}_${pastPeaks[j].freqBin}_${timeDelta}`;
          
          if (pairsGenerated.has(pairKey)) continue;
          pairsGenerated.add(pairKey);

          const hash = this.createHash(
            currentPeaks[i].freqBin,
            pastPeaks[j].freqBin,
            timeDelta
          );

          hashes.push({
            hash,
            time: currentTime,
            timeOffset: timeDelta,
            freq1: currentPeaks[i].frequency,
            freq2: pastPeaks[j].frequency,
            amplitude1: currentPeaks[i].amplitude,
            amplitude2: pastPeaks[j].amplitude
          });
        }
      }
    }

    return hashes;
  }

  createHash(freqBin1, freqBin2, timeDelta) {
    let hash = 0;
    hash = (hash << 8) | (freqBin1 & 0xFF);
    hash = (hash << 8) | (freqBin2 & 0xFF);
    hash = (hash << 16) | (timeDelta & 0xFFFF);
    return hash >>> 0;
  }

  hashToString(hash) {
    return hash.toString(16).padStart(8, '0');
  }

  processSpectrum(spectrum, timeIndex) {
    const peaks = this.findPeaks(spectrum, timeIndex);
    const hashes = this.generateHashes(peaks, timeIndex);
    
    return {
      peaks,
      hashes,
      numPeaks: peaks.length,
      numHashes: hashes.length
    };
  }

  reset() {
    this.peakHistory = [];
  }

  getStatistics() {
    let totalPeaks = 0;
    for (const entry of this.peakHistory) {
      totalPeaks += entry.peaks.length;
    }
    
    return {
      historySize: this.peakHistory.length,
      totalPeaks,
      currentPeaks: this.peakHistory.length > 0 ? 
        this.peakHistory[this.peakHistory.length - 1].peaks.length : 0
    };
  }
}

class FingerprintMatcher {
  constructor(options = {}) {
    this.timeTolerance = options.timeTolerance || 3;
    this.matchThreshold = options.matchThreshold || 0.1;
    this.minMatches = options.minMatches || 5;
  }

  findMatches(queryHashes, databaseHashes) {
    const matches = [];
    
    for (const queryHash of queryHashes) {
      for (const dbHash of databaseHashes) {
        if (queryHash.hash === dbHash.hash) {
          const timeDiff = Math.abs(queryHash.time - dbHash.time);
          
          if (timeDiff <= this.timeTolerance) {
            matches.push({
              queryHash,
              dbHash,
              timeDiff,
              confidence: this.calculateConfidence(queryHash, dbHash)
            });
          }
        }
      }
    }

    return this.clusterMatches(matches);
  }

  calculateConfidence(hash1, hash2) {
    const ampDiff = Math.abs(hash1.amplitude1 - hash2.amplitude1) + 
                    Math.abs(hash1.amplitude2 - hash2.amplitude2);
    return Math.max(0, 1 - ampDiff / 2);
  }

  clusterMatches(matches) {
    if (matches.length === 0) {
      return [];
    }

    const clusters = new Map();
    
    for (const match of matches) {
      const timeOffset = match.dbHash.time - match.queryHash.time;
      const key = Math.round(timeOffset / this.timeTolerance);
      
      if (!clusters.has(key)) {
        clusters.set(key, {
          matches: [],
          totalConfidence: 0,
          timeOffset: timeOffset
        });
      }
      
      const cluster = clusters.get(key);
      cluster.matches.push(match);
      cluster.totalConfidence += match.confidence;
    }

    const results = Array.from(clusters.values())
      .map(cluster => ({
        matchCount: cluster.matches.length,
        averageConfidence: cluster.totalConfidence / cluster.matches.length,
        timeOffset: cluster.timeOffset,
        score: cluster.totalConfidence * cluster.matches.length
      }))
      .filter(result => result.matchCount >= this.minMatches)
      .sort((a, b) => b.score - a.score);

    return results;
  }

  calculateSimilarity(hashes1, hashes2) {
    if (hashes1.length === 0 || hashes2.length === 0) {
      return 0;
    }

    const hashSet1 = new Set(hashes1.map(h => h.hash));
    const hashSet2 = new Set(hashes2.map(h => h.hash));

    let intersection = 0;
    for (const hash of hashSet1) {
      if (hashSet2.has(hash)) {
        intersection++;
      }
    }

    const union = hashSet1.size + hashSet2.size - intersection;
    
    return union > 0 ? intersection / union : 0;
  }
}

export { FingerprintGenerator, FingerprintMatcher };
export default FingerprintGenerator;
