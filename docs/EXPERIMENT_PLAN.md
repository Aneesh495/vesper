# Experiment plan v1

The diagnostic profile is bounded to one local checkout and one Apple M2. An accepted diagnostic has a different status from full acceptance. CPU arithmetic is float64, GPU transport is float32, and the temporal film stores float32 sums. All estimates use fixed sample counts with the production wavelength density. No clamping or denoising enters the estimators.

| Question | Unit and frozen diagnostic rule |
|---|---|
| Can traversal retain edge hits and moving primitives? | One million rays with seed 10b67a2b9cf101d5 against a plane-edge and quadratic oracle. Require no hit mismatches and distance error at most 2e-10 times max(1, distance). |
| Do the two wavelength densities integrate the same spectra? | Three seeds, 65,536 samples per density. Compare equal-energy XYZ to observer quadrature and require a Euclidean error below 2.0. |
| Is the shifted Halton sequence reasonably distributed? | 4,096 two-dimensional samples. Each 8 by 8 cell must have 59 to 69 samples. |
| Does GGX sampling retain the evaluated mass? | Midpoint solid-angle quadrature and 65,536 Halton samples. Compare accepted mass and energy to 0.013 absolute, with explicit loss of rejected proposals. |
| Does a linear density grid preserve attenuation? | Three seeds, 65,536 flights and ratio estimates each. Require absolute errors below 0.008 and 0.007 respectively against a scalar integral. |
| Do visibility and light PDFs reconcile? | A Lambertian patch under a square parallel emitter. Midpoint area quadrature is the independent oracle. Compare BSDF-only and MIS at 4,096 and 131,072 samples with seeds 11, 19 and 47. Require errors within six standard errors plus 0.0001 at the large budget. |
| Does arrival time include all segments? | Direct paths, a diffuse point-source connection and a dielectric chord at near-normal incidence. Compare path delay to metre geometry with an allowance of half a 0.125 ns bin. Test a convolved pulse and a clipped gate separately. |
| Do Vulkan estimates preserve the CPU statistical result? | Shared seeds 11, 19 and 47 for 24 by 18 images at 16 and 128 spp in Afterglow, Veil and Meridian. Require the image mean Y difference below 1 percent or 0.005 absolute at 128 spp. Record both smaller-budget differences and ray counts. |
| Does million-triangle compute meet the throughput threshold? | An authored 1,000 by 500 rectangular cell grid split into one million triangles. Primary paths and shadow paths are counted separately. Full threshold is 5 million useful segments/s with synchronized end-to-end elapsed time. |
| Do independent pilot budgets preserve primary samples? | A 24 by 18 Afterglow crop at seed 19, with 16 pilot samples per pixel, 128 maximum primary samples and a relative target of 0.45. Check both backends, three fixed-count pixel replays and byte-identical adaptive resume. Count pilot work in the savings comparison. |
| Do imported geometry and textures preserve transport? | Compare glTF and GLB against literal mirrored triangles. Compare PPM texture factors against independent linear and sRGB calculations, with a relative tolerance of 3e-6. |
| Can the inverse solver detect ambiguity and mismatch? | Twelve calibrated known-truth cases with seeds 11, 19 and 47. Require 5 percent relative error. Four cases exercise a scale ambiguity, collinear basis, zero-length extinction and a misspecified observation. |

## Profiles

The full profile adds six 1920 by 1080 renders at 1,024 samples per pixel, six 256 by 256 transient renders with 512 bins, hardware captures and the remaining feature and semantic gates. Full renders have no exclusion of unsuccessful seeds or unsupported features. Require an explicit workload and device budget before running the full campaign. The local diagnostic budget is 10 minutes per command and 640 MiB per film and per Vulkan allocation arena. The raw findings retain timeout, driver failure and insufficient throughput as failures.
