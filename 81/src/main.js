const COMMAND_LABELS = [
    "开机", "关机", "调高音量", "调低音量",
    "下一首", "上一首", "暂停", "播放",
    "静音", "取消静音"
];

const CMD_ICONS = {
    "开机": "⚡",
    "关机": "⏻",
    "调高音量": "🔊",
    "调低音量": "🔉",
    "下一首": "⏭",
    "上一首": "⏮",
    "暂停": "⏸",
    "播放": "▶",
    "静音": "🔇",
    "取消静音": "🔈",
    "未知": "❓"
};

let status = {
    isRecording: false,
    isAwake: false,
    smartHomeConnected: false,
    modelLoaded: false,
    denoiseEnabled: true,
    votingEnabled: true,
    noiseLearning: false,
    noiseLearned: false,
    snrEstimate: 20.0
};

let currentResult = null;
let historyCache = [];

async function invoke(cmd, args = {}) {
    if (window.__TAURI__ && window.__TAURI__.core) {
        return await window.__TAURI__.core.invoke(cmd, args);
    }
    console.log("[Mock invoke]", cmd, args);
    return null;
}

async function init() {
    setupEventListeners();
    setupConfidenceBars();
    updateStatusUI();
    loadHistory();
    pollStatus();
    pollDenoiseStatus();
}

function setupEventListeners() {
    document.getElementById("btn-start").addEventListener("click", async () => {
        try {
            await invoke("start_recording");
            status.isRecording = true;
            updateStatusUI();
            document.getElementById("current-label").classList.add("listening");
        } catch (e) {
            console.error("启动录音失败:", e);
            alert("启动录音失败: " + e);
        }
    });

    document.getElementById("btn-stop").addEventListener("click", async () => {
        try {
            await invoke("stop_recording");
            status.isRecording = false;
            status.isAwake = false;
            updateStatusUI();
            document.getElementById("current-label").classList.remove("listening");
        } catch (e) {
            console.error("停止录音失败:", e);
        }
    });

    document.getElementById("btn-clear-history").addEventListener("click", async () => {
        await invoke("clear_history");
        historyCache = [];
        renderHistory();
    });

    document.getElementById("btn-load-model").addEventListener("click", async () => {
        const path = document.getElementById("model-path").value;
        try {
            await invoke("load_model", { modelPath: path });
            status.modelLoaded = true;
            updateModelStatus(path);
        } catch (e) {
            console.error("加载模型失败:", e);
            alert("加载模型失败: " + e);
        }
    });

    document.getElementById("confidence-threshold").addEventListener("input", async (e) => {
        const val = parseFloat(e.target.value);
        document.getElementById("confidence-threshold-value").textContent = val.toFixed(2);
        try {
            await invoke("set_confidence_threshold", { threshold: val });
        } catch (e) {
            console.error("设置置信度阈值失败:", e);
        }
    });

    document.getElementById("hotword-threshold").addEventListener("input", async (e) => {
        const val = parseFloat(e.target.value);
        document.getElementById("hotword-threshold-value").textContent = val.toFixed(2);
        try {
            await invoke("set_hotword_threshold", { threshold: val });
        } catch (e) {
            console.error("设置热词阈值失败:", e);
        }
    });

    document.getElementById("btn-connect-ws").addEventListener("click", async () => {
        const url = document.getElementById("ws-url").value;
        try {
            await invoke("connect_smart_home", { url });
            status.smartHomeConnected = true;
            updateStatusUI();
        } catch (e) {
            console.error("连接智能家居服务失败:", e);
        }
    });

    document.getElementById("btn-disconnect-ws").addEventListener("click", async () => {
        try {
            await invoke("disconnect_smart_home");
            status.smartHomeConnected = false;
            updateStatusUI();
        } catch (e) {
            console.error("断开连接失败:", e);
        }
    });

    document.getElementById("btn-simulate").addEventListener("click", async () => {
        const label = document.getElementById("simulate-label").value;
        const confidence = 0.95;
        try {
            await invoke("simulate_command", { label, confidence });
            simulateResult(label, confidence);
        } catch (e) {
            console.error("模拟命令失败:", e);
        }
    });

    document.getElementById("enable-denoise").addEventListener("change", async (e) => {
        const enabled = e.target.checked;
        try {
            await invoke("set_denoise_enabled", { enabled });
            status.denoiseEnabled = enabled;
            document.getElementById("denoise-status").textContent = enabled ? "已启用" : "已禁用";
            updateStatusUI();
        } catch (err) {
            console.error("设置降噪状态失败:", err);
        }
    });

    document.getElementById("btn-start-noise-learning").addEventListener("click", async () => {
        try {
            await invoke("start_noise_learning");
            status.noiseLearning = true;
            updateNoiseLearningUI();
            alert("开始环境噪声学习，请保持安静约3秒钟...");
        } catch (e) {
            console.error("开始噪声学习失败:", e);
        }
    });

    document.getElementById("btn-stop-noise-learning").addEventListener("click", async () => {
        const savePath = document.getElementById("noise-profile-path").value || "noise_profile.json";
        try {
            const profile = await invoke("stop_noise_learning", { savePath: savePath });
            status.noiseLearning = false;
            status.noiseLearned = profile.learned_frames >= 50;
            status.snrEstimate = profile.snr_estimate;
            updateNoiseLearningUI();
            updateSnrDisplay();
            alert(
                `噪声学习完成！\n帧数: ${profile.learned_frames}\n估计SNR: ${profile.snr_estimate.toFixed(1)} dB`
            );
        } catch (e) {
            console.error("停止噪声学习失败:", e);
            status.noiseLearning = false;
            updateNoiseLearningUI();
        }
    });

    document.getElementById("btn-save-noise-profile").addEventListener("click", async () => {
        const savePath = document.getElementById("noise-profile-path").value || "noise_profile.json";
        try {
            const profile = await invoke("stop_noise_learning", { savePath });
            alert(`噪声配置已保存到: ${savePath}`);
        } catch (e) {
            console.error("保存噪声配置失败:", e);
        }
    });

    document.getElementById("enable-voting").addEventListener("change", async (e) => {
        const enabled = e.target.checked;
        try {
            await invoke("set_voting_enabled", { enabled });
            status.votingEnabled = enabled;
        } catch (err) {
            console.error("设置投票状态失败:", err);
        }
    });

    document.getElementById("voting-min-frames").addEventListener("change", async () => {
        await updateVotingConfig();
    });

    document.getElementById("voting-consensus").addEventListener("input", async (e) => {
        const val = parseFloat(e.target.value);
        document.getElementById("voting-consensus-value").textContent = val.toFixed(2);
    });

    document.getElementById("voting-consensus").addEventListener("change", async () => {
        await updateVotingConfig();
    });

    document.getElementById("voting-avg-confidence").addEventListener("input", async (e) => {
        const val = parseFloat(e.target.value);
        document.getElementById("voting-avg-confidence-value").textContent = val.toFixed(2);
    });

    document.getElementById("voting-avg-confidence").addEventListener("change", async () => {
        await updateVotingConfig();
    });
}

