#ifndef EDSR_HLS_H_
#define EDSR_HLS_H_

#include <ap_int.h>
#include <ap_fixed.h>
#include <hls_stream.h>

#define INPUT_H 720
#define INPUT_W 1280
#define OUTPUT_H 1080
#define OUTPUT_W 1920
#define CHANNELS 3
#define FEATURES 64
#define RES_BLOCKS 8
#define SCALE 2
#define KERNEL 3
#define PADDING 1

#define INPUT_BATCH 1
#define PE_ARRAY 16

#define TEMPORAL_FRAMES 3
#define TEMPORAL_KERNEL 3

typedef ap_int<8> weight_t;
typedef ap_int<8> act_int_t;
typedef ap_int<32> acc_t;
typedef ap_fixed<16, 8> act_fixed_t;

typedef struct {
    ap_uint<8> data;
    ap_uint<1> last;
} axis_t;

struct ConvParams {
    int in_c;
    int out_c;
    int kH;
    int kW;
    int stride;
    int pad;
    float scale;
    int zp;
};

struct Conv3DParams {
    int in_c;
    int out_c;
    int kT;
    int kH;
    int kW;
    int stride_t;
    int stride_h;
    int stride_w;
    int pad_t;
    int pad_h;
    int pad_w;
    float scale;
    int zp;
};

void edsr_top(hls::stream<axis_t>& input_stream,
              hls::stream<axis_t>& output_stream,
              weight_t weights_input[3*64*3*3],
              weight_t weights_res1[8][64*64*3*3],
              weight_t weights_res2[8][64*64*3*3],
              weight_t weights_mid[64*64*3*3],
              weight_t weights_up[64*256*3*3],
              weight_t weights_out[64*3*3*3],
              weight_t weights_temporal[3*3*3*3*3],
              int input_h, int input_w,
              ap_uint<1> enable_mc);

void edsr_top_mc(hls::stream<axis_t>& input_stream,
                 hls::stream<axis_t>& prev_stream,
                 hls::stream<axis_t>& next_stream,
                 hls::stream<axis_t>& output_stream,
                 weight_t weights_input[3*64*3*3],
                 weight_t weights_res1[8][64*64*3*3],
                 weight_t weights_res2[8][64*64*3*3],
                 weight_t weights_mid[64*64*3*3],
                 weight_t weights_up[64*256*3*3],
                 weight_t weights_out[64*3*3*3],
                 weight_t weights_temporal[3*3*3*3*3],
                 int input_h, int input_w);

void conv3d_int8(act_int_t input[TEMPORAL_FRAMES][CHANNELS][INPUT_H][INPUT_W],
                 act_int_t output[CHANNELS][INPUT_H][INPUT_W],
                 weight_t weights[3*3*3*3*3],
                 Conv3DParams params);

void conv2d_int8(act_int_t input[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                 act_int_t output[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                 weight_t weights[FEATURES*FEATURES*3*3],
                 ConvParams params);

void residual_block_int8(act_int_t input[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                         act_int_t output[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                         weight_t conv1[FEATURES*FEATURES*3*3],
                         weight_t conv2[FEATURES*FEATURES*3*3]);

void upsample_int8(act_int_t input[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                   act_int_t output[INPUT_BATCH][FEATURES][OUTPUT_H][OUTPUT_W],
                   weight_t weights[FEATURES*256*3*3]);

void input_conv_int8(act_int_t input[INPUT_BATCH][CHANNELS][INPUT_H][INPUT_W],
                     act_int_t output[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                     weight_t weights[3*64*3*3]);

void output_conv_int8(act_int_t input[INPUT_BATCH][FEATURES][OUTPUT_H][OUTPUT_W],
                      act_int_t output[INPUT_BATCH][CHANNELS][OUTPUT_H][OUTPUT_W],
                      weight_t weights[64*3*3*3]);

#endif