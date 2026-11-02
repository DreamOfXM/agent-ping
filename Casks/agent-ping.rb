cask "agent-ping" do
  version "0.1.5"

  # 两个架构各一份预编译产物（已用 Developer ID 签名、公证并 staple），
  # 由 release 工作流产在同一批附件里。
  #
  # 这里刻意用带版本号的 URL，而不是 releases/latest/download 那种固定路径：
  # Homebrew 的下载缓存是以 URL 为键的，URL 不变就永远命中第一次下载的那份包，
  # 于是新版本发布后 brew install 装到的还是旧版（而且 sha256 根本没机会起作用）。
  # 版本化之后缓存随 URL 一起变，sha256 也能真正校验。
  on_arm do
    sha256 "c23b0d1309b26a1a1bb210e1d47a55d89308dc669b1dd655192abbdcee752064"
    url "https://github.com/DreamOfXM/agent-ping/releases/download/v#{version}/Agent-Ping-macos-arm64.zip"
  end
  on_intel do
    sha256 "a8c7c366dd40c5cf438ecdc7bc06ea72f523ef2bd6a9e3c91ef84d030efbb711"
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
