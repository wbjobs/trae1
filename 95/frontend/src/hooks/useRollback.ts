import { useEffect, useRef, useState, useCallback } from 'react';
import { TransportClient } from '../transport';
import { RollbackClient, type CorrectionEvent } from '../rollback/client';
import { FixedTimestep, type Entity } from '../physics/world';
import type { GamepadDevice } from '../useGamepads';

export interface RollbackState {
  predictedEntity: Entity | null;
  authorityPos: { x: number; y: number } | null;
  otherPlayers: Entity[];
  correctionEvents: CorrectionEvent[];
  rollbackStats: {
    rollbackCount: number;
    totalCorrectedFrames: number;
    avgCorrectionDistance: number;
    lastCorrectionTime: number;
  };
  predictionWindowMs: number;
  enabled: boolean;
}

export function useRollback(
  transport: TransportClient | null,
  gamepads: GamepadDevice[],
  options: { predictionWindowMs?: number; enabled?: boolean } = {}
): RollbackState {
  const { predictionWindowMs = 100, enabled = true } = options;
  const rollbackRef = useRef<RollbackClient | null>(null);
  const rafRef = useRef<number | null>(null);
  const lastTimeRef = useRef<number>(0);
  const accumulatorRef = useRef<number>(0);
  const posSendTimerRef = useRef<number>(0);

  const [state, setState] = useState<RollbackState>({
    predictedEntity: null,
    authorityPos: null,
    otherPlayers: [],
    correctionEvents: [],
    rollbackStats: {
      rollbackCount: 0,
      totalCorrectedFrames: 0,
      avgCorrectionDistance: 0,
      lastCorrectionTime: 0,
    },
    predictionWindowMs,
    enabled,
  });

  const correctionEventsRef = useRef<CorrectionEvent[]>([]);

  useEffect(() => {
    if (!transport || !enabled) return;

    const playerId = transport.playerId;
    const rb = new RollbackClient(playerId, predictionWindowMs);
    rollbackRef.current = rb;

    rb.setOnCorrection((ev) => {
      correctionEventsRef.current.push(ev);
      if (correctionEventsRef.current.length > 100) {
        correctionEventsRef.current = correctionEventsRef.current.slice(-100);
      }
    });

    const offState = transport.onState((st) => {
      for (const e of st.entities) {
        if (e.playerId === playerId) {
          rb.applyAuthorityState(st.frame, {
            pos: { x: e.posX, y: e.posY },
            vel: { x: e.velX, y: e.velY },
            radius: 20,
            playerId,
          });
        }
      }
    });

    const offCorr = transport.onCorrection((c) => {
      if (c.playerId === playerId) {
        rb.applyAuthorityState(c.frame, {
          pos: { x: c.correctPosX, y: c.correctPosY },
          vel: { x: c.correctVelX, y: c.correctVelY },
          radius: 20,
          playerId,
        });
      }
    });

    const loop = (t: number) => {
      if (!rollbackRef.current) return;
      const last = lastTimeRef.current || t;
      const dt = Math.min(0.1, (t - last) / 1000);
      lastTimeRef.current = t;

      accumulatorRef.current += dt;
      while (accumulatorRef.current >= FixedTimestep) {
        const pad = gamepads.find(g => g.connected);
        const axes = pad?.axes ?? [0, 0, 0, 0, 0, 0];
        const buttons = pad?.buttons ?? Array(17).fill(false);
        const input = {
          moveX: axes[0] ?? 0,
          moveY: axes[1] ?? 0,
          buttons: [...buttons],
          sequence: transport.getPollSeq(),
        };
        rollbackRef.current.applyInput(input);
        accumulatorRef.current -= FixedTimestep;
      }

      rollbackRef.current.updateInterpolation(dt);

      posSendTimerRef.current += dt;
      if (posSendTimerRef.current >= 0.05) {
        posSendTimerRef.current = 0;
        const p = rollbackRef.current.getWorld().getPlayer(playerId);
        if (p) {
          transport.sendPlayerPos(p.pos.x, p.pos.y);
        }
      }

      const p = rollbackRef.current.getWorld().getPlayer(playerId);
      const others = rollbackRef.current.getWorld().entities.filter(e => e.playerId !== playerId);

      setState(prev => ({
        ...prev,
        predictedEntity: p ? { ...p, pos: { ...p.pos }, vel: { ...p.vel } } : null,
        authorityPos: rollbackRef.current!.getLastAuthorityPos(),
        otherPlayers: others.map(e => ({ ...e, pos: { ...e.pos }, vel: { ...e.vel } })),
        correctionEvents: [...correctionEventsRef.current],
        rollbackStats: rollbackRef.current!.getStats(),
      }));

      rafRef.current = requestAnimationFrame(loop);
    };

    rafRef.current = requestAnimationFrame(loop);

    return () => {
      if (rafRef.current) cancelAnimationFrame(rafRef.current);
      offState();
      offCorr();
      rollbackRef.current = null;
    };
  }, [transport, gamepads, predictionWindowMs, enabled]);

  const setPredictionWindow = useCallback((ms: number) => {
    if (rollbackRef.current) {
      rollbackRef.current.setPredictionWindow(ms);
    }
    setState(prev => ({ ...prev, predictionWindowMs: ms }));
  }, []);

  return { ...state, setPredictionWindow } as RollbackState & { setPredictionWindow: (ms: number) => void };
}
