#include "edsr_hls.h"

#pragma hls_design top
void edsr_top(hls::stream<axis_t>& input_stream,
              hls::stream<axis_t>& output_stream,
              weight_t weights_input[3*64*3*3],
              weight_t weights_res1[8][64*64*3*3],
              weight_t weights_res2[8][64*64*3*3],
              weight_t weights_mid[64*64*3*3],
              weight_t weights_up[64*256*3*3],
              weight_t weights_out[64*3*3*3],
              int input_h, int input_w) {
#pragma HLS INTERFACE axis port=input_stream
#pragma HLS INTERFACE axis port=output_stream
#pragma HLS INTERFACE m_axi port=weights_input bundle=weight0 depth=576
#pragma HLS INTERFACE m_axi port=weights_res1 bundle=weight1 depth=294912
#pragma HLS INTERFACE m_axi port=weights_res2 bundle=weight2 depth=294912
#pragma HLS INTERFACE m_axi port=weights_mid bundle=weight3 depth=36864
#pragma HLS INTERFACE m_axi port=weights_up bundle=weight4 depth=147456
#pragma HLS INTERFACE m_axi port=weights_out bundle=weight5 depth=1728
#pragma HLS INTERFACE s_axilite port=input_h bundle=control
#pragma HLS INTERFACE s_axilite port=input_w bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    static act_int_t input_buf[INPUT_BATCH][CHANNELS][INPUT_H][INPUT_W];
    static act_int_t feat_buf[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t res_buf[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t mid_buf[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t up_buf[INPUT_BATCH][FEATURES][OUTPUT_H][OUTPUT_W];
    static act_int_t out_buf[INPUT_BATCH][CHANNELS][OUTPUT_H][OUTPUT_W];

#pragma HLS ARRAY_PARTITION variable=input_buf dim=2 complete
#pragma HLS ARRAY_PARTITION variable=feat_buf dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=res_buf dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=mid_buf dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=up_buf dim=2 cyclic factor=8
#pragma HLS ARRAY_PARTITION variable=out_buf dim=2 complete

    read_input:
    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int c = 0; c < CHANNELS; c++) {
            for (int h = 0; h < INPUT_H; h++) {
                for (int w = 0; w < INPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    axis_t elem = input_stream.read();
                    input_buf[b][c][h][w] = static_cast<act_int_t>(elem.data) - 128;
                }
            }
        }
    }

    ConvParams in_params = {3, FEATURES, 3, 3, 1, 1, 1.0f/127.0f, 0};
    input_conv_int8(input_buf, feat_buf, weights_input);

    for (int i = 0; i < INPUT_BATCH; i++) {
        for (int j = 0; j < FEATURES; j++) {
            for (int k = 0; k < INPUT_H; k++) {
                for (int l = 0; l < INPUT_W; l++) {
                    res_buf[i][j][k][l] = feat_buf[i][j][k][l];
                }
            }
        }
    }

    res_blocks:
    for (int rb = 0; rb < RES_BLOCKS; rb++) {
#pragma HLS UNROLL factor=2
        residual_block_int8(res_buf, res_buf,
                            weights_res1[rb], weights_res2[rb]);
    }

    ConvParams mid_params = {FEATURES, FEATURES, 3, 3, 1, 1, 1.0f/127.0f, 0};
    conv2d_int8(res_buf, mid_buf, weights_mid, mid_params);

    upsample_int8(mid_buf, up_buf, weights_up);

    ConvParams out_params = {FEATURES, CHANNELS, 3, 3, 1, 1, 1.0f/127.0f, 0};
    output_conv_int8(up_buf, out_buf, weights_out);

    write_output:
    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int c = 0; c < CHANNELS; c++) {
            for (int h = 0; h < OUTPUT_H; h++) {
                for (int w = 0; w < OUTPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    axis_t elem;
                    int val = static_cast<int>(out_buf[b][c][h][w]) + 128;
                    val = (val < 0) ? 0 : (val > 255) ? 255 : val;
                    elem.data = static_cast<ap_uint<8>>(val);
                    elem.last = (b == INPUT_BATCH - 1 && c == CHANNELS - 1 &&
                                 h == OUTPUT_H - 1 && w == OUTPUT_W - 1);
                    output_stream.write(elem);
                }
            }
        }
    }
}

