import viteReact from "@vitejs/plugin-react";
import tsConfigPaths from "vite-tsconfig-paths";
import { defineConfig } from "vite";
import { resolve } from "path";

/**
 * 预渲染专用构建配置（主构建配置见 vite.config.ts，那份是两个入口 HTML）。
 *
 * 单独一份的原因：主配置里 rollupOptions.input 是两个 HTML、output 带 hash 文件名，
 * 直接加 --ssr 会把预渲染脚本也打成 assets/prerender-[hash].js，node 无法稳定执行。
 */
export default defineConfig({
  // 必须与 vite.config.ts 的 base 一致：预渲染产物里的内链由 import.meta.env.BASE_URL 拼出，
  // 这里留空会让爬虫看到 /en/ 而不是 /agent-ping/en/（浏览器侧是对的，看不出问题）
  base: process.env.VITE_BASE ?? "/agent-ping/",
  plugins: [viteReact(), tsConfigPaths()],
  build: {
    ssr: resolve(__dirname, "src/prerender.tsx"),
    outDir: ".prerender",
    emptyOutDir: true,
    minify: false,
    target: "node20",
  },
});
