# Homebrew cask for Fangs (macOS).
#
# Distributes the prebuilt `Fangs.app` produced by
# `scripts/macos-bundle.sh` and attached to a GitHub release. Building from
# source via Homebrew is impractical here: the VT engine needs Zig 0.15.2 plus
# the macOS-26 SDK linker workaround (see scripts/macos-build.sh), which doesn't
# fit Homebrew's sandbox — so we ship the bundle instead.
#
# MAINTAINER, per release: the release workflow (.github/workflows/release.yml)
# builds Fangs.app and uploads fangs-<version>-macos-arm64.zip automatically on
# tag push. Then bump `version` below and set `sha256` to that zip's hash
# (printed by the workflow's "Bundle .app" step, or `shasum -a 256` of the asset).
#
cask "fangs" do
  version "0.1.1"
  sha256 "3033a791d100f6b9c828094542af395b2992a1676652f6f9a79d35e5c9a83660"

  url "https://github.com/rene-rodriguez/fangs/releases/download/v#{version}/fangs-#{version}-macos-arm64.zip"
  name "Fangs"
  desc "Native BYOK terminal with an AI sidebar and inline command generation"
  homepage "https://github.com/rene-rodriguez/fangs"

  depends_on macos: ">= :monterey"
  depends_on arch: :arm64

  app "Fangs.app"

  caveats <<~EOS
    For OSC-133 command blocks, add the shell snippet from docs/shell-integration.md.
    Set FANGS_API_KEY in your environment to enable the AI features.
  EOS

  zap trash: [
    "~/.config/fangs",
  ]
end
