#include "common.hpp"
#include "preprocessor.hpp"
#include "fingerprint.hpp"
#include "database.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>

struct WavHeader {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
};

struct SongDatabaseEntry {
    std::string title;
    std::string artist;
    std::string album;
    std::string filename;
};

enum NoiseProfile {
    NOISE_CLEAN = 0,
    NOISE_CAFE = 1,
    NOISE_STREET = 2,
    NOISE_OFFICE = 3,
    NOISE_CAR = 4
};

struct BuildOptions {
    std::string db_path = "fingerprints.db";
    bool use_synthetic = true;
    std::string wav_dir;
    NoiseProfile noise_profile = NOISE_CLEAN;
    double noise_level = 0.0;
    bool simulate_noisy_conditions = false;
};

std::vector<SongDatabaseEntry> generateSongList() {
    std::vector<SongDatabaseEntry> songs;
    songs.reserve(100);

    const char* titles[] = {
        "Moonlight Sonata", "Clair de Lune", "Canon in D", "The Four Seasons",
        "Eine kleine Nachtmusik", "Fur Elise", "Ode to Joy", "Ave Maria",
        "The Magic Flute", "Requiem", "Symphony No. 5", "Symphony No. 9",
        "The Blue Danube", "Also Sprach Zarathustra", "The Planets",
        "Bolero", "Carmen Suite", "The Nutcracker", "Swan Lake",
        "Romeo and Juliet", "Peer Gynt", "Finlandia", "Pictures at an Exhibition",
        "Night on Bald Mountain", "Flight of the Bumblebee", "Sabre Dance",
        "Reverie", "Suite Bergamasque", "Children's Corner",
        "La Mer", "Nocturnes", "Preludes", "Images", "Estampes",
        "Gymnopedie No. 1", "Gymnopedie No. 2", "Gymnopedie No. 3",
        "Jeux d'enfants", "Le Piccadilly", "La Belle Excentrique",
        "Turkish March", "Rondo Alla Turca", "Minuet in G",
        "Toccata and Fugue", "Air on the G String", "Jesu Joy of Man's Desiring",
        "Brandenburg Concertos", "Goldberg Variations", "The Well-Tempered Clavier",
        "St Matthew Passion", "Mass in B Minor", "Magnificat",
        "The Creation", "The Seasons", "Trumpet Concerto", "Horn Concerto",
        "Piano Concerto No. 21", "Piano Concerto No. 24", "Symphony No. 40",
        "Symphony No. 41", "Don Giovanni", "The Marriage of Figaro",
        "Cosi fan tutte", "Idomeneo", "La Clemenza di Tito",
        "Hungarian Rhapsody No. 2", "Liebestraum", "La Campanella",
        "Transcendental Etudes", "Paganini Etudes", "Annees de Pelerinage",
        "Funerailles", "Mephisto Waltz", "Harmonies Poetiques",
        "Carnival", "Papillons", "Kreisleriana", "Davidsbundlertanze",
        "Symphonic Etudes", "Carnaval", "Fantasiestucke",
        "Piano Quintet", "Piano Concerto", "Symphony No. 1",
        "Symphony No. 3", "Symphony No. 4", "Manfred Symphony",
        "Romeo and Juliet Overture", "1812 Overture", "Marche Slave",
        "Piano Concerto No. 1", "Violin Concerto", "Variations on a Rococo Theme",
        "Mazurkas", "Polonaises", "Nocturnes",
        "Preludes", "Etudes", "Ballades", "Scherzos", "Sonatas",
        "Waltzes", "Impromptus", "Barcarolle", "Berceuse", "Piano Sonata No. 2",
        "Polonaise-Fantasie", "Fantaisie-Impromptu"
    };

    const char* artists[] = {
        "Ludwig van Beethoven", "Claude Debussy", "Johann Pachelbel", "Antonio Vivaldi",
        "Wolfgang Amadeus Mozart", "Ludwig van Beethoven", "Ludwig van Beethoven",
        "Franz Schubert", "Wolfgang Amadeus Mozart", "Wolfgang Amadeus Mozart",
        "Ludwig van Beethoven", "Ludwig van Beethoven", "Johann Strauss II",
        "Richard Strauss", "Gustav Holst", "Maurice Ravel", "Georges Bizet",
        "Pyotr Ilyich Tchaikovsky", "Pyotr Ilyich Tchaikovsky",
        "Pyotr Ilyich Tchaikovsky", "Edvard Grieg", "Jean Sibelius",
        "Modest Mussorgsky", "Modest Mussorgsky", "Nikolai Rimsky-Korsakov",
        "Aram Khachaturian", "Claude Debussy", "Claude Debussy", "Claude Debussy",
        "Claude Debussy", "Claude Debussy", "Claude Debussy", "Claude Debussy",
        "Claude Debussy", "Claude Debussy", "Erik Satie", "Erik Satie", "Erik Satie",
        "Georges Bizet", "Erik Satie", "Erik Satie", "Wolfgang Amadeus Mozart",
        "Wolfgang Amadeus Mozart", "Johann Sebastian Bach", "Johann Sebastian Bach",
        "Johann Sebastian Bach", "Johann Sebastian Bach", "Johann Sebastian Bach",
        "Johann Sebastian Bach", "Johann Sebastian Bach", "Johann Sebastian Bach",
        "Johann Sebastian Bach", "Johann Sebastian Bach", "Franz Joseph Haydn",
        "Franz Joseph Haydn", "Franz Joseph Haydn", "Wolfgang Amadeus Mozart",
        "Wolfgang Amadeus Mozart", "Wolfgang Amadeus Mozart", "Wolfgang Amadeus Mozart",
        "Wolfgang Amadeus Mozart", "Wolfgang Amadeus Mozart", "Wolfgang Amadeus Mozart",
        "Wolfgang Amadeus Mozart", "Wolfgang Amadeus Mozart", "Franz Liszt",
        "Franz Liszt", "Franz Liszt", "Franz Liszt", "Franz Liszt", "Franz Liszt",
        "Franz Liszt", "Franz Liszt", "Franz Liszt", "Robert Schumann",
        "Robert Schumann", "Robert Schumann", "Robert Schumann", "Robert Schumann",
        "Robert Schumann", "Robert Schumann", "Robert Schumann", "Robert Schumann",
        "Robert Schumann", "Pyotr Ilyich Tchaikovsky", "Pyotr Ilyich Tchaikovsky",
        "Pyotr Ilyich Tchaikovsky", "Pyotr Ilyich Tchaikovsky",
        "Pyotr Ilyich Tchaikovsky", "Pyotr Ilyich Tchaikovsky",
        "Pyotr Ilyich Tchaikovsky", "Pyotr Ilyich Tchaikovsky",
        "Pyotr Ilyich Tchaikovsky", "Frederic Chopin", "Frederic Chopin",
        "Frederic Chopin", "Frederic Chopin", "Frederic Chopin", "Frederic Chopin",
        "Frederic Chopin", "Frederic Chopin", "Frederic Chopin", "Frederic Chopin",
        "Frederic Chopin", "Frederic Chopin", "Frederic Chopin", "Frederic Chopin",
        "Frederic Chopin"
    };

    const char* albums[] = {
        "Classical Masterpieces", "Impressionist Works", "Baroque Suites",
        "Venetian School", "Viennese Classics", "Symphonic Poems",
        "Orchestral Suites", "Opera Highlights", "Ballet Suites",
        "Piano Works", "Chamber Music", "Sacred Music", "Virtuoso Pieces"
    };

    int num_titles = sizeof(titles) / sizeof(titles[0]);
    int num_artists = sizeof(artists) / sizeof(artists[0]);
    int num_albums = sizeof(albums) / sizeof(albums[0]);

    for (int i = 0; i < 100; ++i) {
        SongDatabaseEntry entry;
        entry.title = titles[i % num_titles];
        entry.artist = artists[i % num_artists];
        entry.album = albums[i % num_albums];
        entry.filename = "song_" + std::to_string(i + 1) + ".wav";
        songs.push_back(entry);
    }

    return songs;
}

