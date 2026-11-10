cask "agent-ping" do
  version "0.1.11"

  # URL 刻意带版本号，而不是 releases/latest/download 那种固定路径：Homebrew 的下载缓存以
  # URL 为键，URL 不变就永远命中第一次下的那份包，新版本发布后装到的还是旧版（sha256 根本没机会起作用）。
  #
  # 两个架构各一份预编译产物，URL 指向的是钉过票据的那一份：release 工作流先挂上 runner
  # ad-hoc 签的 zip（那种装机会被 Gatekeeper 拒），本地用 Developer ID 重签 → 送审 →
  # staple → 替换同名附件，所以下面这两个 sha256 必须是**钉票之后**那一份的哈希。
  # staple 会改写字节：钉票前后的两个哈希别混用，抄错了 brew 100% 校验失败。
  on_arm do
    sha256 "75bc218bcae8a6fa642e12898558899a23dfa8168c921e9b61ce268e3965bcdf"
    url "https://github.com/DreamOfXM/agent-ping/releases/download/v#{version}/Agent-Ping-macos-arm64.zip"
  end
  on_intel do
    sha256 "b8aa3d4fd1e6c28df021017902f9a84dfc0f172de3e342ee82745a3133aef971"
    url "https://github.com/DreamOfXM/agent-ping/releases/download/v#{version}/Agent-Ping-macos-x86_64.zip"
  end

  name "Agent Ping"
  desc "Menu-bar panel showing which AI coding agent is running, waiting, or stuck"
  homepage "https://github.com/DreamOfXM/agent-ping"

  depends_on macos: :ventura

  app "Agent Ping.app"

  caveats <<~EOS
    Agent Ping has no Dock icon. It appears at the right end of the menu bar —
    the oval icon with a star, next to the clock.

    Upgrade it with:
      brew upgrade --cask agent-ping

    Turn on "Launch at login" in its settings if you want it to start with the
    session.

    If you previously installed it with the formula (brew install agent-ping),
    remove that first — otherwise two copies both put an icon in the menu bar:
      brew uninstall agent-ping
  EOS
end
