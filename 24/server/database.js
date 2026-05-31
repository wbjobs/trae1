const Database = require('better-sqlite3');
const path = require('path');
const fs = require('fs');

class FingerprintDatabase {
  constructor(dbPath) {
    const dbDir = path.dirname(dbPath);
    if (!fs.existsSync(dbDir)) {
      fs.mkdirSync(dbDir, { recursive: true });
    }
    
    this.db = new Database(dbPath);
    this.db.pragma('journal_mode = WAL');
    this.db.pragma('synchronous = NORMAL');
    this.db.pragma('cache_size = 10000');
    
    this.initTables();
    this.initStatements();
  }

  initTables() {
    this.db.exec(`
      CREATE TABLE IF NOT EXISTS audio_clips (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        duration REAL DEFAULT 0,
        sample_rate INTEGER DEFAULT 48000,
        fft_size INTEGER DEFAULT 1024,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        hash_count INTEGER DEFAULT 0,
        UNIQUE(name)
      );

      CREATE TABLE IF NOT EXISTS fingerprints (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        clip_id INTEGER NOT NULL,
        hash INTEGER NOT NULL,
        time_offset REAL NOT NULL,
        freq1 REAL,
        freq2 REAL,
        amplitude1 REAL,
        amplitude2 REAL,
        time_delta REAL,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (clip_id) REFERENCES audio_clips(id) ON DELETE CASCADE
      );

      CREATE INDEX IF NOT EXISTS idx_fingerprints_hash ON fingerprints(hash);
      CREATE INDEX IF NOT EXISTS idx_fingerprints_clip ON fingerprints(clip_id);
      CREATE INDEX IF NOT EXISTS idx_fingerprints_hash_time ON fingerprints(hash, time_offset);
      
      CREATE TABLE IF NOT EXISTS match_results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        query_clip_id INTEGER,
        matched_clip_id INTEGER,
        score REAL,
        match_count INTEGER,
        avg_confidence REAL,
        time_offset REAL,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (query_clip_id) REFERENCES audio_clips(id) ON DELETE SET NULL,
        FOREIGN KEY (matched_clip_id) REFERENCES audio_clips(id) ON DELETE SET NULL
      );
    `);
  }

  initStatements() {
    this.insertClip = this.db.prepare(`
      INSERT OR IGNORE INTO audio_clips (name, duration, sample_rate, fft_size)
      VALUES (?, ?, ?, ?)
    `);

    this.getClipByName = this.db.prepare(`
      SELECT * FROM audio_clips WHERE name = ?
    `);

    this.getClipById = this.db.prepare(`
      SELECT * FROM audio_clips WHERE id = ?
    `);

    this.insertFingerprint = this.db.prepare(`
      INSERT INTO fingerprints (clip_id, hash, time_offset, freq1, freq2, amplitude1, amplitude2, time_delta)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `);

    this.insertFingerprintsBatch = this.db.transaction((clipId, fingerprints) => {
      for (const fp of fingerprints) {
        this.insertFingerprint.run(
          clipId,
          fp.hash,
          fp.time,
          fp.freq1,
          fp.freq2,
          fp.amplitude1,
          fp.amplitude2,
          fp.timeOffset
        );
      }
      return fingerprints.length;
    });

    this.getFingerprintsByHash = this.db.prepare(`
      SELECT * FROM fingerprints WHERE hash = ?
    `);

    this.getFingerprintsByClipId = this.db.prepare(`
      SELECT * FROM fingerprints WHERE clip_id = ? ORDER BY time_offset
    `);

    this.getFingerprintsByHashes = this.db.prepare(`
      SELECT * FROM fingerprints WHERE hash IN (SELECT value FROM json_each(?))
    `);

    this.updateClipHashCount = this.db.prepare(`
      UPDATE audio_clips SET hash_count = (
        SELECT COUNT(*) FROM fingerprints WHERE clip_id = ?
      ) WHERE id = ?
    `);

    this.deleteClip = this.db.prepare(`
      DELETE FROM audio_clips WHERE id = ?
    `);

    this.getAllClips = this.db.prepare(`
      SELECT * FROM audio_clips ORDER BY created_at DESC
    `);

    this.insertMatchResult = this.db.prepare(`
      INSERT INTO match_results (query_clip_id, matched_clip_id, score, match_count, avg_confidence, time_offset)
      VALUES (?, ?, ?, ?, ?, ?)
    `);

    this.getMatchResultsByClip = this.db.prepare(`
      SELECT mr.*, ac.name as matched_clip_name
      FROM match_results mr
      LEFT JOIN audio_clips ac ON mr.matched_clip_id = ac.id
      WHERE mr.query_clip_id = ? OR mr.matched_clip_id = ?
      ORDER BY mr.score DESC
    `);
  }

