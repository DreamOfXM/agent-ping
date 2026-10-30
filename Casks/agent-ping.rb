cask "agent-ping" do
  # 稳定文件名 + :latest：每次发版工作流都会把同一路径的 zip 覆盖掉，
  # 所以这里不用跟着 tag 改 url / sha256。
  version :latest
  sha256 :no_check

  url "https://github.com/DreamOfXM/agent-ping/releases/latest/download/Agent-Ping-macos-arm64.zip"
  name "Agent Ping"
  desc "Menu-bar panel showing which AI coding agent is running, waiting, or stuck"
  homepage "https://github.com/DreamOfXM/agent-ping"

  # 预编译产物是 Apple 芯片的：Intel 机器装不了这份，走 formula 从源码编译
  # （brew install agent-ping，就是我们自己那台 macOS 26 的路径）。
  depends_on arch: :arm64
  depends_on macos: :ventura

  app "Agent Ping.app"

  caveats <<~EOS
    Agent Ping has no Dock icon. It appears at the right end of the menu bar —
    the oval icon with a star, next to the clock.

    Turn on "Launch at login" in its settings if you want it to start with the
    session.
  EOS
end
