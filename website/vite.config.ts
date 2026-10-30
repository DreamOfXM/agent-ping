import { TanStackRouterVite } from "@tanstack/router-plugin/vite";
import tailwindcss from "@tailwindcss/vite";
import viteReact from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import tsConfigPaths from "vite-tsconfig-paths";
import { resolve } from "path";

/**
 * React + Vite 构建配置（多语言多页入口）。
 *
 * base 默认按 GitHub Pages 项目站路径（https://dreamofxm.github.io/agent-ping/）生成，
 * 若绑定自定义域名或部署到根路径，把 base 改为 "/" 即可。
 * 两个语言入口：/（中文）、/en/，与 README 的中英一一对应。
 */
export default defineConfig({
  base: process.env.VITE_BASE ?? "/agent-ping/",
  plugins: [tailwindcss(), TanStackRouterVite(), viteReact(), tsConfigPaths()],
  build: {
    outDir: "dist",
    assetsDir: "assets",
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(__dirname, "index.html"),
        en: resolve(__dirname, "en/index.html"),
      },
      output: {
        entryFileNames: "assets/[name]-[hash].js",
        chunkFileNames: "assets/[name]-[hash].js",
        assetFileNames: "assets/[name]-[hash][extname]",
      },
    },
  },
});
