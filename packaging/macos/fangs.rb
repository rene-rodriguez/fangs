# Homebrew cask for Fangs (macOS).
#
# Distributes the prebuilt `Fangs.app` produced by
# `scripts/macos-bundle.sh` and attached to a GitHub release. Building from
# source via Homebrew is impractical here: the VT engine needs Zig 0.15.2 plus
# the macOS-26 SDK linker workaround (see scripts/macos-build.sh), which doesn't
# fit Homebrew's sandbox — so we ship the bundle instead.
#
# MAINTAINER, per release:
#   1. scripts/macos-bundle.sh <version>
#   2. Upload dist/fangs-<version>-macos-arm64.zip to the GitHub release.
#   3. Set `version` below and `sha256` to the value printed by the bundler
#      (shasum -a 256 of the zip).
#
# Until the app is notarized with a Developer ID, it is only ad-hoc signed, so
# Gatekeeper quarantines downloads. The caveats tell users how to clear it.
cask "fangs" do
  version "0.1.0"
  sha256 "REPLACE_WITH_RELEASE_ZIP_SHA256"

  url "https://github.com/rene-rodriguez/fangs/releases/download/v#{version}/fangs-#{version}-macos-arm64.zip"
  name "Fangs"
  desc "Native BYOK terminal with an AI sidebar and inline command generation"
  homepage "https://github.com/rene-rodriguez/fangs"

  depends_on macos: ">= :monterey"
  depends_on arch: :arm64

  app "Fangs.app"

  caveats <<~EOS
    Fangs is ad-hoc signed, not yet notarized. On first launch macOS may
    say it "cannot be opened". Either right-click the app and choose Open, or run:

      xattr -dr com.apple.quarantine "#{appdir}/Fangs.app"

    For OSC-133 command blocks, add the shell snippet from docs/shell-integration.md.
    Set FANGS_API_KEY in your environment to enable the AI features.
  EOS

  zap trash: [
    "~/.config/fangs",
  ]
end