async function updateVotingConfig() {
    const minFrames = parseInt(document.getElementById("voting-min-frames").value) || 3;
    const consensus = parseFloat(document.getElementById("voting-consensus").value) || 0.6;
    const avgConfidence = parseFloat(document.getElementById("voting-avg-confidence").value) || 0.7;

    try {
        await invoke("set_voting_config", {
            minFrames,
            minConsensus: consensus,
            minAvgConfidence: avgConfidence
        });
    } catch (e) {
        console.error("更新投票配置失败:", e);
    }
}

function updateNoiseLearningUI() {
    const startBtn = document.getElementById("btn-start-noise-learning");
    const stopBtn = document.getElementById("btn-stop-noise-learning");

    if (status.noiseLearning) {
        startBtn.disabled = true;
        stopBtn.disabled = false;
        startBtn.innerHTML = '<span class="learning-indicator"></span>学习中...';
    } else {
        startBtn.disabled = false;
        stopBtn.disabled = true;
        startBtn.textContent = "开始学习";
    }
}

function updateSnrDisplay() {
    const el = document.getElementById("snr-estimate");
    if (!el) return;

    const snr = status.snrEstimate;
    el.textContent = `${snr.toFixed(1)} dB`;
    el.classList.remove("low", "critical");

    if (snr < 10) {
        el.classList.add("critical");
    } else if (snr < 15) {
        el.classList.add("low");
    }
}

