#include "utils/common.h"
#include "utils/args_parser.h"
#include "pipeline/pipeline_manager.h"
#include "edsr/edsr_weights_loader.h"

#include <csignal>
#include <atomic>

SceneClassifier* g_scene_classifier = nullptr;

std::atomic<bool> g_running{true};
std::unique_ptr<PipelineManager> g_manager;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[Main] Received signal " << signal << ", shutting down...\n";
        g_running.store(false);
        if (g_manager) {
            g_manager->stopAll();
        }
    }
}

void handleFrameOutput(const ProcessedFrame& frame) {
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            ArgsParser::printUsage(argv[0]);
            return 0;
        }
    }

    ServiceConfig config = ArgsParser::parse(argc, argv);
    ArgsParser::printConfig(config);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "\n=== Video Stream Super-Resolution Service ===\n";
    std::cout << "  Input Resolution:  " << INPUT_WIDTH << "x" << INPUT_HEIGHT << " @ " << TARGET_FPS << "fps\n";
    std::cout << "  Output Resolution: " << OUTPUT_WIDTH << "x" << OUTPUT_HEIGHT << " @ " << TARGET_FPS << "fps\n";
    std::cout << "  Model: EDSR x" << EDSR_SCALE_FACTOR << " (INT8 quantized)\n";
    std::cout << "  Target Latency: < " << TARGET_LATENCY_MS << "ms\n";
    std::cout << "  Max Streams: " << MAX_STREAMS << "\n";

    if (config.enable_motion_compensation) {
        std::cout << "\n  === Motion Compensation Enabled ===\n";
        std::cout << "  Temporal Window: " << TEMPORAL_WINDOW_SIZE << " frames\n";
        std::cout << "  3D Convolution: " << TEMPORAL_KERNEL << "x" << EDSR_KERNEL_SIZE << "x" << EDSR_KERNEL_SIZE << "\n";
        std::cout << "  Auto Fallback: " << (config.auto_fallback ? "Enabled" : "Disabled") << "\n";
        std::cout << "  Motion Threshold: " << config.motion_threshold << "\n";
        std::cout << "  Artifact Threshold: " << config.artifact_threshold << "\n";
        std::cout << "  Expected Param Increase: ~15%\n";
        std::cout << "  Expected Performance Overhead: <10%\n";
    }

    g_manager = std::make_unique<PipelineManager>(config);

    if (!g_manager->initialize()) {
        std::cerr << "[Main] Failed to initialize pipeline manager\n";
        return 1;
    }

    g_manager->setGlobalOutputCallback(handleFrameOutput);

    std::cout << "\n=== Starting processing pipelines ===\n";
    g_manager->startAll();

    std::cout << "[Main] Service running. Press Ctrl+C to stop.\n";

    while (g_running.load() && g_manager->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n=== Final Statistics ===\n";
    g_manager->printStatsReport();

    std::cout << "[Main] Service shutdown complete.\n";
    return 0;
}