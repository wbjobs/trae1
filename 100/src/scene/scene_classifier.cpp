#include "scene/scene_classifier.h"

#include <chrono>

SceneClassifier::SceneClassifier() {}

SceneClassifier::~SceneClassifier() {}

bool SceneClassifier::initialize(const std::string& model_path) {
    if (!model_path.empty()) {
        try {
            net_ = cv::dnn::readNet(model_path);
            if (net_.empty()) {
                std::cerr << "[SceneClassifier] Failed to load model from: " << model_path << "\n";
            } else {
                net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                initialized_ = true;
                std::cout << "[SceneClassifier] MobileNet model loaded successfully\n";
                return true;
            }
        } catch (const cv::Exception& e) {
            std::cerr << "[SceneClassifier] Exception loading model: " << e.what() << "\n";
        }
    }

    std::cout << "[SceneClassifier] Using heuristic-based classification\n";
    initialized_ = true;
    return true;
}

SceneClassificationResult SceneClassifier::classify(const cv::Mat& frame) {
    SceneClassificationResult result;

    if (!initialized_) {
        result.type = SceneType::UNKNOWN;
        result.confidence = 0.0;
        result.label = "unknown";
        result.inference_time_ms = 0;
        return result;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    if (!net_.empty()) {
        cv::Mat blob;
        preprocessFrame(frame, blob);
        net_.setInput(blob);

        cv::Mat output = net_.forward();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        result = postprocessResults(output, duration.count());
    } else {
        result = postprocessResults(cv::Mat(), 0);
        result.class_probabilities.clear();
        result.type = classifyByHeuristics(frame, result.class_probabilities);
        result.confidence = result.class_probabilities[result.type];
        result.label = sceneTypeToString(result.type);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        result.inference_time_ms = duration.count();
    }

    if (result.confidence >= min_confidence_threshold_) {
        if (result.type == current_scene_) {
            stable_frames_++;
            pending_scene_ = SceneType::UNKNOWN;
            pending_frames_ = 0;
        } else if (result.type == pending_scene_) {
            pending_frames_++;
            if (pending_frames_ >= stable_frames_threshold_) {
                current_scene_ = result.type;
                current_confidence_ = result.confidence;
                stable_frames_ = pending_frames_;
                pending_scene_ = SceneType::UNKNOWN;
                pending_frames_ = 0;
            }
        } else {
            pending_scene_ = result.type;
            pending_frames_ = 1;
        }
    }

    current_confidence_ = result.confidence;

    return result;
}

bool SceneClassifier::isSceneStable() const {
    return stable_frames_ >= stable_frames_threshold_;
}

std::string SceneClassifier::sceneTypeToString(SceneType type) {
    switch (type) {
        case SceneType::ANIMATION: return "animation";
        case SceneType::SPORTS: return "sports";
        case SceneType::MOVIE: return "movie";
        case SceneType::SURVEILLANCE: return "surveillance";
        case SceneType::CUSTOM: return "custom";
        case SceneType::UNKNOWN:
        default: return "unknown";
    }
}

SceneType SceneClassifier::stringToSceneType(const std::string& str) {
    if (str == "animation") return SceneType::ANIMATION;
    if (str == "sports") return SceneType::SPORTS;
    if (str == "movie") return SceneType::MOVIE;
    if (str == "surveillance") return SceneType::SURVEILLANCE;
    if (str == "custom") return SceneType::CUSTOM;
    return SceneType::UNKNOWN;
}

void SceneClassifier::preprocessFrame(const cv::Mat& frame, cv::Mat& blob) {
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(224, 224));
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    blob = cv::dnn::blobFromImage(resized, 1.0 / 255.0, cv::Size(224, 224),
                                  cv::Scalar(0.485, 0.456, 0.406), true, false);
}

