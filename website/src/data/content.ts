// 多语言内容层：zh 为源语言，en 与两份 README 保持同一口径。
// 每条对外主张都要能在 README 或竞品自己的 README 里找到原句；核不到的整条不写。
// 竞品读数取自 2026-10-30 现读的 GitHub API 与各家 README 原文。
import demoGifEn from "@/assets/tour-en.gif";
import demoGifZh from "@/assets/tour-zh.gif";
import { BREW_CMD, LINUX_BUILD, MAC_BUILD } from "./site";

export type Locale = "zh" | "en";

export const LOCALES: { code: Locale; label: string; short: string; path: string }[] = [
  { code: "zh", label: "简体中文", short: "中", path: "" },
  { code: "en", label: "English", short: "EN", path: "en" },
];

export interface SiteContent {
  htmlLang: string;
  demoGif: string;
  nav: { href: string; label: string }[];
  navStar: string;
  hero: {
    badge: string;
    h1: string;
    subtitle: string;
    tagline: string;
    promise: string;
    brewTitle: string;
    primaryBtn: string;
    secondaryBtn: string;
    stats: { num: string; label: string }[];
    figcaption: string;
    fallbackTitle: string;
    fallbackNote: string;
  };
  why: { eyebrow: string; title: string };
  differentiators: { icon: string; title: string; desc: string }[];
  market: {
    eyebrow: string;
    title: string;
    intro: string;
    claimLabel: string;
    headers: string[];
    rows: {
      name: string;
      stars: string;
      kind: string;
      tools: string;
      jump: string;
      quota: string;
      network: string;
      ours?: boolean;
    }[];
    sourceNote: string;
  };
  positioning: { claim: string; logic: [string, string][] };
  features: { eyebrow: string; title: string; intro: string; items: { title: string; desc: string }[] };
  install: {
    eyebrow: string;
    title: string;
    sub: string;
    channels: { name: string; desc: string; action: string }[];
    note: string;
  };
  cta: {
    eyebrow: string;
    title: string;
    body: string;
    starBtn: string;
    issueBtn: string;
    feedbackLead: string;
    issueNote: string;
  };
  faq: { eyebrow: string; title: string; items: { q: string; a: string }[] };
  footer: {
    desc: string;
    meta: string;
    getLabel: string;
    links: { label: string }[];
    copyright: string;
    langLabel: string;
  };
}