function setupConfidenceBars() {
    const container = document.getElementById("confidence-bars");
    container.innerHTML = "";
    COMMAND_LABELS.forEach(label => {
        const bar = document.createElement("div");
        bar.className = "chart-bar";
        bar.innerHTML = `
            <div class="chart-bar-wrapper">
                <div class="chart-bar-fill" data-label="${label}"></div>
                <span class="chart-bar-value">0%</span>
            </div>
            <span class="chart-bar-label">${label}</span>
        `;
        container.appendChild(bar);
    });
}

function updateConfidenceBars(allConfidences) {
    if (!allConfidences) return;
    allConfidences.forEach(([label, conf]) => {
        const fill = document.querySelector(`.chart-bar-fill[data-label="${label}"]`);
        if (fill) {
            const percent = (conf * 100).toFixed(0);
            fill.style.height = `${percent}%`;
            const wrapper = fill.parentElement;
            const valueEl = wrapper.querySelector(".chart-bar-value");
            if (valueEl) valueEl.textContent = `${percent}%`;
        }
    });
}

function updateCurrentResult(result) {
    currentResult = result;
    document.getElementById("current-label").textContent = result.label;
    document.getElementById("confidence-value").textContent =
        (result.confidence * 100).toFixed(1) + "%";
    document.getElementById("confidence-fill").style.width =
        (result.confidence * 100) + "%";
    document.getElementById("inference-time").textContent =
        `推理时间: ${result.inference_time_ms} ms`;
    updateConfidenceBars(result.all_confidences);
}

function simulateResult(label, confidence) {
    const allConfidences = COMMAND_LABELS.map(l =>
        [l, l === label ? confidence : 0.0]
    );
    const result = {
        label,
        confidence,
        all_confidences: allConfidences,
        inference_time_ms: 42
    };
    updateCurrentResult(result);
    const entry = {
        label,
        confidence,
        inference_time_ms: 42,
        timestamp: Math.floor(Date.now() / 1000),
        sent_to_smart_home: true
    };
    historyCache.unshift(entry);
    if (historyCache.length > 50) historyCache.pop();
    renderHistory();
}

async function loadHistory() {
    try {
        const history = await invoke("get_history", { limit: 50 });
        if (history && history.length) {
            historyCache = history.reverse();
            renderHistory();
        }
    } catch (e) {
        console.log("获取历史记录失败:", e);
    }
}

function renderHistory() {
    const container = document.getElementById("history-list");
    if (!historyCache.length) {
        container.innerHTML = '<div class="history-empty">暂无识别记录</div>';
        return;
    }

    container.innerHTML = historyCache.slice(0, 20).map(item => {
        const time = new Date(item.timestamp * 1000).toLocaleTimeString("zh-CN");
        const icon = CMD_ICONS[item.label] || "❓";
        const confPercent = (item.confidence * 100).toFixed(1);
        const sentClass = item.sent_to_smart_home ? "sent" : "";
        return `
            <div class="history-item ${sentClass}">
                <div class="history-item-icon">${icon}</div>
                <div class="history-item-content">
                    <div class="history-item-label">${item.label}</div>
                    <div class="history-item-meta">${time} · ${item.inference_time_ms}ms</div>
                </div>
                <div class="history-item-conf">${confPercent}%</div>
            </div>
        `;
    }).join("");
}

function updateStatusUI() {
    const recDot = document.querySelector('[data-status="recording"]');
    const awakeDot = document.querySelector('[data-status="awake"]');
    const connDot = document.querySelector('[data-status="connected"]');
    const denoiseDot = document.querySelector('[data-status="denoise"]');

    recDot.classList.toggle("active", status.isRecording);
    awakeDot.classList.toggle("active", status.isAwake);
    connDot.classList.toggle("active", status.smartHomeConnected);
    denoiseDot.classList.toggle("active", status.denoiseEnabled);

    document.getElementById("btn-start").disabled = status.isRecording;
    document.getElementById("btn-stop").disabled = !status.isRecording;

    const hotwordValue = document.getElementById("hotword-value");
    if (status.isAwake) {
        hotwordValue.textContent = "已唤醒";
        hotwordValue.classList.add("awake");
    } else {
        hotwordValue.textContent = "待机";
        hotwordValue.classList.remove("awake");
    }
}

function updateModelStatus(path) {
    const el = document.getElementById("model-status");
    if (status.modelLoaded) {
        el.textContent = `模型: ${path.split("/").pop() || path}`;
        el.classList.add("loaded");
    } else {
        el.textContent = "模型: 未加载";
        el.classList.remove("loaded");
    }
}

