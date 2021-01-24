#!/bin/sh
set -eu
renderer=$1
output=$2
backend=$3
spp=$4
mkdir -p "$output"
for scene in afterglow veil prism meridian lantern shutter; do
    prefix="$output/$scene"
    "$renderer" render "scenes/$scene.vs" --backend "$backend" --width 256 --height 256 \
        --spp "$spp" --bins 512 --step-ns .125 --prefix "$prefix" --checkpoint "$prefix.vfilm" \
        >"$prefix.json" 2>"$prefix.log"
done
