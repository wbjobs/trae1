import { useEffect, useRef, useState } from 'react';

export interface GamepadState {
  index: number;
  id: string;
  mapping: GamepadMappingType;
  connected: boolean;
  buttons: boolean[];
  axes: number[];
  timestamp: number;
}

export function useGamepads(pollRate = 60) {
  const [pads, setPads] = useState<GamepadState[]>([]);
  const rafRef = useRef(0);

  useEffect(() => {
    const onConnect = () => update();
    const onDisconnect = () => update();
    window.addEventListener('gamepadconnected', onConnect);
    window.addEventListener('gamepaddisconnected', onDisconnect);

    let last = 0;
    const interval = 1000 / pollRate;
    const loop = (t: number) => {
      if (t - last >= interval) {
        last = t;
        update();
      }
      rafRef.current = requestAnimationFrame(loop);
    };
    rafRef.current = requestAnimationFrame(loop);

    return () => {
      cancelAnimationFrame(rafRef.current);
      window.removeEventListener('gamepadconnected', onConnect);
      window.removeEventListener('gamepaddisconnected', onDisconnect);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [pollRate]);

  const update = () => {
    const list = navigator.getGamepads ? navigator.getGamepads() : [];
    const out: GamepadState[] = [];
    for (let i = 0; i < list.length; i++) {
      const g = list[i];
      if (!g) continue;
      out.push({
        index: g.index,
        id: g.id,
        mapping: g.mapping,
        connected: g.connected,
        buttons: g.buttons.map(b => b.pressed),
        axes: [...g.axes],
        timestamp: g.timestamp,
      });
    }
    setPads(out);
  };

  return pads;
}