const zh: SiteContent = {
  htmlLang: "zh-CN",
  demoGif: demoGifZh,
  nav: [
    { href: "#why", label: "凭什么" },
    { href: "#market", label: "同类对比" },
    { href: "#features", label: "屏上有什么" },
    { href: "#install", label: "安装" },
    { href: "#faq", label: "常见疑问" },
  ],
  navStar: "Star",
  hero: {
    badge: "Free · Open Source · Apache-2.0",
    h1: "谁在跑、谁在等你，|一眼看完",
    subtitle: "六家 AI 编码工具的状态放在同一块菜单栏面板上；双击一行，人就回到那条会话。",
    tagline:
      "Agent Ping 读的是 Qoder、Claude Code、Codex、ZCode、WorkBuddy、OpenCode 已经写在你机器上的日志与本地库：谁在跑、谁在排队、谁报错停了、谁正等你回答、哪家的券能领。然后在你没看屏幕的时候提醒一声，把那一条递到你面前。",
    promise: "零网络调用：整个二进制没有 connect / socket 的未定义符号，nm -u 自己就能查。额度不联网查，也不碰钥匙串。",
    brewTitle: "点击复制安装命令",
    primaryBtn: "在 GitHub 上看源码",
    secondaryBtn: "它到底读你哪些文件",
    stats: [
      { num: "6", label: "家工具同屏：Qoder CLI · Claude Code · Codex · ZCode · WorkBuddy · OpenCode" },
      { num: "0", label: "次网络调用 · 0 个账号 · 0 行遥测" },
      { num: "6", label: "种界面语言开箱即用（zh · en · de · it · ja · ko）" },
      { num: "2", label: "个平台共用一套面板与判定：macOS 13+ 与 Linux（X11）" },
    ],
    figcaption:
      "三张屏都由本仓库编出的同一个二进制离屏渲染（AGENTPING_FIXTURE=1），不是摆拍；右上角「样例数据」是产品自己打的标签——那些数字不来自任何真机器。",
    fallbackTitle: "看总览 / 额度 / 权益三屏",
    fallbackNote: "演示动图没加载出来，点这里在仓库里看同一批渲染图",
  },
  why: { eyebrow: "Why Agent Ping", title: "四件同类今天不做的事" },
  differentiators: [
    {
      icon: "gift",
      title: "券到账了，面板上就有",
      desc: "WorkBuddy 的 [Checkin]、Qoder 的 [Campaign]、ZCode 今天的日志——三家自己写进磁盘的领取状态放在同一屏，能领的排最前，卡面带面值、连签天数和本期到期倒计时。点整张卡只把那家 App 唤到前台，并明写「领取要在那家 App 里自己点」：这一层绝不替你按领取。",
    },
    {
      icon: "gauge",
      title: "额度只从本机日志读",
      desc: "5 小时与本周剩余、何时重置，全部取自各家自己写的字段（比如 Codex 运行中落的 rate_limits 事件）。不调计费 API、不要 cookie、不进钥匙串；读数超过 10 分钟就退出首屏，挪到额度屏并标上 updated HH:MM——「我们没刷新」和「那家没写」是两个答案。",
    },
    {
      icon: "scale",
      title: "每个数字都写清算法",
      desc: "「Turn 77」是数 type 为 user 且不带 toolUseResult 的记录条数；「41.4 Credits」是 Qoder 自己的计数字段，不是钱，没有这个字段的家贡献 0。源里没有的字段整格不画，不填 0，也不折算成美元——面板上直接印着口径。",
    },
    {
      icon: "click",
      title: "双击才跳，跳了就落到那条会话",
      desc: "误点一次只当选中，0.5 秒内同一行的第二下才触发跳转；macOS 走那家的 URL scheme、AppleScript，或者驱动它自己的 ⌘K 面板。卡片不随刷新移动——每条会话第一次被看见时拿到固定序号，所以不会从光标底下跑掉。",
    },
  ],
  market: {
    eyebrow: "同类对比 · 2026-10-30 现读",
    title: "这个品类里已经站着谁",
    intro:
      "下面每一格都抄自对方自己的 README 或官网原文，星数与最后提交日期取自 2026-10-30 的 GitHub API。原文没写的格子写「未提」——那不是短板，那是没取证。",
    claimLabel: "定位主张：",
    headers: ["项目", "★", "平台与形态", "覆盖哪些工具", "能不能回到那条会话", "额度从哪来", "要不要联网"],
    rows: [
      {
        name: "Agent Ping（本项目）",
        stars: "—",
        kind: "macOS 13+ 与 Linux（X11）· C++/Dear ImGui 菜单栏浮层 · 仓库内 Formula 源码编译",
        tools: "Qoder CLI · Claude Code · Codex · ZCode · WorkBuddy · OpenCode",
        jump: "双击一行落到那条会话（macOS）；Linux 侧目前只到唤起 App 或打开工作目录",
        quota: "只读本机日志，并标读数多旧",
        network: "不联网：nm -u 里没有 connect / socket / getaddrinfo，也没有 HTTP 客户端",
        ours: true,
      },
      {
        name: "peon-ping",
        stars: "5,096",
        kind: "macOS · Linux · Windows · WSL2 · SSH · 语音包与横幅",
        tools: "Claude Code、Amp、Copilot、Codex、Grok Build、Cursor、OpenCode、Kilo、Kiro、Kimi、Windsurf、Antigravity 等 20+，以及任何 MCP client",
        jump: "README 未提会话级跳转（它给的是语音台词与横幅）",
        quota: "README 未提",
        network: "装声音包时要从 registry 下载",
      },
      {
        name: "CodeIsland",
        stars: "2,528",
        kind: "macOS 14+ · 刘海里的状态条 · DMG + iPhone/Watch 伴生 App",
        tools: "自称 30+，含 Claude Code、Codex、Cursor、Gemini CLI、Qoder、ZCode、WorkBuddy、OpenCode",
        jump: "「click once to jump to the exact terminal tab」，并能在岛上直接回答审批与提问",
        quota: "用量统计 + 可选的 plan limits（5 小时与每周）",
        network: "Sparkle 更新检查；开 plan limits 会带着你的登录态请求 api.anthropic.com；可配 webhook 转发",
      },
      {
        name: "claude-status-bar",
        stars: "722",
        kind: "macOS · 菜单栏图标 + 下拉列表 · 靠 Claude Code 的 hooks",
        tools: "Claude Code 一家",
        jump: "「Precise per-tab focus is in progress」（README 里挂着 issue #19）",
        quota: "README 未提",
        network: "无服务端；自身无状态，Claude Code 一开就自启、没有会话就自退",
      },
      {
        name: "lockpaw",
        stars: "168",
        kind: "macOS · 快捷键盖住屏幕的锁屏层",
        tools: "Claude Code、Codex、Gemini 等（README 列到这几家）",
        jump: "不做跳转：锁屏上每家一行，写谁在等你",
        quota: "README 未提",
        network: "README 未提",
      },
      {
        name: "muster",
        stars: "72",
        kind: "macOS 13+ · 菜单栏玻璃浮层 · DMG + brew cask",
        tools: "Claude Code、Codex、Cursor",
        jump: "不跳转，就地回答：⌥↓ 看它为什么问，⌥↩ 逐条答",
        quota: "README 未提",
        network: "README 未提",
      },
      {
        name: "Agent-Signal-Bar",
        stars: "71",
        kind: "macOS · 红绿灯 + 桌面悬浮灯 · ad-hoc 签名未公证",
        tools: "以 Codex 为主（Desktop、CLI/TUI、VS Code、Xcode、IDEA），其他走 hooks / 自带 CLI / JSON 事件",
        jump: "README 未提会话级跳转",
        quota: "额度与成本视图；可选功能直连 OpenAI，可用本机 Codex 凭据或 chatgpt.com 浏览器会话数据，凭据存进 macOS 钥匙串",
        network: "有联网取额度的路径",
      },
      {
        name: "your-turn",
        stars: "5",
        kind: "macOS · 菜单栏 · 最后一次提交 2026-02-20",
        tools: "Claude Code 一家",
        jump: "「brings the right terminal session to front with one click」",
        quota: "README 未提",
        network: "README 未提",
      },
    ],
    sourceNote:
      "读数时间 2026-10-30。星数与 pushed_at 来自 https://api.github.com/repos/<owner>/<repo>，其余引自各仓库 README 原文；「未提」表示那份文档里没有这句话，不代表该功能不存在。",
  },
  positioning: {
    claim: "把「轮到谁」这件事一次看全：状态、额度、券，全部只从你自己磁盘上已经存在的字段里读出来。",
    logic: [
      [
        "空位在哪",
        "权益/券这一格，上面七家的 README 全文检索 reward、claim、check-in 三个词零命中（2026-10-30）。额度这一格有两家要做，但都要联网或要凭据；只从本机日志读、并且把读数新鲜度印在卡上的，目前没有第二家。",
      ],
      [
        "我们不如人的三处",
        "覆盖：CodeIsland 自称 30+、peon-ping 20+，我们只有六家。就地回答：CodeIsland 与 muster 能在提示上直接答复，我们只把原文和选项显示出来、再把你送回那条会话。远程：CodeIsland 支持 tmux、zellij 与 SSH 主机，我们没有。这三条是产品决定，不是文案能补的。",
      ],
      [
        "为什么还值得装",
        "一次扫描同时给出六家的状态、额度、券；不联网、不装后台守护；默认不写你的任何配置文件——只有你按下 Connect（或自己跑 Tools/install_hook.sh）才合并一条 hook，且先落一份带时间戳的备份。",
      ],
      [
        "为谁而做",
        "同时开着好几家 Agent、靠切窗口确认「是不是轮到我了」的人；不希望额度数字来自第三方 API 的人；以及在 Linux 桌面上用 Codex CLI / OpenCode 的人——上面那几家多数只有 macOS。",
      ],
    ],
  },
  features: {
    eyebrow: "What you get",
    title: "一个窗口，每屏都来自同一轮扫描",
    intro:
      "顶部那条工具栏按家筛选，下面每一屏都是同一轮扫描的另一个视角：谁在等你、还剩多少额度、哪张券要过期、今天完成了什么。",
    items: [
      { title: "Sessions 总览", desc: "一张在跑的卡（轮次、子 Agent、本会话 credits）、额度预览、按最久等待排序的「Needs you」队列，页脚是今天完成的条数与逐小时分布。" },
      { title: "Usage & limits", desc: "今天的 token 总数，外加每家一张卡：5 小时与本周剩余、何时重置、这条读数几点取的。WorkBuddy 刷新失败时明写「Last reading · not refreshed」。" },
      { title: "Rewards 权益", desc: "三家的券卡：能领的排最前，面值、连签天数、本期到期倒计时都印在卡上；只知道什么时候出现过、不知道什么时候到期的那家，进度轨画成虚线。" },
      { title: "Detail 详情", desc: "任意一行按 ⓘ：等你回答的原文与它的选项，或者失败的那条命令原文。" },
      { title: "History 历史", desc: "今天完成的会话，新的在前，12 条一页，往下滚就续。" },
      { title: "Tools 六路源", desc: "本机装了哪几家采集器、这一轮各读到什么，逐条摊开——某家一片空白时，你能看出是没装还是没写。" },
      { title: "提醒层", desc: "每个 (agent, session) 只提醒一次，账本落盘所以重启不重刷；每轮最多发 4 条，其余留到后面几轮而不是丢掉；免打扰只压送达、不清账本。券到账走同一账本的另一个 key，横幅常驻，不点不收。" },
      { title: "两个平台、六种语言", desc: "macOS 用 NSStatusItem 与系统通知中心，Linux 用 StatusNotifierItem（D-Bus）与 notify-send；面板、扫描层、提醒判定是同一份代码。界面六语，字体按字号现场烘焙，中文不是外挂。" },
      { title: "口径印在屏上", desc: "每个数字旁边都有它的定义与来源；托盘上「2 · 5」这种三位计数按固定顺序排，空位不画，哪一格是什么由悬停说明。" },
    ],
  },
  install: {
    eyebrow: "Install",
    title: "装法三种，都不花钱",
    sub: "macOS 13+ 用仓库里的 Homebrew Formula；Linux（Debian 12 实测）只有源码构建这一条，没有对应的 formula；Windows 暂不支持。没有预编译包可下载。",
    channels: [
      {
        name: "Homebrew（macOS）",
        desc: "Formula 就住在这个仓库的 Formula/agent-ping.rb，按 v0.1.1 标签的源码现场编译。第一次装需要 Xcode 命令行工具，Apple 芯片上约一分钟。macOS 26 上若 brew 停在 Error: Xcode alone is not sufficient，那是 Homebrew 自己的准入检查，跑一次 xcode-select --install 再重试。",
        action: BREW_CMD,
      },
      {
        name: "macOS 源码构建",
        desc: "依赖 brew install cmake ninja glfw freetype；产出 build/AgentPing-cpp.app，再跑一次 Tools/make_bundle.sh 打包。",
        action: MAC_BUILD,
      },
      {
        name: "Linux 源码构建",
        desc: "Debian 12 包名：cmake ninja-build libglfw3-dev libfreetype6-dev libsqlite3-dev libgl1-mesa-dev libdbus-1-dev。缺 SQLite3 会跳过 ZCode、WorkBuddy、OpenCode 并在构建时警告。",
        action: LINUX_BUILD,
      },
    ],
    note: "brew trust 只需要跑一次：这是第三方 tap，Homebrew 在你没点头之前不肯按短名加载它的 formula。不跑这句，就只有 dreamofxm/agent-ping/agent-ping 这个全名可用，而 brew upgrade agent-ping 会直接拒绝执行。装好后 agent-ping 是启动器，App 住在 Homebrew 的 opt 目录而不是 /Applications——Homebrew 在沙箱里编译，写不进 /Applications。",
  },
  cta: {
    eyebrow: "Star & Feedback",
    title: "觉得有用？留一颗星",
    body: "Agent Ping 是独立维护的开源项目。如果它帮你少切了几次窗口，欢迎去 GitHub 点一颗星——星多了，下一个同时开着六家 Agent、不知道该看哪一边的人，才更容易搜到它。",
    starBtn: "去 GitHub 留一颗星",
    issueBtn: "提一个 Issue",
    feedbackLead: "意见也走 Issue：缺哪家工具、哪个数字口径看不懂、跳转落点不对，中英文都可以，每一条都会读。已知缺口先查 README 的 Known limitations，那里面每条都写了成因。",
    issueNote: "它读哪些文件、写哪些文件、什么时候写，README 里有全表；不放心可以自己对一个编出来的二进制跑 nm -u。",
  },
  faq: {
    eyebrow: "FAQ",
    title: "常见疑问",
    items: [
      {
        q: "Claude Code 自己有通知钩子和 Remote Control，为什么还要这个？",
        a: "那些是单家的：hooks 只报 Claude Code，Remote Control 要经 claude.ai 那一侧把推送打到手机。Agent Ping 是一次看六家，其中 ZCode、WorkBuddy、OpenCode 根本没有钩子通道，只能读它们自己的本地库。Codex 的 osc9 提醒只在终端聚焦时才有效。跳转落点是这条链的最后一环：响一声之后，你还得自己找到那条会话。",
      },
      {
        q: "它会动我的配置文件吗？",
        a: "默认不动。只有你在 Connect 屏按下按钮，或者自己跑 Tools/install_hook.sh install claude|qoder 时才写，顺序是：先在自己的数据目录里原子写出 hook.sh（0755），再把目标配置备份成 <config>.bak-agentping-<YYYYmmdd-HHMMSS>，最后往 hooks.Notification 里按 matcher 合并一条并原子写回。Codex 的 notify 槽位只检测：如果它属于别的工具，面板会说明并停手，不碰那个文件。钩子本身只把 CLI 的 JSON 载荷拷进收件箱，从不替你回答，收件箱写不进也照样 exit 0——钩子的退出码会被 CLI 读成一个决定。",
      },
      {
        q: "为什么没有下载好的二进制？",
        a: "这个仓库不发预编译包：Formula 从 v0.1.1 标签的源码现场编译。好处是任何人用同一条命令就能复现你机器上那个 App，坏处是第一次装要等约一分钟编译，而且需要 Xcode 命令行工具。",
      },
      {
        q: "额度数字是不是猜的？",
        a: "不是猜的，也不是查来的：只读各家自己写进本地日志的字段，例如 Codex 运行中落的 rate_limits 事件。所以工具闲置时读数会旧——超过 10 分钟就退出首屏、挪到额度屏并标 updated HH:MM。没有这个字段的家，那一格整格不画。",
      },
      {
        q: "它会替我点领取吗？",
        a: "不会。权益屏是只读的，点卡片只把那家 App 唤到前台，屏幕上明写「领取要在那家 App 里自己点」。ZCode 的本期额度由它自己的 App 落盘（`~/.zcode/v2/logs/<日期>.log` 里的 `billing/balance` 响应），面板读的就是那条——但还没领的那一期不会写进这份日志，所以面板不可能提前预告，卡上那时只说「本机没有活期」。面值是本期发的总额而非剩余额度，且本机没有日志记录「领取」这个动作。",
      },
      {
        q: "支持哪些工具？Linux 上到什么程度？",
        a: "六家：Qoder CLI、Claude Code、Codex、ZCode、WorkBuddy、OpenCode，后三家需要带 SQLite3 的构建。Linux 走 X11：横幅用 notify-send，声音按 paplay / pw-play / ffplay / canberra-gtk-play / aplay / cxplay 里装了哪个来选，装了 libdbus-1-dev 才是真的 StatusNotifierItem 托盘项。这两条要说清楚：Linux 侧的验证是在 Debian 12 容器里完成的，没跑过真 KDE/GNOME 桌面；横幅点击在 Linux 上不跳会话，也没有 ⌘K 面板驱动。",
      },
      {
        q: "怎么确认它真的不联网？",
        a: "对你自己编出来的二进制跑 nm -u 看未定义符号：没有 connect、sendto、getaddrinfo、socket，也没有任何 HTTP 客户端。Linux 构建里唯一带 connect 字样的符号是 dbus_connection_*，那是本机 IPC。它也不读钥匙串、不读环境变量里的密钥、不读 shell 历史。",
      },
      {
        q: "会话标题会不会被上传？",
        a: "没有地方可传：没有任何网络路径。但标题会显示在屏幕上，提醒横幅引用的就是标题原文，所以请把这块面板当成你自己的终端回滚缓冲区来看待。",
      },
    ],
  },
  footer: {
    desc: "免费开源的 AI 编码工具状态面板。六家 Agent 同屏，双击回到那条会话；额度与券只从本机日志读，零网络调用。",
    meta: "Apache-2.0 · macOS 13+ · Linux (X11) · 6 UI languages",
    getLabel: "获取",
    links: [
      { label: "GitHub Release（最新发布）" },
      { label: "源码 / Issue / PR" },
      { label: "它读哪些文件（Privacy）" },
      { label: "简体中文 README" },
    ],
    copyright:
      "© 2026 DreamOfXM · Agent Ping 是独立开源项目，与 Qoder、Claude Code（Anthropic）、Codex（OpenAI）、ZCode、WorkBuddy、OpenCode 各家均无隶属关系；文中提到的产品名与商标归各自所有者。",
    langLabel: "语言",
  },
};

