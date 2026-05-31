import { GamepadState } from '../useGamepads';

interface Props {
  pad: GamepadState;
  layout: string;
  onLayoutChange: (l: string) => void;
}

const BUTTON_LABELS = ['A', 'B', 'X', 'Y', 'LB', 'RB', 'LT', 'RT', 'Back', 'Start', 'LS', 'RS', 'D^', 'Dv', 'D<', 'D>', 'Guide'];

export default function GamepadCard({ pad, layout, onLayoutChange }: Props) {
  const lx = pad.axes[0] ?? 0;
  const ly = pad.axes[1] ?? 0;
  const rx = pad.axes[2] ?? 0;
  const ry = pad.axes[3] ?? 0;
  const lt = pad.axes[4] ?? 0;
  const rt = pad.axes[5] ?? 0;

  return (
    <div className="pad-card">
      <h3>Pad {pad.index} — {pad.id.slice(0, 40)}</h3>
      <div className="row">
        <label>布局</label>
        <select value={layout} onChange={(e) => onLayoutChange(e.target.value)}>
          <option value="xbox">Xbox</option>
          <option value="ps5">PS5</option>
          <option value="switch">Switch Pro</option>
        </select>
        <span style={{ fontSize: 12, color: '#888' }}>mapping: {pad.mapping}</span>
      </div>

      <div className="btn-grid">
        {BUTTON_LABELS.map((lbl, i) => (
          <div key={i} className={'btn ' + (pad.buttons[i] ? 'on' : '')}>{lbl}</div>
        ))}
      </div>

      <div className="row">
        <div>
          <div style={{ fontSize: 12, color: '#888', marginBottom: 4 }}>L-Stick</div>
          <span className="stick" style={{ ['--x' as any]: lx, ['--y' as any]: ly }} />
        </div>
        <div>
          <div style={{ fontSize: 12, color: '#888', marginBottom: 4 }}>R-Stick</div>
          <span className="stick" style={{ ['--x' as any]: rx, ['--y' as any]: ry }} />
        </div>
      </div>

      <div style={{ marginTop: 10 }}>
        <div style={{ fontSize: 12, color: '#888' }}>LT {(lt * 100).toFixed(0)}%</div>
        <div className="trigger"><span style={{ width: `${lt * 100}%` }} /></div>
        <div style={{ fontSize: 12, color: '#888', marginTop: 6 }}>RT {(rt * 100).toFixed(0)}%</div>
        <div className="trigger"><span style={{ width: `${rt * 100}%` }} /></div>
      </div>
    </div>
  );
}
