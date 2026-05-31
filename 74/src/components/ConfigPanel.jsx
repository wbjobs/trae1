import { useState, useEffect } from "react";
import { Settings, Save, AlertTriangle, Info } from "lucide-react";
import "./ConfigPanel.css";

const scanModes = [
  { value: "Manual", label: "手动模式", desc: "需要手动触发扫描" },
  { value: "Continuous", label: "连续扫描", desc: "持续扫描直到停止" },
  { value: "Trigger", label: "触发模式", desc: "按下扳机时扫描" },
];

const ledColors = [
  { value: "Red", label: "红色", color: "#ee5253" },
  { value: "Green", label: "绿色", color: "#4ecca3" },
  { value: "Blue", label: "蓝色", color: "#54a0ff" },
  { value: "Yellow", label: "黄色", color: "#ff9f43" },
  { value: "Off", label: "关闭", color: "#636e72" },
];

export default function ConfigPanel({ device, config, capabilities, onSave }) {
  const [localConfig, setLocalConfig] = useState(config);
  const [isModified, setIsModified] = useState(false);
  const [validationErrors, setValidationErrors] = useState([]);

  useEffect(() => {
    setLocalConfig(config);
    setIsModified(false);
    setValidationErrors([]);
  }, [config]);

  const validateConfig = (cfg) => {
    const errors = [];
    if (!capabilities) return errors;

    if (cfg.prefix_char && cfg.prefix_char.length > capabilities.max_prefix_length) {
      errors.push({
        field: "prefix_char",
        message: `前缀字符长度${cfg.prefix_char.length}字节超出设备限制${capabilities.max_prefix_length}字节`,
      });
    }

    if (cfg.suffix_char && cfg.suffix_char.length > capabilities.max_suffix_length) {
      errors.push({
        field: "suffix_char",
        message: `后缀字符长度${cfg.suffix_char.length}字节超出设备限制${capabilities.max_suffix_length}字节`,
      });
    }

    if (
      capabilities.supported_scan_modes &&
      capabilities.supported_scan_modes.length > 0 &&
      !capabilities.supported_scan_modes.includes(cfg.scan_mode)
    ) {
      errors.push({
        field: "scan_mode",
        message: `扫描模式不被此设备支持，支持: ${capabilities.supported_scan_modes.join(", ")}`,
      });
    }

    if (
      capabilities.supported_led_colors &&
      capabilities.supported_led_colors.length > 0 &&
      !capabilities.supported_led_colors.includes(cfg.led_color)
    ) {
      errors.push({
        field: "led_color",
        message: `LED颜色不被此设备支持，支持: ${capabilities.supported_led_colors.join(", ")}`,
      });
    }

    if (
      capabilities.max_output_report_size &&
      cfg.serialized_size > capabilities.max_output_report_size
    ) {
      errors.push({
        field: "config_size",
        message: `配置总大小${cfg.serialized_size}字节超出设备HID报告限制${capabilities.max_output_report_size}字节`,
      });
    }

    return errors;
  };

  const handleChange = (field, value) => {
    const newConfig = {
      ...localConfig,
      [field]: value,
    };
    setLocalConfig(newConfig);
    setIsModified(true);
    setValidationErrors(validateConfig(newConfig));
  };

  const handleSave = () => {
    if (device && localConfig && validationErrors.length === 0) {
      onSave(device.id, localConfig);
      setIsModified(false);
    }
  };

  if (!device) {
    return (
      <div className="config-panel">
        <div className="empty-state">
          <Settings size={48} />
          <p>请先选择一个已连接的设备</p>
          <p className="hint">在"设备管理"中连接设备后，选择设备以配置</p>
        </div>
      </div>
    );
  }

  const hasErrors = validationErrors.length > 0;

  return (
    <div className="config-panel">
      <div className="config-header">
        <div className="device-info-banner">
          <Settings size={24} />
          <div>
            <h2>{device.product || "未知设备"}</h2>
            <p>
              VID: 0x{device.vendor_id.toString(16).padStart(4, "0").toUpperCase()}
              {" | "}
              PID: 0x{device.product_id.toString(16).padStart(4, "0").toUpperCase()}
            </p>
          </div>
        </div>
        {isModified && (
          <div className="modified-badge">
            <AlertTriangle size={16} />
            有未保存的更改
          </div>
        )}
      </div>

      {capabilities && (
        <div className="capabilities-banner">
          <Info size={16} />
          <div className="capabilities-info">
            <span>设备HID输出报告: {capabilities.max_output_report_size}字节</span>
            <span>前缀最大: {capabilities.max_prefix_length}字节</span>
            <span>后缀最大: {capabilities.max_suffix_length}字节</span>
          </div>
        </div>
      )}

      {hasErrors && (
        <div className="validation-errors">
          <strong>⚠️ 配置校验未通过：</strong>
          <ul>
            {validationErrors.map((err, i) => (
              <li key={i}>{err.message}</li>
            ))}
          </ul>
        </div>
      )}

      {localConfig ? (
        <div className="config-form">
          <div className="form-section">
            <h3>扫描模式</h3>
            <div className="radio-group">
              {scanModes.map((mode) => (
                <label
                  key={mode.value}
                  className={`radio-label ${
                    localConfig.scan_mode === mode.value ? "checked" : ""
                  } ${
                    capabilities?.supported_scan_modes &&
                    !capabilities.supported_scan_modes.includes(mode.value)
                      ? "disabled"
                      : ""
                  }`}
                >
                  <input
                    type="radio"
                    name="scanMode"
                    value={mode.value}
                    checked={localConfig.scan_mode === mode.value}
                    onChange={() => handleChange("scan_mode", mode.value)}
                  />
                  <span>{mode.label}</span>
                </label>
              ))}
            </div>
          </div>

          <div className="form-section">
            <h3>字符设置</h3>
            <div className="form-row">
              <div className="form-group">
                <label>
                  前缀字符
                  {capabilities && (
                    <span className="limit-hint">
                      （最大{capabilities.max_prefix_length}字节）
                    </span>
                  )}
                </label>
                <input
                  type="text"
                  value={localConfig.prefix_char || ""}
                  onChange={(e) => handleChange("prefix_char", e.target.value || null)}
                  placeholder="留空表示无"
                  className={`form-input ${
                    validationErrors.some((e) => e.field === "prefix_char")
                      ? "input-error"
                      : ""
                  }`}
                />
                {localConfig.prefix_char && (
                  <span className="char-count">
                    {localConfig.prefix_char.length} / {capabilities?.max_prefix_length || "?"}
                  </span>
                )}
              </div>
              <div className="form-group">
                <label>
                  后缀字符
                  {capabilities && (
                    <span className="limit-hint">
                      （最大{capabilities.max_suffix_length}字节）
                    </span>
                  )}
                </label>
                <input
                  type="text"
                  value={localConfig.suffix_char || ""}
                  onChange={(e) => handleChange("suffix_char", e.target.value || null)}
                  placeholder="例如: \\r, \\n, \\r\\n"
                  className={`form-input ${
                    validationErrors.some((e) => e.field === "suffix_char")
                      ? "input-error"
                      : ""
                  }`}
                />
                {localConfig.suffix_char && (
                  <span className="char-count">
                    {localConfig.suffix_char.length} / {capabilities?.max_suffix_length || "?"}
                  </span>
                )}
              </div>
            </div>
          </div>

          <div className="form-section">
            <h3>蜂鸣器</h3>
            <label className="switch-label">
              <div className="switch-info">
                <span>启用蜂鸣器</span>
                <p className="hint">扫描成功时发出提示音</p>
              </div>
              <div className="switch-wrapper">
                <input
                  type="checkbox"
                  checked={localConfig.beeper_enabled}
                  onChange={(e) => handleChange("beeper_enabled", e.target.checked)}
                  className="switch-input"
                />
                <span className="switch-slider"></span>
              </div>
            </label>
          </div>

          <div className="form-section">
            <h3>LED指示灯</h3>
            <div className="color-grid">
              {ledColors.map((color) => (
                <label
                  key={color.value}
                  className={`color-option ${
                    localConfig.led_color === color.value ? "selected" : ""
                  } ${
                    capabilities?.supported_led_colors &&
                    !capabilities.supported_led_colors.includes(color.value)
                      ? "disabled"
                      : ""
                  }`}
                >
                  <input
                    type="radio"
                    name="ledColor"
                    value={color.value}
                    checked={localConfig.led_color === color.value}
                    onChange={() => handleChange("led_color", color.value)}
                  />
                  <div
                    className="color-preview"
                    style={{ backgroundColor: color.color }}
                  />
                  <span>{color.label}</span>
                </label>
              ))}
            </div>
          </div>

          <div className="form-actions">
            <button
              className="btn btn-primary"
              onClick={handleSave}
              disabled={!isModified || hasErrors}
            >
              <Save size={18} />
              保存配置
            </button>
          </div>
        </div>
      ) : (
        <div className="loading-config">
          <p>正在加载设备配置...</p>
        </div>
      )}
    </div>
  );
}
