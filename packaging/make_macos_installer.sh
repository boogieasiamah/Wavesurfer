#!/usr/bin/env bash
# Builds WaveSurfer-<version>-macOS.pkg and .zip from a Release build.
# Usage: packaging/make_macos_installer.sh <artefacts dir> <version> <out dir>
set -euo pipefail
ART="$1"; VER="$2"; OUT="$3"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"   # absolute: the zip step runs from another directory
STAGE="$(mktemp -d)"

mkdir -p "$STAGE/root/Library/Audio/Plug-Ins/VST3" \
         "$STAGE/root/Library/Audio/Plug-Ins/Components" \
         "$STAGE/root/Applications"
cp -R "$ART/VST3/WaveSurfer.vst3"      "$STAGE/root/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/WaveSurfer.component"   "$STAGE/root/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/WaveSurfer.app" "$STAGE/root/Applications/"

# Installer must never "relocate" a bundle into some other copy it finds on disk
pkgbuild --analyze --root "$STAGE/root" "$STAGE/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$STAGE/components.plist" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$STAGE/components.plist" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$STAGE/components.plist"
  i=$((i + 1))
done
echo "Marked $i bundle(s) non-relocatable"

pkgbuild --root "$STAGE/root" \
         --component-plist "$STAGE/components.plist" \
         --identifier com.quantumresearchaudio.wavesurfer \
         --version "$VER" \
         --install-location / \
         "$OUT/WaveSurfer-$VER-macOS.pkg"

# Manual-install zip
mkdir -p "$STAGE/zip/WaveSurfer-$VER"
cp -R "$ART/VST3/WaveSurfer.vst3" "$ART/AU/WaveSurfer.component" "$ART/Standalone/WaveSurfer.app" "$STAGE/zip/WaveSurfer-$VER/"
cat > "$STAGE/zip/WaveSurfer-$VER/INSTALL.txt" <<'EOF'
WaveSurfer — manual install (macOS)

  WaveSurfer.vst3       ->  /Library/Audio/Plug-Ins/VST3/
  WaveSurfer.component  ->  /Library/Audio/Plug-Ins/Components/
  WaveSurfer.app        ->  /Applications/  (standalone)

The build is not notarised. If macOS blocks it, run once in Terminal:
  xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/WaveSurfer.vst3 \
      /Library/Audio/Plug-Ins/Components/WaveSurfer.component /Applications/WaveSurfer.app
Then rescan plug-ins in your DAW (Logic: restart; AU cache refreshes automatically).
EOF
(cd "$STAGE/zip" && ditto -c -k --keepParent "WaveSurfer-$VER" "$OUT/WaveSurfer-$VER-macOS.zip")
rm -rf "$STAGE"
ls -la "$OUT"