std::vector<Sample> loadWavFile(const std::string& path, int* sample_rate, int* channels) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << path << std::endl;
        return {};
    }

    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        std::cerr << "Invalid WAV file: " << path << std::endl;
        return {};
    }

    *sample_rate = header.sample_rate;
    *channels = header.num_channels;

    int num_samples = header.data_size / (header.bits_per_sample / 8);
    std::vector<Sample> samples(num_samples);

    if (header.bits_per_sample == 16) {
        std::vector<int16_t> raw_samples(num_samples);
        file.read(reinterpret_cast<char*>(raw_samples.data()), header.data_size);
        for (int i = 0; i < num_samples; ++i) {
            samples[i] = raw_samples[i] / 32768.0;
        }
    } else if (header.bits_per_sample == 32) {
        std::vector<float> raw_samples(num_samples);
        file.read(reinterpret_cast<char*>(raw_samples.data()), header.data_size);
        for (int i = 0; i < num_samples; ++i) {
            samples[i] = raw_samples[i];
        }
    }

    return samples;
}

std::vector<Sample> generateCafeNoise(int num_samples, int sample_rate, double level) {
    std::vector<Sample> noise(num_samples);
    std::mt19937 rng(42);
    std::normal_distribution<> gaussian(0, level);

    std::vector<double> band_gains = {
        0.8, 1.2, 1.0, 0.7, 0.5, 0.3, 0.2, 0.1
    };

    for (int i = 0; i < num_samples; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        double sample = 0.0;

        sample += gaussian(rng) * 0.5;

        double mod = 0.3 * std::sin(2.0 * M_PI * 2.0 * t);
        sample += gaussian(rng) * mod;

        for (int b = 0; b < 8; ++b) {
            double freq = 100.0 * (b + 1);
            sample += band_gains[b] * level * 0.1 *
                     std::sin(2.0 * M_PI * freq * t + gaussian(rng));
        }

        if (i % static_cast<int>(sample_rate * 0.1) == 0) {
            double burst = gaussian(rng) * level * 2.0;
            for (int j = 0; j < 100 && (i + j) < num_samples; ++j) {
                noise[i + j] += burst * std::exp(-j / 50.0);
            }
        }

        noise[i] = sample * 0.3;
    }

    return noise;
}

