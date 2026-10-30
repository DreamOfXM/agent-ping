class AgentPing < Formula
  desc "Menu-bar panel showing which AI coding agent is running, waiting, or stuck"
  homepage "https://github.com/DreamOfXM/agent-ping"
  url "https://github.com/DreamOfXM/agent-ping/archive/refs/tags/v0.1.0.tar.gz"
  version "0.1"
  sha256 "8bd824b0af46da25c0f5be6bc4619203f8173e0c96af98642b8d9a0a53e4080b"
  license "Apache-2.0"

  depends_on macos: :ventura
  depends_on "cmake" => :build
  depends_on "freetype"
  depends_on "glfw"

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
           "-DCMAKE_PREFIX_PATH=#{Formula["glfw"].opt_prefix}:#{Formula["freetype"].opt_prefix}",
           *std_cmake_args
    system "cmake", "--build", "build", "--target", "pin_imgui_shell", "--parallel", ENV.make_jobs.to_s

    app = buildpath/"Agent Ping.app"
    (app/"Contents/MacOS").mkpath
    (app/"Contents/Resources/fonts").mkpath
    cp "build/pin_imgui_shell", app/"Contents/MacOS/AgentPing"
    cp "assets/Info.plist", app/"Contents/Info.plist"
    cp "assets/AppIcon.icns", app/"Contents/Resources/AppIcon.icns"
    cp Dir["assets/menubar-icon*.png"], app/"Contents/Resources/"
    cp Dir["fonts/*.ttf"], app/"Contents/Resources/fonts/"
    cp Dir["fonts/LICENSE-*"], app/"Contents/Resources/fonts/"
    cp "LICENSE", app/"Contents/Resources/LICENSE"
    (app/"Contents/PkgInfo").write "APPL????"
    system "codesign", "--force", "--sign", "-", app
    prefix.install app

    launcher = buildpath/"agent-ping"
    launcher.write <<~SH
      #!/bin/bash
      exec open "#{opt_prefix}/Agent Ping.app"
    SH
    launcher.chmod 0755
    bin.install launcher
  end

  def caveats
    <<~EOS
      Agent Ping is a menu-bar app. Start it with:
        agent-ping

      It lives in Homebrew's opt directory (#{opt_prefix}/Agent Ping.app) rather
      than /Applications, because Homebrew builds inside a sandbox that cannot
      write there. Turn on "Launch at login" in the app's settings to start it
      with the session.

      Uninstall with:
        brew uninstall agent-ping
    EOS
  end

  def test
    assert_predicate prefix/"Agent Ping.app/Contents/MacOS/AgentPing", :executable?
    assert_predicate prefix/"Agent Ping.app/Contents/Resources/fonts/NotoSansSC.ttf", :exist?
    assert_match "opt/agent-ping/Agent Ping.app", (bin/"agent-ping").read
  end
end
