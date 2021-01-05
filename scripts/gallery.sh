#!/bin/sh
set -eu
renderer=$1
out=$2
backend=$3
width=$4
height=$5
spp=$6
mkdir -p "$out"
for scene in afterglow veil prism meridian iris shutter lantern furnace metalwork frost needle array; do
    "$renderer" render "scenes/$scene.vs" --backend "$backend" --width "$width" --height "$height" \
        --spp "$spp" --prefix "$out/$scene" >"$out/$scene.json" 2>"$out/$scene.log"
done