async function pollStatus() {
    setInterval(async () => {
        try {
            const s = await invoke("get_status");
            if (s) {
                status.isRecording = s.is_recording;
                status.isAwake = s.is_awake;
                status.smartHomeConnected = s.is_smart_home_connected;
                updateStatusUI();

                if (s.current_label && s.current_label !== "--") {
                    document.getElementById("current-label").textContent = s.current_label;
                    document.getElementById("confidence-value").textContent =
                        (s.current_confidence * 100).toFixed(1) + "%";
                    document.getElementById("confidence-fill").style.width =
                        (s.current_confidence * 100) + "%";
                    document.getElementById("inference-time").textContent =
                        `推理时间: ${s.last_inference_time_ms} ms`;
                }
            }
        } catch (e) {
            // silently ignore
        }
    }, 500);
}

async function pollDenoiseStatus() {
    setInterval(async () => {
        try {
            const ds = await invoke("get_denoise_status");
            if (ds) {
                status.denoiseEnabled = ds.enabled;
                status.noiseLearned = ds.noise_learned;
                status.noiseLearning = ds.learning;
                status.snrEstimate = ds.snr_estimate;

                document.getElementById("enable-denoise").checked = ds.enabled;
                document.getElementById("denoise-status").textContent = ds.enabled ? "已启用" : "已禁用";

                updateNoiseLearningUI();
                updateSnrDisplay();
                updateStatusUI();
            }
        } catch (e) {
            // silently ignore
        }
    }, 1000);
}

document.addEventListener("DOMContentLoaded", init);

const enrollmentState = {
    currentStep: 0,
    totalSteps: 3,
    isRecording: false,
    name: "",
    action: "",
    description: "",
    recordings: [],
    noiseLevel: 0,
    qualityScores: [],
};

async function loadCustomCommands() {
    try {
        const commands = await invoke("list_custom_commands");
        renderCustomCommands(commands);
    } catch (e) {
        console.error("加载自定义命令失败:", e);
    }
}

function renderCustomCommands(commands) {
    const container = document.getElementById("custom-commands-list");

    if (!commands || commands.length === 0) {
        container.innerHTML = '<div class="custom-commands-empty">暂无自定义命令词</div>';
        return;
    }

    container.innerHTML = "";

    commands.forEach((cmd) => {
        const item = document.createElement("div");
        item.className = `custom-command-item ${cmd.is_active ? "" : "inactive"}`;
        item.dataset.id = cmd.id;

        const qualityClass = cmd.avg_quality_score > 0.8 ? "quality-high" :
            cmd.avg_quality_score > 0.6 ? "quality-medium" : "quality-low";

        const loraClass = cmd.lora_trained ? "trained" : "untrained";
        const loraText = cmd.lora_trained ? "LoRA适配" : "未微调";

        item.innerHTML = `
            <div class="custom-command-icon">🎙️</div>
            <div class="custom-command-content">
                <div class="custom-command-name">${escapeHtml(cmd.name)}</div>
                <div class="custom-command-action">${escapeHtml(cmd.action)}</div>
                <div class="custom-command-meta">
                    <span>质量: <span class="quality-indicator ${qualityClass}">${(cmd.avg_quality_score * 100).toFixed(0)}%</span></span>
                    <span>阈值: ${cmd.match_threshold.toFixed(2)}</span>
                    <span>触发: ${cmd.trigger_count}次</span>
                    <span class="lora-status ${loraClass}">${loraText}</span>
                </div>
            </div>
            <div class="custom-command-controls">
                <div style="display: flex; gap: 4px; margin-bottom: 4px;">
                    <button class="btn btn-small btn-toggle" onclick="toggleCommand('${cmd.id}')">
                        ${cmd.is_active ? "禁用" : "启用"}
                    </button>
                    <button class="btn btn-small btn-primary" onclick="reRecordCommand('${cmd.id}', '${escapeHtml(cmd.name)}', '${escapeHtml(cmd.action)}', '${escapeHtml(cmd.description)}')">
                        重新录制
                    </button>
                    <button class="btn btn-small btn-small-danger" onclick="deleteCommand('${cmd.id}')">
                        删除
                    </button>
                </div>
                <div style="display: flex; gap: 4px; align-items: center;">
                    <input type="number" class="command-threshold-input" 
                           value="${cmd.match_threshold.toFixed(2)}" min="0.3" max="0.99" step="0.01"
                           onchange="setCommandThreshold('${cmd.id}', this.value)" />
                    <button class="btn btn-small" onclick="trainCommandLora('${cmd.id}')" title="LoRA微调">
                        🚀
                    </button>
                </div>
            </div>
        `;

        container.appendChild(item);
    });
}