std::vector<Sample> generateStreetNoise(int num_samples, int sample_rate, double level) {
    std::vector<Sample> noise(num_samples);
    std::mt19937 rng(123);
    std::normal_distribution<> gaussian(0, level);

    for (int i = 0; i < num_samples; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        double sample = 0.0;

        sample += gaussian(rng) * 0.6;

        sample += 0.3 * level * std::sin(2.0 * M_PI * 80.0 * t);
        sample += 0.2 * level * std::sin(2.0 * M_PI * 120.0 * t);
        sample += 0.15 * level * std::sin(2.0 * M_PI * 160.0 * t);

        double horn_prob = 0.0001;
        if (gaussian(rng) > 3.0) {
            horn_prob = 0.01;
        }
        if (static_cast<double>(rng()) / rng.max() < horn_prob) {
            double horn_freq = 300.0 + gaussian(rng) * 100.0;
            for (int j = 0; j < 500 && (i + j) < num_samples; ++j) {
                double ht = static_cast<double>(j) / sample_rate;
                noise[i + j] += level * 0.8 * std::sin(2.0 * M_PI * horn_freq * ht) *
                               std::exp(-ht * 5.0);
            }
        }

        noise[i] = sample * 0.4;
    }

    return noise;
}

std::vector<Sample> generateOfficeNoise(int num_samples, int sample_rate, double level) {
    std::vector<Sample> noise(num_samples);
    std::mt19937 rng(456);
    std::normal_distribution<> gaussian(0, level);

    for (int i = 0; i < num_samples; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        double sample = 0.0;

        sample += gaussian(rng) * 0.3;

        sample += 0.1 * level * std::sin(2.0 * M_PI * 60.0 * t);
        sample += 0.08 * level * std::sin(2.0 * M_PI * 120.0 * t);
        sample += 0.05 * level * std::sin(2.0 * M_PI * 180.0 * t);

        if (i % static_cast<int>(sample_rate * 2.0) == 0) {
            double key_click = level * 0.15;
            for (int j = 0; j < 20 && (i + j) < num_samples; ++j) {
                noise[i + j] += key_click * std::exp(-j / 10.0);
            }
        }

        noise[i] = sample * 0.2;
    }

    return noise;
}

void addNoiseToSignal(std::vector<Sample>& signal, const std::vector<Sample>& noise, double snr_db) {
    double signal_power = 0.0;
    for (const auto& s : signal) {
        signal_power += s * s;
    }
    signal_power /= signal.size();

    double noise_power = 0.0;
    for (const auto& n : noise) {
        noise_power += n * n;
    }
    noise_power /= noise.size();

    if (noise_power < 1e-10) return;

    double snr_linear = std::pow(10.0, snr_db / 10.0);
    double scale = std::sqrt(signal_power / (noise_power * snr_linear));

    int len = std::min(signal.size(), noise.size());
    for (int i = 0; i < len; ++i) {
        signal[i] += noise[i] * scale;
    }
}

