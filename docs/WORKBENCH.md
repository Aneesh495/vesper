# Workbench

Run the native workbench with a scene and a CPU or Vulkan backend. The render region is outlined in teal. The left plot shows picked-pixel spectral contributions by band. The right plot shows picked-pixel time-of-flight energy. The window title shows the active backend, picked pixel, sample count, Y variance, exposure and arrival-bin location.

| Input | Effect |
|---|---|
| Space | Pause or resume |
| Left click | Pick a pixel and its primary material |
| Right drag | Set a render region |
| R | Restore the full render region |
| Left and right arrows | Orbit the target |
| W, A, S, D, Q, E | Translate the camera |
| T, brackets, or click on the time plot | Toggle and scrub the transient view |
| Tab, L | Toggle the spectrum of a selected material or light and cycle lights |
| 1 to 4 | Adjust selected material reflectance or explicit light intensity |
| Minus or equals | Adjust exposure |
| P | Print a CPU path replay with primitive and event identifiers |
| X | Export raw EXR, VSPFILM and the selected pixel's temporal CSV |
| Escape | Close |

Camera and material changes rebuild and revalidate scene state before reopening a film. Exposure, region and time-bin changes preserve existing samples. The workbench intentionally bounds each progressive update to a small tile.
