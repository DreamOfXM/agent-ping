cask "agent-ping" do
  # 稳定文件名 + :latest：每次发版工作流都会把同一路径的 zip 覆盖掉，
  # 所以这里不用跟着 tag 改 url / sha256。
  version :latest
  sha256 :no_check

  # 两个架构各一份预编译产物，都由 release 工作流产在同一批附件里。
  on_arm do
    url "https://github.com/DreamOfXM/agent-ping/releases/latest/download/Agent-Ping-macos-arm64.zip"
  end
  on_intel do
    url "https://github.com/DreamOfXM/agent-ping/releases/latest/download/Agent-Ping-macos-x86_64.zip"
  end

  name "Agent Ping"
  desc "Menu-bar panel showing which AI coding agent is running, waiting, or stuck"
  homepage "https://github.com/DreamOfXM/agent-ping"

  depends_on macos: :ventura

  app "Agent Ping.app"

  caveats <<~EOS
    Agent Ping has no Dock icon. It appears at the right end of the menu bar —
    the oval icon with a star, next to the clock.

    This cask tracks "latest", so upgrade it with --greedy:
      brew upgrade --cask --greedy agent-ping

    Turn on "Launch at login" in its settings if you want it to start with the
    session.

    If you previously installed it with the formula (brew install agent-ping),
    remove that first — otherwise two copies both put an icon in the menu bar:
      brew uninstall agent-ping
  EOS
end
