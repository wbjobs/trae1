#include "edsr_hls.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <hls_stream.h>

void generate_test_input(hls::stream<axis_t>& input_stream,
                          int h, int w, int c) {
    for (int i = 0; i < h * w * c; ++i) {
        axis_t elem;
        elem.data = static_cast<ap_uint<8>>(rand() % 256);
        elem.last = (i == h * w * c - 1);
        input_stream.write(elem);
    }
}

int read_output_stream(hls::stream<axis_t>& output_stream,
                        ap_uint<8>* output, int size) {
    int count = 0;
    for (int i = 0; i < size; ++i) {
        if (output_stream.empty()) {
            std::cout << "ERROR: Output stream empty at " << i << "\n";
            return count;
        }
        axis_t elem = output_stream.read();
        output[i] = elem.data;
        count++;
        if (elem.last) {
            std::cout << "LAST signal received at " << i << "\n";
            break;
        }
    }
    return count;
}

int main() {
    std::cout << "=== EDSR HLS C-Simulation Test ===\n";

    hls::stream<axis_t> input_stream("input_stream");
    hls::stream<axis_t> output_stream("output_stream");

    weight_t* weights_input = new weight_t[3*64*3*3];
    weight_t (*weights_res1)[8][64*64*3*3] = new weight_t[1][8][64*64*3*3];
    weight_t (*weights_res2)[8][64*64*3*3] = new weight_t[1][8][64*64*3*3];
    weight_t* weights_mid = new weight_t[64*64*3*3];
    weight_t* weights_up = new weight_t[64*256*3*3];
    weight_t* weights_out = new weight_t[64*3*3*3];

    for (int i = 0; i < 3*64*3*3; ++i) weights_input[i] = (rand() % 255) - 128;
    for (int rb = 0; rb < 8; ++rb) {
        for (int i = 0; i < 64*64*3*3; ++i) {
            (*weights_res1)[rb][i] = (rand() % 255) - 128;
            (*weights_res2)[rb][i] = (rand() % 255) - 128;
        }
    }
    for (int i = 0; i < 64*64*3*3; ++i) weights_mid[i] = (rand() % 255) - 128;
    for (int i = 0; i < 64*256*3*3; ++i) weights_up[i] = (rand() % 255) - 128;
    for (int i = 0; i < 64*3*3*3; ++i) weights_out[i] = (rand() % 255) - 128;

    int test_h = 180;
    int test_w = 320;

    std::cout << "Generating test input...\n";
    generate_test_input(input_stream, test_h, test_w, 3);

    std::cout << "Running EDSR HLS top...\n";
    edsr_top(input_stream, output_stream,
             weights_input,
             *weights_res1,
             *weights_res2,
             weights_mid,
             weights_up,
             weights_out,
             test_h, test_w);

    int output_size = test_h * 2 * test_w * 2 * 3;
    auto* output_data = new ap_uint<8>[output_size];

    std::cout << "Reading output stream...\n";
    int received = read_output_stream(output_stream, output_data, output_size);

    std::cout << "Received " << received << " of " << output_size << " bytes\n";

    if (received > 0) {
        std::cout << "Test PASSED\n";
        std::cout << "First 10 output values: ";
        for (int i = 0; i < std::min(10, received); ++i) {
            std::cout << static_cast<int>(output_data[i]) << " ";
        }
        std::cout << "\n";
    } else {
        std::cout << "Test FAILED\n";
    }

    delete[] weights_input;
    delete[] weights_res1;
    delete[] weights_res2;
    delete[] weights_mid;
    delete[] weights_up;
    delete[] weights_out;
    delete[] output_data;

    return (received > 0) ? 0 : 1;
}