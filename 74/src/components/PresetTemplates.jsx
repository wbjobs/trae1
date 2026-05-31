import { Layers, CheckCircle, AlertTriangle } from "lucide-react";
import "./PresetTemplates.css";

const presets = [
  {
    name: "仓储模式",
    description: "适合仓库库存管理，连续扫描模式，支持快速条码录入",
    icon: "📦",
    config: {
      scan_mode: "Continuous",
      suffix_char: "\r",
      prefix_char: null,
      beeper_enabled: true,
      led_color: "Green",
    },
  },
  {
    name: "零售模式",
    description: "适合零售POS收银，手动触发扫描，蜂鸣器提示",
    icon: "🛒",
    config: {
      scan_mode: "Trigger",
      suffix_char: "\r\n",
      prefix_char: null,
      beeper_enabled: true,
      led_color: "Blue",
    },
  },
  {
    name: "医疗模式",
    description: "适合医疗环境，静音模式，关闭蜂鸣器",
    icon: "🏥",
    config: {
      scan_mode: "Trigger",
      suffix_char: "\r",
      prefix_char: "M",
      beeper_enabled: false,
      led_color: "Yellow",
    },
  },
];

export default function PresetTemplates({ device, onApply }) {
  if (!device) {
    return (
      <div className="preset-panel">
        <div className="empty-state">
          <Layers size={48} />
          <p>请先选择一个已连接的设备</p>
          <p className="hint">在"设备管理"中连接设备后，选择设备以应用预设</p>
        </div>
      </div>
    );
  }

  return (
    <div className="preset-panel">
      <div className="preset-header">
        <Layers size={24} />
        <div>
          <h2>预设配置模板</h2>
          <p>为设备：{device.product || "未知设备"}</p>
        </div>
      </div>

      <div className="preset-notice">
        <AlertTriangle size={18} />
        <span>应用预设将覆盖当前设备配置，请确认后操作</span>
      </div>

      <div className="preset-grid">
        {presets.map((preset) => (
          <div key={preset.name} className="preset-card">
            <div className="preset-icon">{preset.icon}</div>
            <h3>{preset.name}</h3>
            <p className="preset-description">{preset.description}</p>

            <div className="preset-config-preview">
              <div className="config-item">
                <span className="config-label">扫描模式</span>
                <span className="config-value">
                  {preset.config.scan_mode === "Continuous"
                    ? "连续扫描"
                    : preset.config.scan_mode === "Trigger"
                    ? "触发模式"
                    : "手动模式"}
                </span>
              </div>
              <div className="config-item">
                <span className="config-label">蜂鸣器</span>
                <span className="config-value">
                  {preset.config.beeper_enabled ? "启用" : "关闭"}
                </span>
              </div>
              <div className="config-item">
                <span className="config-label">LED颜色</span>
                <span className={`led-dot led-${preset.config.led_color.toLowerCase()}`} />
              </div>
              {preset.config.prefix_char && (
                <div className="config-item">
                  <span className="config-label">前缀</span>
                  <span className="config-value">{preset.config.prefix_char}</span>
                </div>
              )}
              {preset.config.suffix_char && (
                <div className="config-item">
                  <span className="config-label">后缀</span>
                  <span className="config-value">
                    {preset.config.suffix_char === "\r"
                      ? "CR"
                      : preset.config.suffix_char === "\n"
                      ? "LF"
                      : preset.config.suffix_char === "\r\n"
                      ? "CR+LF"
                      : preset.config.suffix_char}
                  </span>
                </div>
              )}
            </div>

            <button
              className="btn btn-primary apply-btn"
              onClick={() => onApply(device.id, preset.name)}
            >
              <CheckCircle size={16} />
              应用此预设
            </button>
          </div>
        ))}
      </div>
    </div>
  );
}
