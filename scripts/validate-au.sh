#!/bin/bash
# Local/CI development validation only; distribution signing is separate.
set -euo pipefail
preset=${1:?Usage: bash scripts/validate-au.sh PRESET}
configuration=Release
if [[ "$preset" == *debug ]]; then configuration=Debug; fi
product='Wicky Chordinator'
type=aumf
subtype=Jbpl
case "$preset" in
    instrument-*) product='JUCE Instrument'; type=aumu; subtype=Jbpi ;;
    midi-fx-*) product='Wicky Chordinator MIDI FX'; type=aumi; subtype=Wcmf ;;
    debug|release) ;;
    *) echo "Unknown preset: $preset" >&2; exit 1 ;;
esac
component="build/$preset/JUCEBoilerplate_artefacts/$configuration/AU/$product.component"
# The linker signature alone does not seal a component bundle's resources.
# Use an ad-hoc development signature before asking the registrar to load it.
codesign --force --sign - "$component"
codesign --verify --deep --strict "$component"
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
ditto "$component" "$HOME/Library/Audio/Plug-Ins/Components/$product.component"
killall AudioComponentRegistrar 2>/dev/null || true
# Registration starts lazily: enumerate first, then wait for this exact identity.
registered=false
for attempt in {1..10}; do
    if auval -l | grep -Eq "${type}[[:space:]]+${subtype}[[:space:]]+Wckm"; then
        registered=true
        break
    fi
    sleep 1
done
if [[ "$registered" != true ]]; then
    echo "AU registrar did not discover $type $subtype Wckm" >&2
    exit 1
fi
auval -v "$type" "$subtype" Wckm
