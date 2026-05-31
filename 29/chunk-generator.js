const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const CHUNK_SIZE = 1 * 1024 * 1024;
const TOTAL_SIZE = 10 * 1024 * 1024;
const CHUNK_DIR = path.join(__dirname, 'chunks');
const PUBLIC_DIR = path.join(__dirname, 'public');

function generateLargeJSFile(size) {
  const parts = [];
  const template = `
// Large Library Module - Part {part}
(function(global) {
  'use strict';

  var LibPart{part} = (function() {
    var data = [];
    var config = {{
      version: '1.0.0',
      name: 'LargeLibPart{part}',
      chunkSize: {chunkSize},
      features: ['math', 'string', 'array', 'object', 'async', 'cache', 'crypto', 'utils', 'dom', 'net']
    }};

    function generateMockData(count) {{
      var result = [];
      for (var i = 0; i < count; i++) {{
        result.push({{
          id: i,
          value: Math.random().toString(36).substring(2),
          timestamp: Date.now(),
          metadata: {{
            hash: Math.random().toString(36).substring(2),
            type: ['string', 'number', 'boolean', 'object', 'array'][Math.floor(Math.random() * 5)],
            nested: {{
              level1: {{
                level2: {{
                  data: 'nested_data_' + i
                }}
              }}
            }}
          }}
        }});
      }}
      return result;
    }}

    function complexCalculation(input) {{
      var result = input;
      for (var i = 0; i < 100; i++) {{
        result = Math.sqrt(Math.abs(result * Math.sin(i) + Math.cos(i * 2))) * 1000;
        result = Math.log(result + 1) * Math.exp(result * 0.001);
      }}
      return result;
    }}

    function stringManipulation(str) {{
      var operations = [
        function(s) {{ return s.toUpperCase(); }},
        function(s) {{ return s.toLowerCase(); }},
        function(s) {{ return s.split('').reverse().join(''); }},
        function(s) {{ return s.replace(/a/g, '@').replace(/e/g, '3').replace(/i/g, '1'); }}
      ];
      var result = str;
      operations.forEach(function(op) {{
        result = op(result);
      }});
      return result;
    }}

    function arrayOperations(arr) {{
      return arr
        .filter(function(x) {{ return x % 2 === 0; }})
        .map(function(x) {{ return x * x; }})
        .reduce(function(acc, x) {{ return acc + x; }}, 0);
    }}

    function asyncSimulator(callback) {{
      var delays = [10, 50, 100, 200, 500];
      var results = [];
      var completed = 0;

      delays.forEach(function(delay, index) {{
        setTimeout(function() {{
          results[index] = {{
            delay: delay,
            timestamp: Date.now(),
            data: crypto.randomBytes(32).toString('hex')
          }};
          completed++;
          if (completed === delays.length) {{
            callback(results);
          }}
        }}, delay);
      }});
    }}

    var cache = new Map();
    var MAX_CACHE_SIZE = 1000;

    function cachedCalculation(key, fn) {{
      if (cache.has(key)) {{
        return cache.get(key);
      }}
      var result = fn();
      if (cache.size >= MAX_CACHE_SIZE) {{
        var firstKey = cache.keys().next().value;
        cache.delete(firstKey);
      }}
      cache.set(key, result);
      return result;
    }}

    function cryptoHash(data) {{
      return crypto.createHash('sha256').update(data).digest('hex');
    }}

    var events = {{}};
    function on(event, handler) {{
      if (!events[event]) {{
        events[event] = [];
      }}
      events[event].push(handler);
    }}
    function emit(event, data) {{
      if (events[event]) {{
        events[event].forEach(function(handler) {{
          handler(data);
        }});
      }}
    }}

    return {{
      config: config,
      generateMockData: generateMockData,
      complexCalculation: complexCalculation,
      stringManipulation: stringManipulation,
      arrayOperations: arrayOperations,
      asyncSimulator: asyncSimulator,
      cachedCalculation: cachedCalculation,
      cryptoHash: cryptoHash,
      on: on,
      emit: emit
    }};
  }})();

  global.LibPart{part} = LibPart{part};
  console.log('[LargeLib] Part {part} loaded successfully');
}})(typeof window !== 'undefined' ? window : globalThis);
`;

  let currentSize = 0;
  let partNum = 0;

  while (currentSize < size) {
    const partContent = template.replace(/\{part\}/g, partNum)
                                .replace(/\{chunkSize\}/g, CHUNK_SIZE);
    parts.push(partContent);
    currentSize += Buffer.byteLength(partContent, 'utf8');
    partNum++;
  }

  return parts.join('\n');
}