std::vector<Sample> generateSyntheticSamples(double frequency, double duration,
                                              int sample_rate, NoiseProfile noise_profile,
                                              double snr_db) {
    int num_samples = static_cast<int>(sample_rate * duration);
    std::vector<Sample> samples(num_samples);

    std::mt19937 rng(static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::normal_distribution<> noise(0, 0.02);

    for (int i = 0; i < num_samples; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        double envelope = std::sin(M_PI * t / duration);
        envelope = 0.3 + 0.7 * envelope;

        double sample = 0.0;
        sample += 0.35 * std::sin(2.0 * M_PI * frequency * t);
        sample += 0.18 * std::sin(2.0 * M_PI * frequency * 2.0 * t);
        sample += 0.10 * std::sin(2.0 * M_PI * frequency * 3.0 * t);
        sample += 0.08 * std::sin(2.0 * M_PI * frequency * 4.0 * t);
        sample += 0.05 * std::sin(2.0 * M_PI * frequency * 0.5 * t);
        sample += noise(rng) * 0.5;

        double vibrato = 1.0 + 0.01 * std::sin(2.0 * M_PI * 5.0 * t);
        sample *= vibrato;

        samples[i] = sample * envelope;
    }

    if (noise_profile != NOISE_CLEAN) {
        std::vector<Sample> noise_samples;
        double noise_level = 0.1;

        switch (noise_profile) {
            case NOISE_CAFE:
                noise_samples = generateCafeNoise(num_samples, sample_rate, noise_level);
                break;
            case NOISE_STREET:
                noise_samples = generateStreetNoise(num_samples, sample_rate, noise_level);
                break;
            case NOISE_OFFICE:
                noise_samples = generateOfficeNoise(num_samples, sample_rate, noise_level);
                break;
            default:
                break;
        }

        if (!noise_samples.empty()) {
            addNoiseToSignal(samples, noise_samples, snr_db);
        }
    }

    return samples;
}

std::vector<DbFingerprint> extractFingerprintsFromSamples(
    const std::vector<Sample>& samples,
    int sample_rate,
    int channels,
    bool simulate_noisy = false) {

    std::vector<DbFingerprint> fingerprints;

    int window_size = DB_FINGERPRINT_WINDOW;
    int hop_size = DB_FINGERPRINT_HOP;

    Preprocessor preprocessor(sample_rate);
    FingerprintExtractor extractor(window_size);

    if (simulate_noisy) {
        NoiseEstimator& noise_est = extractor.getNoiseEstimator();
        noise_est.init(extractor.getNumBins());

        int noise_frames = std::min(
            static_cast<int>(NOISE_ESTIMATION_DURATION * sample_rate / window_size),
            static_cast<int>(samples.size() / window_size));

        Frame frame(window_size);
        Frame processed(window_size);

        for (int fi = 0; fi < noise_frames; ++fi) {
            for (int i = 0; i < window_size; ++i) {
                frame[i] = samples[fi * hop_size + i];
            }
            preprocessor.process(frame, processed);
            extractor.extractPeaksForNoiseEstimation(processed, fi);
        }

        noise_est.finalize();
        extractor.setNoiseEstimator(&noise_est);
    }

    Frame frame(window_size);
    Frame processed(window_size);
    int frame_index = 0;

    for (size_t start = 0; start + window_size <= samples.size(); start += hop_size) {
        for (int i = 0; i < window_size; ++i) {
            frame[i] = samples[start + i];
        }

        preprocessor.process(frame, processed);

        auto peaks = extractor.extractPeaks(processed, frame_index, simulate_noisy);
        auto hashes = extractor.generateHashes(peaks);

        for (const auto& h : hashes) {
            DbFingerprint fp;
            fp.hash = h.hash;
            fp.time_offset = h.time_delta;
            fp.song_id = 0;
            fingerprints.push_back(fp);
        }

        frame_index++;
    }

    return fingerprints;
}

BuildOptions parseArgs(int argc, char* argv[]) {
    BuildOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            opts.db_path = argv[++i];
        } else if (arg == "--wav-dir" && i + 1 < argc) {
            opts.wav_dir = argv[++i];
            opts.use_synthetic = false;
        } else if (arg == "--noise" && i + 1 < argc) {
            std::string profile = argv[++i];
            if (profile == "cafe") {
                opts.noise_profile = NOISE_CAFE;
                opts.simulate_noisy_conditions = true;
            } else if (profile == "street") {
                opts.noise_profile = NOISE_STREET;
                opts.simulate_noisy_conditions = true;
            } else if (profile == "office") {
                opts.noise_profile = NOISE_OFFICE;
                opts.simulate_noisy_conditions = true;
            }
        } else if (arg == "--snr" && i + 1 < argc) {
            opts.noise_level = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Audio Fingerprint Database Builder\n";
            std::cout << "===================================\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --db <path>       Database file path (default: fingerprints.db)\n";
            std::cout << "  --wav-dir <path>  Directory containing WAV files\n";
            std::cout << "  --noise <type>    Simulate noise conditions (cafe/street/office)\n";
            std::cout << "  --snr <db>        Signal-to-noise ratio (default: 10)\n";
            std::cout << "  --help, -h        Show this help\n";
            std::cout << "\nNoise simulation applies spectral subtraction during\n";
            std::cout << "fingerprint extraction to improve robustness.\n";
            std::exit(0);
        }
    }

    return opts;
}

