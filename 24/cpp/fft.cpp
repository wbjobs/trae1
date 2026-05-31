#include <emscripten.h>
#include <emscripten/bind.h>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstring>

using namespace emscripten;

class FFTCalculator {
private:
    std::vector<std::complex<double>> twiddleFactors;
    int fftSize;

    void precomputeTwiddleFactors(int n) {
        if (n <= 0 || (n & (n - 1)) != 0) {
            emscripten_log(EM_LOG_WARN, "FFTCalculator: Invalid size %d, must be power of 2", n);
            n = 512;
        }
        
        twiddleFactors.resize(n / 2);
        for (int i = 0; i < n / 2; i++) {
            double angle = -2.0 * M_PI * i / n;
            twiddleFactors[i] = std::complex<double>(cos(angle), sin(angle));
        }
    }

    void bitReversePermutation(std::vector<std::complex<double>>& data) {
        int n = data.size();
        if (n <= 1) return;
        
        int j = 0;
        for (int i = 1; i < n; i++) {
            int bit = n >> 1;
            while (j >= bit && bit > 0) {
                j -= bit;
                bit >>= 1;
            }
            j += bit;
            
            if (i < j) {
                std::swap(data[i], data[j]);
            }
        }
    }

    bool validateInput(const std::vector<double>& input) {
        if (input.empty()) {
            emscripten_log(EM_LOG_WARN, "FFTCalculator: Empty input");
            return false;
        }
        
        for (size_t i = 0; i < input.size(); i++) {
            if (!std::isfinite(input[i])) {
                emscripten_log(EM_LOG_WARN, "FFTCalculator: Non-finite value at index %zu", i);
                return false;
            }
        }
        
        return true;
    }

    bool validateSize(int size) {
        if (size <= 0) {
            emscripten_log(EM_LOG_WARN, "FFTCalculator: Size must be positive");
            return false;
        }
        
        if ((size & (size - 1)) != 0) {
            emscripten_log(EM_LOG_WARN, "FFTCalculator: Size %d is not power of 2", size);
            return false;
        }
        
        return true;
    }

public:
    FFTCalculator(int size) : fftSize(size) {
        if (!validateSize(size)) {
            fftSize = 512;
        }
        precomputeTwiddleFactors(fftSize);
    }

    void setSize(int size) {
        if (!validateSize(size)) {
            emscripten_log(EM_LOG_WARN, "FFTCalculator: Keeping current size %d", fftSize);
            return;
        }
        
        fftSize = size;
        precomputeTwiddleFactors(fftSize);
    }

    int getSize() const {
        return fftSize;
    }

    std::vector<double> compute(const std::vector<double>& input) {
        if (!validateInput(input)) {
            std::vector<double> emptyResult(fftSize / 2, 0.0);
            return emptyResult;
        }

        int n = fftSize;
        std::vector<std::complex<double>> complexData(n, std::complex<double>(0.0, 0.0));

        int copyLen = std::min(n, static_cast<int>(input.size()));
        for (int i = 0; i < copyLen; i++) {
            complexData[i] = std::complex<double>(input[i], 0.0);
        }

        bitReversePermutation(complexData);

        for (int len = 2; len <= n; len *= 2) {
            int halfLen = len / 2;
            double angle = -2.0 * M_PI / len;
            std::complex<double> wlen(cos(angle), sin(angle));

            for (int i = 0; i < n; i += len) {
                std::complex<double> w(1.0, 0.0);
                for (int j = 0; j < halfLen; j++) {
                    if (i + j >= n || i + j + halfLen >= n) {
                        emscripten_log(EM_LOG_ERROR, 
                            "FFTCalculator: Index out of bounds - i=%d, j=%d, halfLen=%d, n=%d",
                            i, j, halfLen, n);
                        continue;
                    }
                    
                    std::complex<double> u = complexData[i + j];
                    std::complex<double> v = complexData[i + j + halfLen] * w;
                    complexData[i + j] = u + v;
                    complexData[i + j + halfLen] = u - v;
                    w *= wlen;
                }
            }
        }

        std::vector<double> magnitudes(n / 2);
        for (int i = 0; i < n / 2; i++) {
            double real = complexData[i].real();
            double imag = complexData[i].imag();
            double magnitude = sqrt(real * real + imag * imag);
            
            if (!std::isfinite(magnitude)) {
                emscripten_log(EM_LOG_WARN, "FFTCalculator: Non-finite magnitude at bin %d", i);
                magnitudes[i] = 0.0;
            } else {
                magnitudes[i] = magnitude;
            }
        }

        return magnitudes;
    }
};

EMSCRIPTEN_BINDINGS(fft_module) {
    class_<FFTCalculator>("FFTCalculator")
        .constructor<int>()
        .function("compute", &FFTCalculator::compute)
        .function("setSize", &FFTCalculator::setSize)
        .function("getSize", &FFTCalculator::getSize);

    register_vector<double>("VectorDouble");
}
