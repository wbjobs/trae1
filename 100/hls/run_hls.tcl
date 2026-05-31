#!/usr/bin/tclsh
###############################################################################
# Vitis HLS Synthesis Script for EDSR Super-Resolution Accelerator
###############################################################################

set project_name "edsr_hls"
set top_module "edsr_top"
set src_dir [file dirname [info script]]
set solution_name "solution1"
set fpga_part "xcvu9p-flga2104-2L-e"
set clock_period "3.333"

open_project $project_name -reset

add_files $src_dir/edsr_hls.cpp
add_files -tb $src_dir/edsr_hls_tb.cpp

set_top $top_module

open_solution -reset $solution_name -flow_target vivado
set_part $fpga_part
create_clock -period $clock_period -name default

config_compile -pipeline_loops 16
config_schedule -enable_dsp_48_max_usage 80
config_array_partition -complete_threshold 0
config_interface -m_axi_addr_width 64 -m_axi_max_widen_bit_width 512
config_dataflow -enable_stall_profiling

puts "=== Running C-Simulation ==="
csim_design -clean -O

puts "=== Running Synthesis ==="
csynth_design

puts "=== Running Co-Simulation (xsim) ==="
cosim_design -enable_dataflow_profiling -trace_level all -tool xsim

puts "=== Exporting RTL ==="
export_design -format syn_dcp -rtl verilog
export_design -format ip_catalog -vendor "xilinx.com" -library "hls" -version "1.0"

puts "=== Summary Report ==="
set fp [open "edsr_hls.rpt" w]
puts $fp "EDSR HLS Synthesis Report\n"
puts $fp "=========================\n"
puts $fp "FPGA Part: $fpga_part"
puts $fp "Clock Period: $clock_period ns (300 MHz)"
puts $fp "Target: 720p -> 1080p @ 30fps"
puts $fp "Latency Target: <50ms\n"
close $fp

close_solution
close_project

puts "=== Synthesis Complete ==="