void input_conv_int8(act_int_t input[INPUT_BATCH][CHANNELS][INPUT_H][INPUT_W],
                     act_int_t output[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                     weight_t weights[3*64*3*3]) {
#pragma HLS INLINE
    ConvParams params = {CHANNELS, FEATURES, 3, 3, 1, 1, 1.0f/127.0f, 0};
    conv2d_int8(reinterpret_cast<act_int_t (*)[FEATURES][INPUT_H][INPUT_W]>(input),
                output,
                reinterpret_cast<weight_t (*)>(weights),
                params);
}

void output_conv_int8(act_int_t input[INPUT_BATCH][FEATURES][OUTPUT_H][OUTPUT_W],
                      act_int_t output[INPUT_BATCH][CHANNELS][OUTPUT_H][OUTPUT_W],
                      weight_t weights[64*3*3*3]) {
#pragma HLS INLINE
    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int oc = 0; oc < CHANNELS; oc++) {
            for (int oh = 0; oh < OUTPUT_H; oh++) {
                for (int ow = 0; ow < OUTPUT_W; ow++) {
#pragma HLS PIPELINE II=1
                    acc_t sum = 0;
                    for (int ic = 0; ic < FEATURES; ic++) {
                        for (int kh = 0; kh < 3; kh++) {
                            for (int kw = 0; kw < 3; kw++) {
                                int ih = oh + kh - 1;
                                int iw = ow + kw - 1;
                                if (ih >= 0 && ih < OUTPUT_H && iw >= 0 && iw < OUTPUT_W) {
                                    int in_val = static_cast<int>(input[b][ic][ih][iw]);
                                    int w_idx = ((oc * FEATURES + ic) * 3 + kh) * 3 + kw;
                                    int w_val = static_cast<int>(weights[w_idx]);
                                    sum += in_val * w_val;
                                }
                            }
                        }
                    }
                    float deq = static_cast<float>(sum) * (1.0f / (127.0f * 127.0f));
                    deq = (deq < -1.0f) ? -1.0f : (deq > 1.0f) ? 1.0f : deq;
                    output[b][oc][oh][ow] = static_cast<act_int_t>(deq * 127.0f);
                }
            }
        }
    }
}

void residual_block_int8(act_int_t input[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                         act_int_t output[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                         weight_t conv1[FEATURES*FEATURES*3*3],
                         weight_t conv2[FEATURES*FEATURES*3*3]) {
#pragma HLS INLINE
    static act_int_t temp[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t temp2[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
#pragma HLS ARRAY_PARTITION variable=temp dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=temp2 dim=2 cyclic factor=16

    ConvParams params = {FEATURES, FEATURES, 3, 3, 1, 1, 1.0f/127.0f, 0};
    conv2d_int8(input, temp, conv1, params);

    relu:
    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int c = 0; c < FEATURES; c++) {
            for (int h = 0; h < INPUT_H; h++) {
                for (int w = 0; w < INPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    temp[b][c][h][w] = (temp[b][c][h][w] > 0) ? temp[b][c][h][w] : 0;
                }
            }
        }
    }

    conv2d_int8(temp, temp2, conv2, params);

    residual_add:
    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int c = 0; c < FEATURES; c++) {
            for (int h = 0; h < INPUT_H; h++) {
                for (int w = 0; w < INPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    int sum = static_cast<int>(input[b][c][h][w]) +
                              static_cast<int>(temp2[b][c][h][w]);
                    sum = (sum < -128) ? -128 : (sum > 127) ? 127 : sum;
                    output[b][c][h][w] = static_cast<act_int_t>(sum);
                }
            }
        }
    }
}

