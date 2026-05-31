# Audio Fingerprint Recognition CLI

Shazam-style audio fingerprint recognition system implemented in C++.

## Features

- Real-time audio capture from microphone (44.1kHz, 1024 samples/frame)
- FFT-based spectrum analysis using FFTW3
- Shazam algorithm fingerprint extraction
- SQLite database for fingerprint storage and matching
- High-pass filter + normalization for noise reduction
- Recognition latency < 3 seconds
- Support for 100+ songs in the database

## Architecture

```
┌─────────────┐   ┌─────────────┐   ┌──────────────┐   ┌──────────────┐
│  Audio      │──▶│  Preprocess │──▶│  Fingerprint │──▶│  Database    │
│  Capture    │   │  (HPF+Norm) │   │  (FFT+Peaks) │   │  (SQLite)    │
└─────────────┘   └─────────────┘   └──────────────┘   └──────┬───────┘
                                                               │
                                                               ▼
                                                        ┌──────────────┐
                                                        │   Matcher    │
                                                        │  (Shazam)    │
                                                        └──────────────┘
```

### Core Modules

| Module | File | Description |
|--------|------|-------------|
| `audio_capture` | `src/audio_capture.cpp` | Microphone input via libsoundio |
| `preprocessor` | `src/preprocessor.cpp` | High-pass filter + normalization |
| `fingerprint` | `src/fingerprint.cpp` | FFT analysis + Shazam fingerprinting |
| `database` | `src/database.cpp` | SQLite storage and query |
| `matcher` | `src/matcher.cpp` | Candidate matching and scoring |

## Dependencies

- **C++17** compiler (GCC/Clang/MSVC)
- **CMake** >= 3.15
- **libsoundio** - Cross-platform audio I/O
- **FFTW3** - Fast Fourier Transform
- **SQLite3** - Database engine

### Install Dependencies (Linux)

```bash
sudo apt-get install cmake libsoundio-dev libfftw3-dev libsqlite3-dev
```

### Install Dependencies (macOS)

```bash
brew install cmake libsoundio fftw sqlite
```

### Install Dependencies (Windows)

Use vcpkg or MSYS2:
```bash
vcpkg install soundio fftw3 sqlite3
```

## Building

```bash
# Clone and build
git clone <repo-url>
cd audio-fingerprint

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Install
sudo cmake --install .
```

Or use the build script (Windows):
```cmd
build.bat
```

## Usage

### 1. Build Fingerprint Database

Generate fingerprints for 100 songs (synthetic or from WAV files):

```bash
# Generate 100 synthetic song fingerprints
afp_builddb --db fingerprints.db

# Or use real WAV files
afp_builddb --db fingerprints.db --wav-dir /path/to/wav/files
```

### 2. List Audio Devices

```bash
afp_cli --list-devices
```

### 3. Real-time Recognition

```bash
# Basic usage
afp_cli --db fingerprints.db

# With specific device
afp_cli --db fingerprints.db --device 0

# Verbose mode
afp_cli --db fingerprints.db --verbose

# Time-limited recognition (5 seconds)
afp_cli --db fingerprints.db --duration 5000

# No-color output
afp_cli --db fingerprints.db --no-color
```

### Output Format

When a match is found:
```
╔═══════════════════════════════════╗
║        🎵  MATCH FOUND! 🎵        ║
╚═══════════════════════════════════╝
  🎼 Song:     Moonlight Sonata
  🎤 Artist:   Ludwig van Beethoven
  💿 Album:    Classical Masterpieces
  ⏱️  Time:     12.34s
  📊 Matches:  42
  🎯 Conf:     85.5%
  ⚡ Latency:  1250ms
───────────────────────────────────
```

## Algorithm Details

### Fingerprint Extraction (Shazam-style)

1. **Audio Capture**: 44.1kHz mono, 1024 samples per frame
2. **Preprocessing**:
   - High-pass filter (150Hz cutoff) to remove low-frequency noise
   - Peak normalization to [-1, 1] range
3. **Windowing**: Hann window applied to reduce spectral leakage
4. **FFT**: Real-to-complex FFT (FFTW3)
5. **Spectrum Binning**: 64 frequency bands (logarithmic spacing)
6. **Peak Detection**: Local maxima in each frame (top 10 peaks)
7. **Hash Generation**: Pair-wise combinatorial hashing:
   - Anchor point + target zone (5 points within 1-6 frames)
   - Hash = (bin1:6bit | bin2:6bit | delta:4bit | XOR:16bit)

### Matching Algorithm

1. **Database Query**: Query all candidate hashes from SQLite
2. **Time Alignment**: Compute delta histogram for each song
3. **Scoring**: Count matching time-aligned hashes
4. **Threshold**: Require minimum 5 matching hashes
5. **Confidence**: Ratio of matched hashes to total query hashes

## Performance

- **Recognition Latency**: < 3 seconds (typical: 1-2s)
- **Database Size**: ~100 songs, ~50K fingerprints each
- **Memory Usage**: ~50MB (FFT buffers + hash accumulation)
- **CPU Usage**: ~15% on modern quad-core CPU

## Configuration

Key parameters in `include/common.hpp`:

```cpp
constexpr int SAMPLE_RATE = 44100;      // Audio sample rate
constexpr int FRAME_SIZE = 1024;        // Samples per frame
constexpr double HIGH_PASS_CUTOFF = 150.0;  // HPF cutoff Hz
constexpr int FINGERPRINT_BINS = 64;    // Frequency bins
constexpr int FAN_VALUE = 5;            // Peaks per anchor
constexpr int MATCH_THRESHOLD = 5;      // Minimum matches
```

## API Reference

### AudioCapture

```cpp
AudioCapture capture(44100, 1024);
capture.setDeviceIndex(0);
capture.start([](const Frame& frame) {
    // Process audio frame
});
capture.stop();
```

### Preprocessor

```cpp
Preprocessor preprocessor(44100, 150.0);
Frame input(1024), output(1024);
preprocessor.process(input, output);
```

### FingerprintExtractor

```cpp
FingerprintExtractor extractor(1024, 64);
auto peaks = extractor.extractPeaks(frame, frame_index);
auto hashes = extractor.generateHashes(peaks);
```

### Database

```cpp
Database db;
db.open("fingerprints.db");
auto candidates = db.queryCandidates(hashes);
auto song = db.getSongInfo(song_id);
```

### Matcher

```cpp
Matcher matcher(5);
MatchResult result = matcher.match(hashes, db, frame_index);
if (result.matched) {
    std::cout << result.song.title << std::endl;
}
```

## Troubleshooting

### No audio input detected
- Check microphone permissions
- Run `afp_cli --list-devices` to verify
- Try `--device 0` or `--device 1`

### No matches found
- Ensure database is built: `afp_builddb --db fingerprints.db`
- Check audio volume and background noise
- Try lower threshold: `--threshold 3`

### High latency
- Reduce database size
- Increase match threshold
- Check system load

## License

MIT License

## References

- Wang, A. "An Industrial Strength Audio Search Algorithm" (2003)
- Shazam Entertainment, US Patent 6,990,453
- libsoundio: http://libsound.io
- FFTW3: http://www.fftw.org
