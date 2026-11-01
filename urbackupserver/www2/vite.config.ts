import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { lingui } from "@lingui/vite-plugin";

// https://vitejs.dev/config/
export default defineConfig({
  // Served from the www2 folder of the classic web interface
  base: "./",
  build: {
    outDir: "../www/www2",
    emptyOutDir: true,
  },
  plugins: [
    react({
      babel: {
        plugins: ["macros"],
      },
    }),
    lingui(),
  ],
  server: {
    host: "127.0.0.1",
    proxy: {
      "/x": "http://localhost:55414",
    },
  },
});