function escapeHtml(text) {
    const div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}

function openEnrollmentModal(name = "", action = "", description = "", isReRecord = false) {
    enrollmentState.currentStep = 0;
    enrollmentState.isRecording = false;
    enrollmentState.name = name;
    enrollmentState.action = action;
    enrollmentState.description = description;
    enrollmentState.recordings = [];
    enrollmentState.qualityScores = [];

    document.getElementById("enrollment-title").textContent = isReRecord ? "重新录制命令词" : "录制自定义命令词";
    document.getElementById("enrollment-modal").style.display = "flex";
    document.getElementById("enrollment-form").style.display = "block";
    document.getElementById("enrollment-record").style.display = "none";
    document.getElementById("enrollment-complete").style.display = "none";

    if (isReRecord) {
        document.getElementById("enrollment-name").value = name;
        document.getElementById("enrollment-action").value = action;
        document.getElementById("enrollment-description").value = description;
        document.getElementById("enrollment-name").readOnly = true;
        document.getElementById("enrollment-action").readOnly = true;
    } else {
        document.getElementById("enrollment-name").value = "";
        document.getElementById("enrollment-action").value = "";
        document.getElementById("enrollment-description").value = "";
        document.getElementById("enrollment-name").readOnly = false;
        document.getElementById("enrollment-action").readOnly = false;
    }

    updateEnrollmentProgress();
    updateEnrollmentMessage("准备开始", "请先安静下来，确保环境噪声较低");
}

function closeEnrollmentModal() {
    if (enrollmentState.isRecording) {
        stopEnrollmentCapture();
    }
    try {
        invoke("cancel_enrollment");
    } catch (e) {}
    document.getElementById("enrollment-modal").style.display = "none";
}

function startEnrollment() {
    const name = document.getElementById("enrollment-name").value.trim();
    const action = document.getElementById("enrollment-action").value.trim();
    const description = document.getElementById("enrollment-description").value.trim();

    if (!name) {
        alert("请输入命令词名称");
        return;
    }
    if (!action) {
        alert("请输入映射动作");
        return;
    }

    enrollmentState.name = name;
    enrollmentState.action = action;
    enrollmentState.description = description;

    invoke("start_enrollment", { name, action, description })
        .then(() => {
            document.getElementById("enrollment-form").style.display = "none";
            document.getElementById("enrollment-record").style.display = "block";
            startNoiseCheck();
        })
        .catch((e) => {
            alert("启动注册失败: " + e);
        });
}

async function startNoiseCheck() {
    updateEnrollmentMessage("环境噪声检测中...", "请保持安静5秒，系统正在检测环境噪声");
    document.getElementById("recording-status").textContent = "环境噪声检测中...";
    document.getElementById("btn-start-capture").disabled = true;

    try {
        const noiseResult = await invoke("check_environment_noise");
        enrollmentState.noiseLevel = noiseResult.noise_level;

        if (noiseResult.noise_level > 0.5) {
            updateEnrollmentMessage("环境噪声过高", `当前噪声等级: ${(noiseResult.noise_level * 100).toFixed(0)}%，请换一个安静的环境或关闭噪声源后重试`);
            document.getElementById("btn-start-capture").disabled = true;
        } else if (noiseResult.noise_level > 0.3) {
            updateEnrollmentMessage("环境噪声中等", `当前噪声等级: ${(noiseResult.noise_level * 100).toFixed(0)}%，请尽量靠近麦克风，发音清晰响亮`);
            document.getElementById("btn-start-capture").disabled = false;
        } else {
            updateEnrollmentMessage("环境噪声良好", `当前噪声等级: ${(noiseResult.noise_level * 100).toFixed(0)}%，可以开始录制`);
            document.getElementById("btn-start-capture").disabled = false;
        }

        document.getElementById("recording-status").textContent = "准备就绪";
    } catch (e) {
        console.error("噪声检测失败:", e);
        updateEnrollmentMessage("噪声检测失败", "请确保麦克风已连接并授权");
        document.getElementById("btn-start-capture").disabled = false;
    }
}

