import { Usb, Settings, Layers, HardDrive, Activity, BarChart3 } from "lucide-react";
import "./Sidebar.css";

const navItems = [
  { id: "monitor", label: "实时监控", icon: Activity },
  { id: "statistics", label: "数据统计", icon: BarChart3 },
  { id: "devices", label: "设备管理", icon: Usb },
  { id: "config", label: "配置设置", icon: Settings },
  { id: "presets", label: "预设模板", icon: Layers },
  { id: "backup", label: "备份恢复", icon: HardDrive },
];

export default function Sidebar({ activeTab, onTabChange }) {
  return (
    <aside className="sidebar">
      <div className="sidebar-header">
        <div className="logo">
          <span className="logo-icon">🔲</span>
          <span className="logo-text">Scanner</span>
        </div>
      </div>
      <nav className="nav-menu">
        {navItems.map((item) => {
          const Icon = item.icon;
          return (
            <button
              key={item.id}
              className={`nav-item ${activeTab === item.id ? "active" : ""}`}
              onClick={() => onTabChange(item.id)}
            >
              <Icon size={20} />
              <span>{item.label}</span>
            </button>
          );
        })}
      </nav>
      <div className="sidebar-footer">
        <span className="version">v1.1.0</span>
      </div>
    </aside>
  );
}