  addClip(name, duration = 0, sampleRate = 48000, fftSize = 1024) {
    const result = this.insertClip.run(name, duration, sampleRate, fftSize);
    
    if (result.changes > 0) {
      return result.lastInsertRowid;
    } else {
      const existing = this.getClipByName.get(name);
      return existing ? existing.id : null;
    }
  }

  addFingerprints(clipId, fingerprints) {
    if (!Array.isArray(fingerprints) || fingerprints.length === 0) {
      return 0;
    }

    const count = this.insertFingerprintsBatch(clipId, fingerprints);
    this.updateClipHashCount.run(clipId, clipId);
    return count;
  }

  getClip(identifier) {
    if (typeof identifier === 'number') {
      return this.getClipById.get(identifier);
    }
    return this.getClipByName.get(identifier);
  }

  getClipFingerprints(clipId) {
    return this.getFingerprintsByClipId.all(clipId);
  }

  findMatchingHashes(hashes) {
    if (!Array.isArray(hashes) || hashes.length === 0) {
      return [];
    }

    const results = [];
    const seen = new Set();

    for (const hash of hashes) {
      if (seen.has(hash)) continue;
      seen.add(hash);

      const matches = this.getFingerprintsByHash.all(hash);
      results.push(...matches);
    }

    return results;
  }

  matchFingerprints(queryFingerprints, timeTolerance = 3) {
    if (!Array.isArray(queryFingerprints) || queryFingerprints.length === 0) {
      return [];
    }

    const queryHashes = queryFingerprints.map(fp => fp.hash);
    const dbMatches = this.findMatchingHashes(queryHashes);

    if (dbMatches.length === 0) {
      return [];
    }

    const matchesByClip = new Map();

    for (const queryFp of queryFingerprints) {
      for (const dbFp of dbMatches) {
        if (queryFp.hash !== dbFp.hash) continue;

        const timeDiff = Math.abs(queryFp.time - dbFp.time_offset);
        if (timeDiff > timeTolerance) continue;

        const clipId = dbFp.clip_id;
        if (!matchesByClip.has(clipId)) {
          matchesByClip.set(clipId, {
            clipId,
            matches: [],
            totalConfidence: 0
          });
        }

        const confidence = this.calculateConfidence(queryFp, dbFp);
        matchesByClip.get(clipId).matches.push({
          queryHash: queryFp,
          dbHash: dbFp,
          timeDiff,
          confidence
        });
        matchesByClip.get(clipId).totalConfidence += confidence;
      }
    }

    const results = [];
    for (const [clipId, data] of matchesByClip) {
      if (data.matches.length >= 5) {
        const clip = this.getClipById.get(clipId);
        results.push({
          clipId,
          clipName: clip ? clip.name : 'Unknown',
          matchCount: data.matches.length,
          averageConfidence: data.totalConfidence / data.matches.length,
          score: data.totalConfidence * data.matches.length,
          matches: data.matches.slice(0, 10)
        });
      }
    }

    results.sort((a, b) => b.score - a.score);
    return results;
  }

  calculateConfidence(hash1, hash2) {
    const amp1 = hash1.amplitude1 || 0;
    const amp2 = hash1.amplitude2 || 0;
    const amp3 = hash2.amplitude1 || 0;
    const amp4 = hash2.amplitude2 || 0;
    
    const ampDiff = Math.abs(amp1 - amp3) + Math.abs(amp2 - amp4);
    return Math.max(0, 1 - ampDiff / 2);
  }

  getAllClips() {
    return this.getAllClips.all();
  }

  removeClip(clipId) {
    return this.deleteClip.run(clipId);
  }

  saveMatchResult(queryClipId, matchedClipId, score, matchCount, avgConfidence, timeOffset) {
    return this.insertMatchResult.run(
      queryClipId,
      matchedClipId,
      score,
      matchCount,
      avgConfidence,
      timeOffset
    );
  }

  getMatchHistory(clipId) {
    return this.getMatchResultsByClip.all(clipId, clipId);
  }

  getStatistics() {
    const clipCount = this.db.prepare('SELECT COUNT(*) as count FROM audio_clips').get().count;
    const fingerprintCount = this.db.prepare('SELECT COUNT(*) as count FROM fingerprints').get().count;
    const matchCount = this.db.prepare('SELECT COUNT(*) as count FROM match_results').get().count;
    
    return {
      clipCount,
      fingerprintCount,
      matchCount
    };
  }

  close() {
    this.db.close();
  }
}

module.exports = FingerprintDatabase;
