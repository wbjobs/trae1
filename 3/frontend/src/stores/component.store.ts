import { create } from 'zustand';
import type { Component } from '../types';

interface ComponentState {
  components: Component[];
  loading: boolean;
  error: string | null;
  setComponents: (list: Component[]) => void;
  setLoading: (v: boolean) => void;
  setError: (err: string | null) => void;
}

export const useComponentStore = create<ComponentState>((set) => ({
  components: [],
  loading: false,
  error: null,
  setComponents: (list) => set({ components: list }),
  setLoading: (v) => set({ loading: v }),
  setError: (err) => set({ error: err }),
}));
