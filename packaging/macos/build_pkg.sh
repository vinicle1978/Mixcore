#!/usr/bin/env bash
# Build a macOS installer (.pkg) for MixCore from a finished Release build.
#
#   packaging/macos/build_pkg.sh <artefacts-dir> <version> <output-dir>
#
#   <artefacts-dir>  e.g. build/plugins/mixcore/MixCore_artefacts/Release
#
# Installs:
#   MixCore.component -> /Library/Audio/Plug-Ins/Components   (Logic Pro, GarageBand, AU hosts)
#   MixCore.vst3      -> /Library/Audio/Plug-Ins/VST3         (Ableton, Reaper, Cubase, ...)
#   MixCore.app       -> /Applications                        (standalone)
#
# Signing: bundles are ad-hoc signed (required for Apple Silicon to load them).
# The .pkg itself is NOT Developer-ID signed or notarised, so macOS will ask
# you to approve it once (see docs/user-guide/installing-on-mac.md).
# When you have a Developer ID, set MIXCORE_APP_SIGN_ID / MIXCORE_PKG_SIGN_ID.
set -euo pipefail

ARTEFACTS="${1:?artefacts dir}"
VERSION="${2:?version}"
OUT="${3:?output dir}"
APP_SIGN_ID="${MIXCORE_APP_SIGN_ID:--}"   # "-" = ad-hoc
PKG_SIGN_ID="${MIXCORE_PKG_SIGN_ID:-}"
ID_BASE="com.mixcoreaudio.mixcore"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

declare -a PARTS=(
  "AU|MixCore.component|/Library/Audio/Plug-Ins/Components|au"
  "VST3|MixCore.vst3|/Library/Audio/Plug-Ins/VST3|vst3"
  "Standalone|MixCore.app|/Applications|app"
)

PKG_ARGS=()
for part in "${PARTS[@]}"; do
  IFS='|' read -r dir bundle dest suffix <<<"$part"
  src="$ARTEFACTS/$dir/$bundle"
  if [[ ! -d "$src" ]]; then
    echo "error: missing $src" >&2
    exit 1
  fi

  stage="$WORK/stage-$suffix"
  mkdir -p "$stage"
  ditto "$src" "$stage/$bundle"

  # Sign the bundle (ad-hoc unless a Developer ID is provided).
  # Hardened runtime only with a real Developer ID (needed for notarisation;
  # the standalone app would then also need an audio-input entitlement).
  if [[ "$APP_SIGN_ID" == "-" ]]; then
    codesign --force --deep --timestamp=none --sign - "$stage/$bundle"
  else
    codesign --force --deep --options runtime --timestamp --sign "$APP_SIGN_ID" "$stage/$bundle"
  fi
  codesign --verify --strict --verbose=2 "$stage/$bundle"
  lipo -info "$stage/$bundle/Contents/MacOS/"* || true

  # Component property list written explicitly (not via `pkgbuild --analyze`,
  # whose output keys vary between macOS versions). Key point: the bundle is
  # NOT relocatable, so Installer never "moves" the install to an older copy
  # found elsewhere on the disk.
  plist="$WORK/$suffix.plist"
  cat >"$plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
  <dict>
    <key>BundleHasStrictIdentifier</key><true/>
    <key>BundleIsRelocatable</key><false/>
    <key>BundleIsVersionChecked</key><false/>
    <key>BundleOverwriteAction</key><string>upgrade</string>
    <key>RootRelativeBundlePath</key><string>$bundle</string>
  </dict>
</array>
</plist>
PLIST
  plutil -lint "$plist"

  scripts_args=()
  if [[ "$suffix" == "au" ]]; then
    # Make the AU system notice the new component without a reboot.
    mkdir -p "$WORK/scripts-au"
    cat >"$WORK/scripts-au/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
    chmod +x "$WORK/scripts-au/postinstall"
    scripts_args=(--scripts "$WORK/scripts-au")
  fi

  pkgbuild --root "$stage" \
           --component-plist "$plist" \
           --install-location "$dest" \
           --identifier "$ID_BASE.$suffix" \
           --version "$VERSION" \
           ${scripts_args[@]+"${scripts_args[@]}"} \
           "$WORK/MixCore-$suffix.pkg"
  PKG_ARGS+=(--package "$WORK/MixCore-$suffix.pkg")
done

# Combine into one installer with a "Customize" option per component.
productbuild --synthesize "${PKG_ARGS[@]}" "$WORK/distribution.xml"
# Title shown in the installer window.
sed -i '' 's#<installer-gui-script minSpecVersion="1">#<installer-gui-script minSpecVersion="1"><title>MixCore '"$VERSION"'</title>#' "$WORK/distribution.xml"

FINAL="$OUT/MixCore-$VERSION-macOS.pkg"
sign_args=()
if [[ -n "$PKG_SIGN_ID" ]]; then
  sign_args=(--sign "$PKG_SIGN_ID")
fi
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" ${sign_args[@]+"${sign_args[@]}"} "$FINAL"

echo "Built $FINAL"