function updateEnrollmentProgress() {
    const percent = (enrollmentState.currentStep / enrollmentState.totalSteps) * 100;
    document.getElementById("enrollment-progress-fill").style.width = percent + "%";
    document.getElementById("enrollment-step-text").textContent = `第 ${enrollmentState.currentStep} / ${enrollmentState.totalSteps} 次录制`;
}

function updateEnrollmentMessage(message, hint) {
    document.getElementById("enrollment-message").textContent = message;
    document.getElementById("enrollment-hint").textContent = hint;
}

function startEnrollmentCapture() {
    if (enrollmentState.currentStep >= enrollmentState.totalSteps) {
        completeEnrollment();
        return;
    }

    enrollmentState.isRecording = true;
    document.getElementById("recording-status").textContent = `录制中... 请说: "${enrollmentState.name}"`;
    document.getElementById("btn-start-capture").disabled = true;
    document.getElementById("btn-stop-capture").disabled = false;

    updateEnrollmentMessage(`第 ${enrollmentState.currentStep + 1} 次录制`, `请清晰地说: "${enrollmentState.name}"，说完后点击停止`);

    invoke("start_enrollment_capture")
        .then(() => {
            console.log("开始录制");
        })
        .catch((e) => {
            console.error("开始录制失败:", e);
            alert("开始录制失败: " + e);
            enrollmentState.isRecording = false;
            document.getElementById("btn-start-capture").disabled = false;
            document.getElementById("btn-stop-capture").disabled = true;
        });
}

function stopEnrollmentCapture() {
    enrollmentState.isRecording = false;
    document.getElementById("recording-status").textContent = "处理中...";
    document.getElementById("btn-start-capture").disabled = true;
    document.getElementById("btn-stop-capture").disabled = true;

    invoke("stop_enrollment_capture")
        .then((result) => {
            console.log("录制结果:", result);

            if (result.status === "success") {
                enrollmentState.currentStep++;
                enrollmentState.qualityScores.push(result.quality_score);
                enrollmentState.recordings.push({
                    quality: result.quality_score,
                    duration: result.duration_ms,
                    consistency: result.consistency_score || 0,
                });

                updateEnrollmentProgress();

                let qualityClass = result.quality_score > 0.8 ? "quality-high" :
                    result.quality_score > 0.6 ? "quality-medium" : "quality-low";

                if (enrollmentState.currentStep < enrollmentState.totalSteps) {
                    const nextNum = enrollmentState.currentStep + 1;
                    updateEnrollmentMessage(
                        `录制成功！质量: <span class="quality-indicator ${qualityClass}">${(result.quality_score * 100).toFixed(0)}%</span>`,
                        `请准备第 ${nextNum} 次录制，保持相同的语调和语速`
                    );
                    document.getElementById("recording-status").textContent = `准备第 ${nextNum} 次录制`;
                    document.getElementById("btn-start-capture").disabled = false;
                    document.getElementById("btn-start-capture").textContent = "录制下一次";
                } else {
                    updateEnrollmentMessage(
                        `录制完成！正在处理...`,
                        `系统正在验证发音一致性`
                    );
                    document.getElementById("recording-status").textContent = "验证中...";
                    setTimeout(completeEnrollment, 500);
                }
            } else if (result.status === "retry") {
                updateEnrollmentMessage(
                    `录制质量不佳: ${result.message || "发音不清晰或噪声过大"}`,
                    `质量分数: ${(result.quality_score * 100).toFixed(0)}%，请重新录制`
                );
                document.getElementById("recording-status").textContent = "请重试";
                document.getElementById("btn-start-capture").disabled = false;
                document.getElementById("btn-start-capture").textContent = "重新录制";
            } else {
                alert("录制失败: " + (result.message || "未知错误"));
                document.getElementById("recording-status").textContent = "录制失败";
                document.getElementById("btn-start-capture").disabled = false;
            }

            document.getElementById("btn-stop-capture").disabled = true;
        })
        .catch((e) => {
            console.error("停止录制失败:", e);
            alert("停止录制失败: " + e);
            document.getElementById("recording-status").textContent = "错误";
            document.getElementById("btn-start-capture").disabled = false;
            document.getElementById("btn-stop-capture").disabled = true;
        });
}