int main(int argc, char* argv[]) {
    BuildOptions opts = parseArgs(argc, argv);

    std::cout << "Audio Fingerprint Database Builder (v2.0)\n";
    std::cout << "==========================================\n\n";

    if (opts.simulate_noisy_conditions) {
        std::string noise_names[] = {"Clean", "Cafe", "Street", "Office", "Car"};
        std::cout << "Noise simulation: " << noise_names[opts.noise_profile] << "\n";
        std::cout << "Target SNR: " << (opts.noise_level > 0 ? opts.noise_level : 10.0) << " dB\n\n";
    }

    Database db;
    if (!db.open(opts.db_path)) {
        std::cerr << "Failed to open database\n";
        return 1;
    }

    if (!db.createTables()) {
        std::cerr << "Failed to create tables\n";
        return 1;
    }

    auto songs = generateSongList();
    std::cout << "Processing " << songs.size() << " songs...\n\n";

    int success_count = 0;
    int total_fingerprints = 0;

    for (size_t i = 0; i < songs.size(); ++i) {
        const auto& song = songs[i];

        std::vector<DbFingerprint> fingerprints;

        if (!opts.use_synthetic && !opts.wav_dir.empty()) {
            std::string wav_path = opts.wav_dir + "/" + song.filename;
            int sr = 0, ch = 0;
            auto samples = loadWavFile(wav_path, &sr, &ch);
            if (!samples.empty()) {
                fingerprints = extractFingerprintsFromSamples(
                    samples, sr, ch, opts.simulate_noisy_conditions);
            }
        }

        if (fingerprints.empty()) {
            double base_freq = 110.0 + (i * 13.75);
            double duration = 30.0;
            double snr = opts.noise_level > 0 ? opts.noise_level : 10.0;

            auto samples = generateSyntheticSamples(
                base_freq, duration, SAMPLE_RATE,
                opts.noise_profile, snr);
            fingerprints = extractFingerprintsFromSamples(
                samples, SAMPLE_RATE, 1, opts.simulate_noisy_conditions);
        }

        SongInfo info;
        info.title = song.title;
        info.artist = song.artist;
        info.album = song.album;
        info.duration = 30.0;

        int song_id = 0;
        if (db.addSong(info, &song_id)) {
            for (auto& fp : fingerprints) {
                fp.song_id = song_id;
            }

            if (db.addFingerprints(song_id, fingerprints)) {
                std::cout << "[" << (i + 1) << "/" << songs.size() << "] "
                         << info.title << " - " << info.artist
                         << " (" << fingerprints.size() << " fingerprints)\n";
                success_count++;
                total_fingerprints += static_cast<int>(fingerprints.size());
            } else {
                std::cerr << "Failed to add fingerprints for: " << info.title << "\n";
            }
        } else {
            std::cerr << "Failed to add song: " << info.title << "\n";
        }
    }

    std::cout << "\n============================================\n";
    std::cout << "Database built successfully!\n";
    std::cout << "Songs added: " << success_count << " / " << songs.size() << "\n";
    std::cout << "Total fingerprints: " << total_fingerprints << "\n";
    std::cout << "Database file: " << opts.db_path << "\n";
    if (opts.simulate_noisy_conditions) {
        std::cout << "Note: Fingerprints extracted with noise simulation\n";
    }

    db.close();
    return 0;
}