SceneClassificationResult SceneClassifier::postprocessResults(const cv::Mat& output, int64_t inference_time) {
    SceneClassificationResult result;
    result.inference_time_ms = inference_time;

    if (output.empty()) {
        result.type = SceneType::UNKNOWN;
        result.confidence = 0.0;
        result.label = "unknown";
        return result;
    }

    cv::Mat prob = output.reshape(1, 1);
    double max_conf = 0.0;
    int max_idx = 0;

    for (int i = 0; i < prob.cols && i < (int)class_labels_.size(); ++i) {
        double conf = prob.at<float>(0, i);
        SceneType type = static_cast<SceneType>(i);
        result.class_probabilities[type] = conf;
        if (conf > max_conf) {
            max_conf = conf;
            max_idx = i;
        }
    }

    result.type = static_cast<SceneType>(max_idx);
    result.confidence = max_conf;
    result.label = class_labels_[max_idx];

    return result;
}

SceneType SceneClassifier::classifyByHeuristics(const cv::Mat& frame,
                                                 std::unordered_map<SceneType, double>& probs) {
    double saturation = computeColorSaturation(frame);
    double edge_density = computeEdgeDensity(frame);
    double skin_ratio = computeSkinToneRatio(frame);

    probs[SceneType::ANIMATION] = std::min(0.95, saturation * 0.6 + (1.0 - edge_density) * 0.4);
    probs[SceneType::SPORTS] = std::min(0.9, edge_density * 0.5 + (1.0 - skin_ratio) * 0.3 + 0.2);
    probs[SceneType::MOVIE] = std::min(0.85, skin_ratio * 0.5 + saturation * 0.3 + 0.2);
    probs[SceneType::SURVEILLANCE] = std::min(0.8, (1.0 - saturation) * 0.6 + (1.0 - skin_ratio) * 0.4);
    probs[SceneType::UNKNOWN] = 0.1;

    double total = 0.0;
    for (auto& [type, prob] : probs) {
        total += prob;
    }
    for (auto& [type, prob] : probs) {
        prob /= total;
    }

    SceneType best_type = SceneType::UNKNOWN;
    double best_prob = 0.0;
    for (auto& [type, prob] : probs) {
        if (prob > best_prob) {
            best_prob = prob;
            best_type = type;
        }
    }

    return best_type;
}

double SceneClassifier::computeColorSaturation(const cv::Mat& frame) {
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    cv::Scalar mean_sat = cv::mean(channels[1]);
    return mean_sat[0] / 255.0;
}

double SceneClassifier::computeEdgeDensity(const cv::Mat& frame) {
    cv::Mat gray, edges;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::Canny(gray, edges, 50, 150);
    double edge_pixels = cv::countNonZero(edges);
    return edge_pixels / (frame.rows * frame.cols);
}

double SceneClassifier::computeSkinToneRatio(const cv::Mat& frame) {
    cv::Mat ycrcb;
    cv::cvtColor(frame, ycrcb, cv::COLOR_BGR2YCrCb);
    cv::Mat skin_mask = cv::Mat::zeros(frame.size(), CV_8UC1);

    for (int y = 0; y < frame.rows; y++) {
        for (int x = 0; x < frame.cols; x++) {
            cv::Vec3b pixel = ycrcb.at<cv::Vec3b>(y, x);
            int cr = pixel[1];
            int cb = pixel[2];
            if (cr >= 133 && cr <= 173 && cb >= 77 && cb <= 127) {
                skin_mask.at<uchar>(y, x) = 255;
            }
        }
    }

    double skin_pixels = cv::countNonZero(skin_mask);
    return skin_pixels / (frame.rows * frame.cols);
}

double SceneClassifier::computeMotionLevel(const cv::Mat& frame) {
    static cv::Mat prev_gray;
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    if (prev_gray.empty()) {
        prev_gray = gray.clone();
        return 0.0;
    }

    cv::Mat diff;
    cv::absdiff(gray, prev_gray, diff);
    double motion = cv::mean(diff)[0] / 255.0;
    prev_gray = gray.clone();

    return motion;
}