void conv2d_int8(act_int_t input[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                 act_int_t output[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                 weight_t weights[FEATURES*FEATURES*3*3],
                 ConvParams params) {
#pragma HLS INLINE off
    for (int b = 0; b < INPUT_BATCH; b++) {
        conv_outer:
        for (int oc = 0; oc < FEATURES; oc++) {
#pragma HLS UNROLL factor=16
            for (int oh = 0; oh < INPUT_H; oh++) {
                for (int ow = 0; ow < INPUT_W; ow++) {
#pragma HLS PIPELINE II=1
                    acc_t sum = 0;
                    for (int ic = 0; ic < FEATURES; ic++) {
#pragma HLS UNROLL factor=4
                        for (int kh = 0; kh < 3; kh++) {
                            for (int kw = 0; kw < 3; kw++) {
                                int ih = oh + kh - 1;
                                int iw = ow + kw - 1;
                                if (ih >= 0 && ih < INPUT_H && iw >= 0 && iw < INPUT_W) {
                                    int in_val = static_cast<int>(input[b][ic][ih][iw]);
                                    int w_idx = ((oc * FEATURES + ic) * 3 + kh) * 3 + kw;
                                    int w_val = static_cast<int>(weights[w_idx]);
                                    sum += in_val * w_val;
                                }
                            }
                        }
                    }
                    float deq = static_cast<float>(sum) * params.scale * (1.0f / 127.0f);
                    deq = (deq < -1.0f) ? -1.0f : (deq > 1.0f) ? 1.0f : deq;
                    output[b][oc][oh][ow] = static_cast<act_int_t>(deq * 127.0f);
                }
            }
        }
    }
}

void upsample_int8(act_int_t input[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W],
                   act_int_t output[INPUT_BATCH][FEATURES][OUTPUT_H][OUTPUT_W],
                   weight_t weights[FEATURES*256*3*3]) {
#pragma HLS INLINE
    static act_int_t up_conv[INPUT_BATCH][FEATURES*4][INPUT_H][INPUT_W];
#pragma HLS ARRAY_PARTITION variable=up_conv dim=2 cyclic factor=16

    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int oc = 0; oc < FEATURES * 4; oc++) {
#pragma HLS UNROLL factor=8
            for (int oh = 0; oh < INPUT_H; oh++) {
                for (int ow = 0; ow < INPUT_W; ow++) {
#pragma HLS PIPELINE II=1
                    acc_t sum = 0;
                    for (int ic = 0; ic < FEATURES; ic++) {
                        for (int kh = 0; kh < 3; kh++) {
                            for (int kw = 0; kw < 3; kw++) {
                                int ih = oh + kh - 1;
                                int iw = ow + kw - 1;
                                if (ih >= 0 && ih < INPUT_H && iw >= 0 && iw < INPUT_W) {
                                    int in_val = static_cast<int>(input[b][ic][ih][iw]);
                                    int w_idx = ((oc * FEATURES + ic) * 3 + kh) * 3 + kw;
                                    int w_val = static_cast<int>(weights[w_idx]);
                                    sum += in_val * w_val;
                                }
                            }
                        }
                    }
                    float deq = static_cast<float>(sum) * (1.0f / (127.0f * 127.0f));
                    deq = (deq < -1.0f) ? -1.0f : (deq > 1.0f) ? 1.0f : deq;
                    up_conv[b][oc][oh][ow] = static_cast<act_int_t>(deq * 127.0f);
                }
            }
        }
    }

    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int c = 0; c < FEATURES; c++) {
            for (int ih = 0; ih < INPUT_H; ih++) {
                for (int iw = 0; iw < INPUT_W; iw++) {
                    for (int sh = 0; sh < SCALE; sh++) {
                        for (int sw = 0; sw < SCALE; sw++) {
#pragma HLS PIPELINE II=1
                            int oh = ih * SCALE + sh;
                            int ow = iw * SCALE + sw;
                            int sub_c = c * SCALE * SCALE + sh * SCALE + sw;
                            output[b][c][oh][ow] = up_conv[b][sub_c][ih][iw];
                        }
                    }
                }
            }
        }
    }
}

void conv3d_int8(act_int_t input[TEMPORAL_FRAMES][CHANNELS][INPUT_H][INPUT_W],
                 act_int_t output[CHANNELS][INPUT_H][INPUT_W],
                 weight_t weights[3*3*3*3*3],
                 Conv3DParams params) {
#pragma HLS INLINE off
    int tK = params.kT;
    int hK = params.kH;
    int wK = params.kW;
    int pad_t = params.pad_t;
    int pad_h = params.pad_h;
    int pad_w = params.pad_w;
    int out_C = params.out_c;

    conv3d_outer:
    for (int oc = 0; oc < out_C; oc++) {
#pragma HLS UNROLL factor=3
        for (int t = 0; t < TEMPORAL_FRAMES; t++) {
            for (int h = 0; h < INPUT_H; h++) {
                for (int w = 0; w < INPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    acc_t sum = 0;
                    for (int ic = 0; ic < CHANNELS; ic++) {
#pragma HLS UNROLL factor=3
                        for (int kt = 0; kt < tK; kt++) {
                            for (int kh = 0; kh < hK; kh++) {
                                for (int kw = 0; kw < wK; kw++) {
                                    int it = t + kt - pad_t;
                                    int ih = h + kh - pad_h;
                                    int iw = w + kw - pad_w;
                                    if (it >= 0 && it < TEMPORAL_FRAMES &&
                                        ih >= 0 && ih < INPUT_H &&
                                        iw >= 0 && iw < INPUT_W) {
                                        int32_t in_val = static_cast<int32_t>(
                                            input[it][ic][ih][iw]);
                                        int32_t w_idx = ((((oc * CHANNELS + ic) * tK + kt) * hK + kh) * wK + kw);
                                        int32_t w_val = static_cast<int32_t>(weights[w_idx]);
                                        sum += in_val * w_val;
                                    }
                                }
                            }
                        }
                    }
                    float deq = static_cast<float>(sum) * params.scale;
                    deq = (deq < -1.0f) ? -1.0f : (deq > 1.0f) ? 1.0f : deq;
                    output[oc][h][w] = static_cast<act_int_t>(deq * 127.0f);
                }
            }
        }
    }
}

