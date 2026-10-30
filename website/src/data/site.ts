// Agent Ping 宣传页链接常量（与语言无关）；全部文案见 src/data/content.ts。
// 推广打法与渠道台账属内部策略，不放对外页面。

export const REPO = "https://github.com/DreamOfXM/agent-ping";
// 走 latest 而不是钉某个 tag：仓库每次 bump 都要改这里，漏一次页脚就指到旧版本说明。
// 前提是至少有一个非 draft 的 Release，否则 /releases/latest 会 404。
export const RELEASE_TAG = `${REPO}/releases/latest`;
export const ISSUES = `${REPO}/issues`;

// 安装四句与 README 的 sh 块逐字一致：页面上显示的、剪贴板里的、README 里的三者同串。
// 不用 && 串成一行——那一摆在窄卡里必然被省略号截断，而截断后的命令贴进终端是跑不通的。
export const BREW_TAP = "brew tap DreamOfXM/agent-ping https://github.com/DreamOfXM/agent-ping";
export const BREW_TRUST = "brew trust dreamofxm/agent-ping";
export const BREW_INSTALL = "brew install agent-ping";
export const BREW_RUN = "agent-ping";
export const BREW_CMD = `${BREW_TAP}\n${BREW_TRUST}\n${BREW_INSTALL}\n${BREW_RUN}`;

export const MAC_BUILD = "cmake -S . -B build -G Ninja && cmake --build build && sh Tools/make_bundle.sh";
export const LINUX_BUILD = "cmake -S . -B build-linux -G Ninja && cmake --build build-linux";

// README 里那一节：它读哪些文件、写哪些文件，全表摊开。
// 锚点取自 GitHub 渲染后的 heading id（2026-10-30 用 contents API 实读两遍），不是按规则猜的。
export const PRIVACY: Record<string, string> = {
  zh: `${REPO}/blob/main/README.zh-CN.md#隐私它到底读什么`,
  en: `${REPO}#privacy--exactly-what-it-reads`,
};
export const LIMITATIONS = `${REPO}#known-limitations`;
export const READMES: Record<string, string> = {
  zh: `${REPO}/blob/main/README.zh-CN.md`,
  en: `${REPO}/blob/main/README.md`,
};
