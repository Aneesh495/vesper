#!/bin/sh
set -eu

renderer=$1
out=$2
selected=
if [ "$#" -ge 3 ]; then selected=$3; fi
scenes="afterglow veil prism iris shutter array"
if [ -n "$selected" ]; then
    case " $scenes " in
        *" $selected "*) scenes=$selected ;;
        *) printf '%s\n' "Unknown full showcase scene: $selected" >&2; exit 2 ;;
    esac
fi
mkdir -p "$out"
python3 scripts/showcase_evidence.py "$renderer" "$out"

for scene in $scenes; do
    prefix="$out/$scene"
    checkpoint="$prefix.vfilm"
    set -- render "scenes/$scene.vs" --backend vulkan --width 1920 --height 1080 --spp 1024 \
        --sampler stratified --seed 1789 --depth 8 --prefix "$prefix" --checkpoint "$checkpoint" \
        --checkpoint-rows 128 --progress
    if [ -f "$checkpoint" ]; then set -- "$@" --resume "$checkpoint"; fi
    attempt="$(mktemp "$prefix.attempt.XXXXXX")"
    printf '%s\n' "Rendering $scene at 1920x1080, 1024 spp. Progress: $attempt.log"
    "$renderer" "$@" >"$attempt" 2>"$attempt.log"
    python3 scripts/showcase_evidence.py "$renderer" "$out" --scene "$scene" --attempt "$attempt"
done