const en: SiteContent = {
  htmlLang: "en",
  demoGif: demoGifEn,
  nav: [
    { href: "#why", label: "Why" },
    { href: "#market", label: "Compare" },
    { href: "#features", label: "Screens" },
    { href: "#install", label: "Install" },
    { href: "#faq", label: "FAQ" },
  ],
  navStar: "Star",
  hero: {
    badge: "Free · Open Source · Apache-2.0",
    h1: "Who is running, and who is waiting on you",
    subtitle:
      "Six AI coding tools on one menu-bar panel. Double-click a row and you are back in that session.",
    tagline:
      "Agent Ping reads the logs and local databases that Qoder, Claude Code, Codex, ZCode, WorkBuddy and OpenCode already write on your machine: who is working, who is queued, who stopped on an error, who is sitting there waiting for an answer, and who just put up a claimable reward. Then it alerts you while you are not looking, and puts that one in front of you.",
    promise:
      "Zero network calls: the binary has no undefined connect / socket symbols — check it yourself with nm -u. Quota is never looked up online, and the Keychain is never opened.",
    brewTitle: "Click to copy the install command",
    primaryBtn: "View the source on GitHub",
    secondaryBtn: "Exactly what it reads",
    stats: [
      { num: "6", label: "tools on one screen: Qoder CLI · Claude Code · Codex · ZCode · WorkBuddy · OpenCode" },
      { num: "0", label: "network calls · 0 accounts · 0 telemetry" },
      { num: "6", label: "UI languages out of the box (zh · en · de · it · ja · ko)" },
      { num: "2", label: "platforms sharing one panel and one alert judgement: macOS 13+ and Linux (X11)" },
    ],
    figcaption:
      "All three screens are rendered offscreen by the same binary this repository builds (AGENTPING_FIXTURE=1), not staged. The Sample data pill in the header is the product's own label: those numbers come from no real machine. The session titles inside are Chinese — the fixture's sample text is written once, in one language, and the panel only translates its own chrome.",
    fallbackTitle: "See the Sessions, Usage and Rewards screens",
    fallbackNote: "The demo GIF failed to load — click to view the same renders in the repository",
  },
  why: { eyebrow: "Why Agent Ping", title: "Four things the category does not do" },
  differentiators: [
    {
      icon: "gift",
      title: "When a reward lands, it is on screen",
      desc: "WorkBuddy's [Checkin], Qoder's [Campaign], ZCode's daily log — the claimable state each vendor writes into its own store, on one screen, claimable-first, with face value, streak and this period's expiry countdown on the card. Tapping the card only brings that vendor's app to the front and says so: this layer never presses Claim for you.",
    },
    {
      icon: "gauge",
      title: "Quota read off the local logs only",
      desc: "The 5-hour and weekly windows and their reset times come from fields each tool writes itself — Codex, for instance, logs rate_limits events while it runs. No billing API, no cookies, no Keychain. A reading older than 10 minutes leaves the first screen and moves to the Usage view with its updated HH:MM stamp, so \"we did not refresh\" and \"that vendor wrote nothing\" stay two different answers.",
    },
    {
      icon: "scale",
      title: "Every number states how it was counted",
      desc: "\"Turn 77\" counts records of type user that carry no toolUseResult key; \"41.4 Credits\" is Qoder's own counter field — not money, and vendors without that field contribute 0. Where a source has no field, the cell is not drawn at all: no zero, no dollar estimate. The definition is printed on the panel.",
    },
    {
      icon: "click",
      title: "Double-click to jump, and it lands on the session",
      desc: "One mis-click only selects; a row fires on a second click on the same row within 0.5 s. On macOS the landing uses that tool's URL scheme, AppleScript, or drives its own ⌘K palette. Cards never move between re-scans: each session gets a stable sequence number the first time the panel sees it, so nothing slips out from under the cursor.",
    },
  ],
  market: {
    eyebrow: "Category · read live 2026-10-30",
    title: "Who already stands in this category",
    intro:
      "Every cell below is copied from that project's own README or site; stars and last-push dates come from the GitHub API on 2026-10-30. A cell that says \"not stated\" means their document does not say it — that is not a weakness, it is an unverified claim we are not making.",
    claimLabel: "Positioning: ",
    headers: ["Project", "★", "Platform & shape", "Tools covered", "Back to the session", "Where quota comes from", "Network"],
    rows: [
      {
        name: "Agent Ping (this project)",
        stars: "—",
        kind: "macOS 13+ and Linux (X11) · C++/Dear ImGui menu-bar panel · built from the formula in this repo",
        tools: "Qoder CLI · Claude Code · Codex · ZCode · WorkBuddy · OpenCode",
        jump: "Double-click a row onto the session (macOS); on Linux it lands on activating the app or opening the working directory",
        quota: "Local logs only, with the reading's age on screen",
        network: "None: no connect / socket / getaddrinfo in nm -u, no HTTP client",
        ours: true,
      },
      {
        name: "peon-ping",
        stars: "5,096",
        kind: "macOS · Linux · Windows · WSL2 · SSH · voice packs and banners",
        tools: "Claude Code, Amp, Copilot, Codex, Grok Build, Cursor, OpenCode, Kilo, Kiro, Kimi, Windsurf, Antigravity and 20+ more, plus any MCP client",
        jump: "not stated (it gives you voice lines and on-screen banners)",
        quota: "not stated",
        network: "downloads sound packs from a registry",
      },
      {
        name: "CodeIsland",
        stars: "2,528",
        kind: "macOS 14+ · status bar in the MacBook notch · DMG + iPhone/Watch companion",
        tools: "claims 30+, including Claude Code, Codex, Cursor, Gemini CLI, Qoder, ZCode, WorkBuddy, OpenCode",
        jump: "\"click once to jump to the exact terminal tab\", and it answers approvals and questions inside the island",
        quota: "usage stats plus opt-in plan limits (5-hour and weekly)",
        network: "Sparkle update checks; with plan limits on it calls api.anthropic.com using your own Claude Code login; optional webhook forwarding",
      },
      {
        name: "claude-status-bar",
        stars: "722",
        kind: "macOS · menu-bar icon with a dropdown · driven by Claude Code hooks",
        tools: "Claude Code",
        jump: "\"Precise per-tab focus is in progress\" — issue #19 is linked from their README",
        quota: "not stated",
        network: "no server; the app is stateless and starts when a Claude Code session does",
      },
      {
        name: "lockpaw",
        stars: "168",
        kind: "macOS · a lock layer over the screen, on a hotkey",
        tools: "Claude Code, Codex, Gemini (as listed in their README)",
        jump: "no jump: the lock screen shows one quiet line per agent, saying who is waiting",
        quota: "not stated",
        network: "not stated",
      },
      {
        name: "muster",
        stars: "72",
        kind: "macOS 13+ · glass menu-bar overlay · DMG + brew cask",
        tools: "Claude Code, Codex, Cursor",
        jump: "answers in place instead: ⌥↓ shows why it asks, two presses of ⌥↩ answer them",
        quota: "not stated",
        network: "not stated",
      },
      {
        name: "Agent-Signal-Bar",
        stars: "71",
        kind: "macOS · traffic light plus a floating desktop signal · ad-hoc signed, not notarized",
        tools: "Codex first (Desktop, CLI/TUI, VS Code, Xcode, IDEA); others via hooks, its CLI or JSON events",
        jump: "not stated",
        quota: "quota and cost views; the optional features connect to OpenAI and can use local Codex credentials or chatgpt.com browser-session data, stored in the macOS Keychain",
        network: "has an online path for quota",
      },
      {
        name: "your-turn",
        stars: "5",
        kind: "macOS · menu bar · last commit 2026-02-20",
        tools: "Claude Code",
        jump: "\"brings the right terminal session to front with one click\"",
        quota: "not stated",
        network: "not stated",
      },
    ],
    sourceNote:
      "Read 2026-10-30. Stars and pushed_at come from https://api.github.com/repos/<owner>/<repo>; everything else is quoted from each repository's README. \"not stated\" means that document does not contain the claim — not that the feature is missing.",
  },
  positioning: {
    claim: "One glance at whose turn it is — state, quota and rewards — read only from fields already on your own disk.",
    logic: [
      [
        "The gap",
        "Rewards: searching reward, claim and check-in across all seven READMEs above returns zero hits (2026-10-30). Quota: two of them do show it, but only by going online or asking for credentials. Reading it off the local logs and stamping how old the reading is — nobody else does that.",
      ],
      [
        "Where we are behind",
        "Coverage: CodeIsland claims 30+, peon-ping 20+, we have six. Answering in place: CodeIsland and muster let you reply to the prompt where it sits; we show the verbatim question and its options and then put you back in that session. Remote work: CodeIsland handles tmux, zellij and SSH hosts; we do not. Those are product decisions, not copy.",
      ],
      [
        "Why install it anyway",
        "Six tools' state, quota and reward cards from one scan, entirely offline, with no background daemon. It does not touch your config files unless you ask: the hook is written only when you press Connect or run Tools/install_hook.sh yourself, and the target config is backed up with a timestamp first.",
      ],
      [
        "Who it is for",
        "People who keep several agents open and switch windows to find out whether one of them is waiting; people who do not want their quota figure to come from a third-party API; and anyone on a Linux desktop running Codex CLI or OpenCode — most of the projects above are macOS only.",
      ],
    ],
  },
  features: {
    eyebrow: "What you get",
    title: "One window, every screen from the same scan round",
    intro:
      "The rail at the top filters by tool; everything below it is another view over one scan round — whose turn it is, how much quota is left, which reward expires, what finished today.",
    items: [
      { title: "Sessions overview", desc: "One running card (turn, sub-agents, session credits), an account quota preview, the Needs-you queue longest-waiting first, and a footer of today's completions with their hourly spread." },
      { title: "Usage & limits", desc: "Today's token total plus one card per tool: 5h and weekly remaining, when each resets, and what time the reading was taken. When WorkBuddy's refresh fails, the card says Last reading · not refreshed." },
      { title: "Rewards", desc: "Three vendors' reward cards with whatever is claimable now sorted first: face value, streak and this period's expiry on the card. For the vendor that only reports when a reward appeared, the track is drawn dashed." },
      { title: "Detail", desc: "Press ⓘ on any row: the verbatim question and its options, or the command that failed." },
      { title: "History", desc: "Completed sessions, newest first, 12 per page, further pages load as you scroll." },
      { title: "Tools", desc: "Which collectors exist on this machine and what each one yielded this round, laid out row by row — so an empty vendor tells you whether it is not installed or simply wrote nothing." },
      { title: "Alert layer", desc: "One alert per (agent, session) key, ledger on disk so a restart does not re-ping the board; at most 4 per round with the rest held rather than dropped; DND suppresses delivery without clearing the ledger. Reward arrivals ride the same ledger under a claim:<vendor>:<period> key, and the banner stays until you dismiss it." },
      { title: "Two platforms, six languages", desc: "NSStatusItem and the notification centre on macOS; StatusNotifierItem over D-Bus and notify-send on Linux — one code path for the panel, the scan layer and the alert judgement. Six UI languages, fonts baked per size at startup, so CJK is not a bolt-on." },
      { title: "Definitions on screen", desc: "Every number carries how it was counted and where it came from; the tray's fixed-order counters omit empty slots, and the tooltip says which slot is which." },
    ],
  },
  install: {
    eyebrow: "Install",
    title: "Three ways in, all free",
    sub: "macOS 13+ uses the Homebrew formula in this repository; Linux (verified on Debian 12) has source builds only — there is no formula for it; Windows is not supported. There is no prebuilt binary to download.",
    channels: [
      {
        name: "Homebrew (macOS)",
        desc: "Formula/agent-ping.rb lives in this repo and compiles the source at the v0.1.1 tag. The first install needs the Xcode command line tools and takes about a minute on an Apple-silicon Mac. If brew stops with Error: Xcode alone is not sufficient on macOS 26, that is Homebrew's own gate: run xcode-select --install once and retry.",
        action: BREW_CMD,
      },
      {
        name: "Build on macOS",
        desc: "Dependencies: brew install cmake ninja glfw freetype. Produces build/AgentPing-cpp.app; run Tools/make_bundle.sh once more to package it.",
        action: MAC_BUILD,
      },
      {
        name: "Build on Linux",
        desc: "Debian 12 packages: cmake ninja-build libglfw3-dev libfreetype6-dev libsqlite3-dev libgl1-mesa-dev libdbus-1-dev. Without SQLite3 the build warns and skips ZCode, WorkBuddy and OpenCode.",
        action: LINUX_BUILD,
      },
    ],
    note: "brew trust runs once: this is a third-party tap, and Homebrew refuses to load a formula from it by short name until you say so. Without it only the fully-qualified dreamofxm/agent-ping/agent-ping works, and brew upgrade agent-ping refuses to run. Afterwards agent-ping is a launcher — the app lives in Homebrew's opt directory rather than /Applications, because Homebrew builds inside a sandbox that cannot write there.",
  },
  cta: {
    eyebrow: "Star & Feedback",
    title: "Found it useful? Leave a star",
    body: "Agent Ping is a separately maintained open-source project. If it saved you a few window switches, a star on GitHub helps: the more stars it has, the easier the next person running six agents at once finds it.",
    starBtn: "Star it on GitHub",
    issueBtn: "Open an issue",
    feedbackLead: "Feedback goes through Issues too: a tool you want added, a number whose definition is unclear, a jump that landed in the wrong place — Chinese or English, every one gets read. Before filing a bug, check Known limitations in the README: each entry there names its cause.",
    issueNote: "What it reads, what it writes and when are tabulated in the README; if you would rather verify than trust the table, run nm -u on a binary you built yourself.",
  },
  faq: {
    eyebrow: "FAQ",
    title: "Common questions",
    items: [
      {
        q: "Claude Code already has notification hooks and Remote Control — why another tool?",
        a: "Those are single-vendor: hooks only report Claude Code, and Remote Control pushes to your phone through claude.ai's side. Agent Ping watches six at once, three of which (ZCode, WorkBuddy, OpenCode) have no hook channel at all and can only be read from their own local databases. Codex's osc9 notification only fires while its terminal is focused. And the last mile is the landing: after a sound, you still have to find that session yourself.",
      },
      {
        q: "Does it edit my config files?",
        a: "Not by default. It writes only when you press the button on the Connect screen or run Tools/install_hook.sh install claude|qoder yourself, in this order: write hook.sh atomically in its own data directory (0755), back the target config up as <config>.bak-agentping-<YYYYmmdd-HHMMSS>, then merge one hooks.Notification entry per matcher and rewrite the config atomically. Codex's notify slot is detected only: if it belongs to another tool, the panel says so and leaves the file alone. The hook itself just copies the CLI's JSON payload into the inbox — it never answers a prompt, and exits 0 even when the inbox is unwritable, because the CLI reads a hook's exit code as a decision.",
      },
      {
        q: "Why is there no prebuilt binary?",
        a: "This repository ships none: the formula compiles the source at the v0.1.1 tag. The upside is that anyone can reproduce the exact app on their machine with one command; the downside is a roughly one-minute compile on first install, plus the Xcode command line tools.",
      },
      {
        q: "Are the quota numbers guesses?",
        a: "Neither guesses nor lookups: they are fields the tools wrote into their own local logs — Codex, for example, writes rate_limits events while it runs. So an idle tool's reading goes stale, and past 10 minutes it leaves the first screen for the Usage view with its updated HH:MM stamp. A tool without that field gets an undrawn cell, not a zero.",
      },
      {
        q: "Will it claim rewards for me?",
        a: "No. The Rewards screen is read-only; clicking a card only brings that vendor's app to the front, and the card states that claiming happens inside that app. ZCode's current grant is written by its own app to `~/.zcode/v2/logs/<date>.log` (the `billing/balance` response), and that is what the panel reads — but a period still waiting to be claimed never appears in that log, so the panel can't announce it in advance; at that point the card only says \"no live period on this machine\". The face value is the total granted for the period rather than what remains, and no local log records the claim action itself.",
      },
      {
        q: "Which tools does it support, and how far does Linux go?",
        a: "Six: Qoder CLI, Claude Code, Codex, ZCode, WorkBuddy and OpenCode — the last three need a build with SQLite3. Linux runs on X11: banners via notify-send, sound picked from paplay / pw-play / ffplay / canberra-gtk-play / aplay / cxplay by what is installed, and a real StatusNotifierItem tray entry when libdbus-1-dev is present. Two things to say plainly: the Linux side was verified inside a Debian 12 container and never on a real KDE/GNOME desktop, and on Linux a banner click does not jump to the session — there is no palette driver.",
      },
      {
        q: "How do I check that it really makes no network calls?",
        a: "Run nm -u on a binary you built: there is no undefined connect, sendto, getaddrinfo or socket, and no HTTP client. The only *connect* symbols in the Linux build are dbus_connection_*, which is local IPC. It also does not read the Keychain, environment secrets or shell history.",
      },
      {
        q: "Do my session titles get uploaded anywhere?",
        a: "There is nowhere to upload them: no network path exists. But titles are shown on screen and an alert quotes the title verbatim, so treat this panel the way you treat your own terminal scrollback.",
      },
    ],
  },
  footer: {
    desc: "A free, open-source status panel for AI coding agents. Six tools on one screen, double-click back to the session; quota and rewards read off your own disk, zero network calls.",
    meta: "Apache-2.0 · macOS 13+ · Linux (X11) · 6 UI languages",
    getLabel: "Get it",
    links: [
      { label: "GitHub Release (latest)" },
      { label: "Source / Issues / PRs" },
      { label: "What it reads (Privacy)" },
      { label: "English README" },
    ],
    copyright:
      "© 2026 DreamOfXM · Agent Ping is an independent open-source project with no affiliation to Qoder, Claude Code (Anthropic), Codex (OpenAI), ZCode, WorkBuddy or OpenCode; product names and marks belong to their owners.",
    langLabel: "Language",
  },
};

export const CONTENT: Record<Locale, SiteContent> = { zh, en };