#pragma hls_design top
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
                 int input_h, int input_w) {
#pragma HLS INTERFACE axis port=input_stream
#pragma HLS INTERFACE axis port=prev_stream
#pragma HLS INTERFACE axis port=next_stream
#pragma HLS INTERFACE axis port=output_stream
#pragma HLS INTERFACE m_axi port=weights_input bundle=weight0 depth=576
#pragma HLS INTERFACE m_axi port=weights_res1 bundle=weight1 depth=294912
#pragma HLS INTERFACE m_axi port=weights_res2 bundle=weight2 depth=294912
#pragma HLS INTERFACE m_axi port=weights_mid bundle=weight3 depth=36864
#pragma HLS INTERFACE m_axi port=weights_up bundle=weight4 depth=147456
#pragma HLS INTERFACE m_axi port=weights_out bundle=weight5 depth=1728
#pragma HLS INTERFACE m_axi port=weights_temporal bundle=weight6 depth=243
#pragma HLS INTERFACE s_axilite port=input_h bundle=control
#pragma HLS INTERFACE s_axilite port=input_w bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    static act_int_t temporal_buf[TEMPORAL_FRAMES][CHANNELS][INPUT_H][INPUT_W];
    static act_int_t fused_buf[CHANNELS][INPUT_H][INPUT_W];
    static act_int_t feat_buf[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t res_buf[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t mid_buf[INPUT_BATCH][FEATURES][INPUT_H][INPUT_W];
    static act_int_t up_buf[INPUT_BATCH][FEATURES][OUTPUT_H][OUTPUT_W];
    static act_int_t out_buf[INPUT_BATCH][CHANNELS][OUTPUT_H][OUTPUT_W];

#pragma HLS ARRAY_PARTITION variable=temporal_buf dim=1 complete
#pragma HLS ARRAY_PARTITION variable=feat_buf dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=res_buf dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=mid_buf dim=2 cyclic factor=16
#pragma HLS ARRAY_PARTITION variable=up_buf dim=2 cyclic factor=8

    read_temporal:
    for (int t = 0; t < TEMPORAL_FRAMES; t++) {
        for (int c = 0; c < CHANNELS; c++) {
            for (int h = 0; h < INPUT_H; h++) {
                for (int w = 0; w < INPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    axis_t elem;
                    if (t == 0) elem = prev_stream.read();
                    else if (t == 1) elem = input_stream.read();
                    else elem = next_stream.read();
                    temporal_buf[t][c][h][w] = static_cast<act_int_t>(elem.data) - 128;
                }
            }
        }
    }

    Conv3DParams temporal_params = {CHANNELS, CHANNELS, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1.0f/(127.0f*3.0f), 0};
    conv3d_int8(temporal_buf, fused_buf, weights_temporal, temporal_params);

    for (int c = 0; c < CHANNELS; c++) {
        for (int h = 0; h < INPUT_H; h++) {
            for (int w = 0; w < INPUT_W; w++) {
#pragma HLS PIPELINE II=1
                feat_buf[0][c][h][w] = fused_buf[c][h][w];
            }
        }
    }

    ConvParams in_params = {CHANNELS, FEATURES, 3, 3, 1, 1, 1.0f/127.0f, 0};
    input_conv_int8(reinterpret_cast<act_int_t (*)[FEATURES][INPUT_H][INPUT_W]>(feat_buf),
                    feat_buf, weights_input);

    res_blocks:
    for (int rb = 0; rb < RES_BLOCKS; rb++) {
#pragma HLS UNROLL factor=2
        residual_block_int8(res_buf, res_buf,
                            weights_res1[rb], weights_res2[rb]);
    }

    ConvParams mid_params = {FEATURES, FEATURES, 3, 3, 1, 1, 1.0f/127.0f, 0};
    conv2d_int8(res_buf, mid_buf, weights_mid, mid_params);

    upsample_int8(mid_buf, up_buf, weights_up);

    ConvParams out_params = {FEATURES, CHANNELS, 3, 3, 1, 1, 1.0f/127.0f, 0};
    output_conv_int8(up_buf, out_buf, weights_out);

    write_output:
    for (int b = 0; b < INPUT_BATCH; b++) {
        for (int c = 0; c < CHANNELS; c++) {
            for (int h = 0; h < OUTPUT_H; h++) {
                for (int w = 0; w < OUTPUT_W; w++) {
#pragma HLS PIPELINE II=1
                    axis_t elem;
                    int val = static_cast<int>(out_buf[b][c][h][w]) + 128;
                    val = (val < 0) ? 0 : (val > 255) ? 255 : val;
                    elem.data = static_cast<ap_uint<8>>(val);
                    elem.last = (b == INPUT_BATCH - 1 && c == CHANNELS - 1 &&
                                 h == OUTPUT_H - 1 && w == OUTPUT_W - 1);
                    output_stream.write(elem);
                }
            }
        }
    }
}