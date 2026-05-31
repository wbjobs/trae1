import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import wasm from 'vite-plugin-wasm'
import path from 'path'

export default defineConfig({
  plugins: [react(), wasm()],
  resolve: {
    alias: {
      'hdr-wasm/pkg/hdr_wasm.js': path.resolve(__dirname, 'hdr-wasm/pkg/hdr_wasm.js'),
    },
  },
  optimizeDeps: {
    exclude: ['hdr-wasm']
  },
  server: {
    port: 3000,
    host: true
  }
})