async function completeEnrollment() {
    try {
        const result = await invoke("complete_enrollment");

        if (result.success) {
            document.getElementById("enrollment-record").style.display = "none";
            document.getElementById("enrollment-complete").style.display = "block";

            const avgQuality = enrollmentState.qualityScores.reduce((a, b) => a + b, 0) / enrollmentState.qualityScores.length;
            let qualityClass = avgQuality > 0.8 ? "quality-high" :
                avgQuality > 0.6 ? "quality-medium" : "quality-low";

            document.getElementById("enrollment-summary").innerHTML = `
                <div><strong>命令词:</strong> ${escapeHtml(enrollmentState.name)}</div>
                <div><strong>映射动作:</strong> <code>${escapeHtml(enrollmentState.action)}</code></div>
                <div><strong>录制次数:</strong> ${enrollmentState.totalSteps}次</div>
                <div><strong>平均质量:</strong> <span class="quality-indicator ${qualityClass}">${(avgQuality * 100).toFixed(0)}%</span></div>
                <div><strong>建议:</strong> ${result.suggestion || "注册成功，可以开始使用"}</div>
            `;
        } else {
            alert("注册失败: " + (result.message || "未知错误"));
            document.getElementById("recording-status").textContent = "注册失败";
            document.getElementById("btn-start-capture").disabled = false;
        }
    } catch (e) {
        console.error("完成注册失败:", e);
        alert("完成注册失败: " + e);
    }
}

function finishEnrollment() {
    closeEnrollmentModal();
    loadCustomCommands();
}

async function deleteCommand(id) {
    if (!confirm("确定要删除这个命令词吗？此操作不可撤销。")) {
        return;
    }

    try {
        await invoke("delete_custom_command", { id });
        loadCustomCommands();
    } catch (e) {
        alert("删除失败: " + e);
    }
}

async function toggleCommand(id) {
    try {
        const commands = await invoke("list_custom_commands");
        const cmd = commands.find(c => c.id === id);
        if (cmd) {
            await invoke("set_command_active", { id, active: !cmd.is_active });
            loadCustomCommands();
        }
    } catch (e) {
        alert("操作失败: " + e);
    }
}

async function setCommandThreshold(id, threshold) {
    try {
        const value = parseFloat(threshold);
        if (value >= 0.3 && value <= 0.99) {
            await invoke("set_command_threshold", { id, threshold: value });
        }
    } catch (e) {
        console.error("设置阈值失败:", e);
    }
}

function reRecordCommand(id, name, action, description) {
    openEnrollmentModal(name, action, description, true);
}

async function trainCommandLora(id) {
    if (!confirm("确定要对该命令词进行LoRA微调吗？这会提升识别准确率，但需要几秒钟时间。")) {
        return;
    }

    try {
        const result = await invoke("train_lora", { command_id: id });
        if (result.success) {
            alert(`LoRA微调完成！训练样本: ${result.training_samples}个，最终损失: ${result.final_loss.toFixed(4)}`);
            loadCustomCommands();
        } else {
            alert("LoRA微调失败: " + (result.message || "未知错误"));
        }
    } catch (e) {
        alert("LoRA微调失败: " + e);
    }
}

async function pollEnrollmentStatus() {
    setInterval(async () => {
        try {
            const modal = document.getElementById("enrollment-modal");
            if (modal.style.display !== "none") {
                const es = await invoke("get_enrollment_status");
                if (es) {
                    if (es.current_step !== enrollmentState.currentStep) {
                        enrollmentState.currentStep = es.current_step;
                        updateEnrollmentProgress();
                    }
                }
            }
        } catch (e) {
            // silently ignore
        }
    }, 200);
}

async function pollCustomCommands() {
    setInterval(async () => {
        try {
            loadCustomCommands();
        } catch (e) {
            // silently ignore
        }
    }, 5000);
}

document.addEventListener("DOMContentLoaded", () => {
    document.getElementById("btn-add-command").addEventListener("click", () => openEnrollmentModal());
    document.getElementById("btn-close-enrollment").addEventListener("click", closeEnrollmentModal);
    document.getElementById("btn-cancel-enrollment").addEventListener("click", closeEnrollmentModal);
    document.getElementById("btn-start-enrollment").addEventListener("click", startEnrollment);
    document.getElementById("btn-start-capture").addEventListener("click", startEnrollmentCapture);
    document.getElementById("btn-stop-capture").addEventListener("click", stopEnrollmentCapture);
    document.getElementById("btn-finish-enrollment").addEventListener("click", finishEnrollment);

    loadCustomCommands();
    pollEnrollmentStatus();
    pollCustomCommands();
});
