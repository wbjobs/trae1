import { CheckCircle, AlertCircle, Info, XCircle } from "lucide-react";
import "./Toast.css";

const icons = {
  success: CheckCircle,
  error: XCircle,
  warning: AlertCircle,
  info: Info,
};

const colors = {
  success: "#4ecca3",
  error: "#ee5253",
  warning: "#ff9f43",
  info: "#54a0ff",
};

export default function Toast({ message, type = "info" }) {
  const Icon = icons[type];
  const color = colors[type];

  return (
    <div className={`toast toast-${type}`}>
      <Icon size={20} style={{ color }} />
      <span>{message}</span>
    </div>
  );
}