function sha256(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

function chunkFile(fileContent, chunkSize) {
  const chunks = [];
  const buffer = Buffer.from(fileContent, 'utf8');

  for (let i = 0; i < buffer.length; i += chunkSize) {
    const chunk = buffer.slice(i, Math.min(i + chunkSize, buffer.length));
    chunks.push({
      index: chunks.length,
      size: chunk.length,
      hash: sha256(chunk),
      offset: i
    });
  }

  return chunks;
}

function main() {
  console.log('[Generator] Starting resource generation...');
  console.log(`[Generator] Target size: ${TOTAL_SIZE / 1024 / 1024}MB`);

  if (!fs.existsSync(CHUNK_DIR)) {
    fs.mkdirSync(CHUNK_DIR, { recursive: true });
  }
  if (!fs.existsSync(PUBLIC_DIR)) {
    fs.mkdirSync(PUBLIC_DIR, { recursive: true });
  }

  console.log('[Generator] Generating large JS file...');
  const fileContent = generateLargeJSFile(TOTAL_SIZE);
  const actualSize = Buffer.byteLength(fileContent, 'utf8');
  console.log(`[Generator] File generated, actual size: ${(actualSize / 1024 / 1024).toFixed(2)}MB`);

  const originalPath = path.join(PUBLIC_DIR, 'large-library.js');
  fs.writeFileSync(originalPath, fileContent, 'utf8');
  console.log(`[Generator] Original file saved to: ${originalPath}`);

  console.log('[Generator] Chunking file...');
  const chunks = chunkFile(fileContent, CHUNK_SIZE);
  console.log(`[Generator] Created ${chunks.length} chunks`);

  const fileBuffer = Buffer.from(fileContent, 'utf8');
  const infoHash = sha256(fileBuffer);

  chunks.forEach((chunk, index) => {
    const chunkData = fileBuffer.slice(chunk.offset, chunk.offset + chunk.size);
    const chunkPath = path.join(CHUNK_DIR, `${infoHash}_${index}`);
    fs.writeFileSync(chunkPath, chunkData);
    console.log(`[Generator] Chunk ${index}: ${chunk.size} bytes, SHA256: ${chunk.hash.substring(0, 16)}...`);
  });

  const manifest = {
    infoHash: infoHash,
    fileName: 'large-library.js',
    totalSize: actualSize,
    chunkSize: CHUNK_SIZE,
    totalChunks: chunks.length,
    fileHash: sha256(fileBuffer),
    chunks: chunks.map(c => ({
      index: c.index,
      size: c.size,
      hash: c.hash
    })),
    createdAt: new Date().toISOString()
  };

  const manifestPath = path.join(CHUNK_DIR, 'manifest.json');
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2), 'utf8');
  console.log(`[Generator] Manifest saved to: ${manifestPath}`);
  console.log(`[Generator] InfoHash: ${infoHash}`);

  console.log('\n[Generator] Summary:');
  console.log(`  - File size: ${(actualSize / 1024 / 1024).toFixed(2)} MB`);
  console.log(`  - Chunk size: ${CHUNK_SIZE / 1024} KB`);
  console.log(`  - Total chunks: ${chunks.length}`);
  console.log(`  - InfoHash: ${infoHash}`);
  console.log(`\n[Generator] Done! You can now start the server with: npm start`);
}

main();