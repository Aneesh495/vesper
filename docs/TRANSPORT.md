# Transport notes

## 1. Radiometry

The engine estimates spectral radiance density over 360 to 830 nm, expressed per nanometre. Geometric lengths are metres. Surface emission is the outgoing radiance density on the geometric front face. Point lights specify intensity density, which becomes incident normal irradiance through inverse-square falloff. Distant lights specify incident normal irradiance directly.

For a nonemitting surface the reflected or transmitted term integrates the BSDF times incident radiance times the absolute incident cosine over solid angle. The volume equation adds attenuation and an integral over the phase function. At a segment endpoint, a contribution is divided by the probability law that sampled that endpoint and direction.

## 2. Spectral estimator

A single wavelength is selected per path. The film uses a sample of L(lambda) times the observer vector divided by p(lambda). The estimator targets the wavelength integral, so both a uniform proposal and a uniform-tent mixture produce the same expectation. The mixture improves allocation near the Y sensitivity peak while preserving support for red and blue paths.

Tabulated material values are linearly interpolated between eight evenly spaced knots. Gaussian spectra express finite-width lines. RGB reconstruction is a convex weighted mixture of input RGB based on three smooth wavelength lobes. It yields bounded plausible reflectance and has no measurement interpretation. The observer table is separate measured standard data. [Dataset provenance](../data/README.md).

## 3. Material models

Diffuse evaluation is reflectance divided by pi. A cosine-weighted proposal cancels the cosine and constant from the throughput update. The exact dielectric Fresnel equations set reflection and transmission probabilities. Discrete transmission multiplies throughput by the squared incident-to-transmitted phase-index ratio for radiance transport. Conductor reflection uses the complex refractive index at the sampled wavelength.

## 4. Microfacet PDFs

GGX uses alpha equal to squared perceptual roughness. Its isotropic projected normal density is D(h) times the microfacet cosine. The reflection mapping divides that normal density by four times the outgoing half-vector cosine. For refraction define eta as transmitted over incident phase index. The transmission Jacobian is eta squared times the absolute incident half-vector cosine divided by the squared generalized half-vector denominator. Fresnel branch probability enters both PDFs.

The proposal samples the full projected normal distribution. Invalid facets and directions terminate with zero contribution, which leaves the accepted directional density subnormalized. This avoids resampling bias while losing efficiency at grazing incidence. The single-scattering microfacet model also loses energy to unmodeled multiple reflections. Reciprocal rough transmission is checked with the expected squared index factor.

## 5. Multiple importance sampling

The direct-light estimator draws one light uniformly. A sampled emitter point has density 1 over area. Multiply by squared distance divided by the emitter cosine to compare in solid angle. The constant environment uses a uniform sphere proposal. The optional directional environment adds a uniform bright cap to the base. The proposal selects the cap or the entire sphere in proportion to each component's radiance times solid angle. Its combined PDF is the directional radiance divided by the total sphere integral, including both components inside the cap. The escape and light-sampling strategies evaluate that same PDF in solid angle. Point and distant sources are discrete choices with unit MIS weight.

The power heuristic compares densities only for strategies with a continuous density in the same measure. Emissive hits and environment escapes use the complementary weight when the previous event had a continuous PDF. Null boundaries preserve that previous event. The shadow ray points from its numerically offset origin to the sampled finite endpoint, while the transport evaluation uses the physical vertex-to-light direction.

## 6. Robust geometry

Triangles project into the dominant ray plane. Shared edges use matching projected endpoints with antisymmetric edge functions. Bounds include both shutter endpoints. Sphere intersection uses a projected miss distance and a stable quadratic root. A hit point is projected onto the corresponding surface before offsetting the spawned ray. The offset uses position and primitive extent. The GPU pads uploaded bounds for float32 rounding. Very thin or nearly coincident features approach the precision limit.

Diffuse normal maps use a tangent frame derived from the surface UVs. The RGB perturbation is converted to a slope, with a blue denominator floor and a 30-degree slope cap. The BSDF multiplies Lambertian response by the product of two direction factors, one for each of the incoming and outgoing directions. For either direction the factor is the ratio of shading cosine to geometric cosine, clipped to [0,1]. Both factors use the geometric hemisphere. This symmetric response is reciprocal, stays below Lambertian pointwise and can lose diffuse energy under tilt. Both continuation and direct-light evaluation apply the same factor. Sampling densities, sidedness, boundary tests and ray offsets use geometric normals. An analytic fixture checks forward and backward illumination, UV mirroring and the sampled hemispherical integral.

## 7. BVH design

The builder bins centroids on each axis and minimizes the sum of child surface area times child primitive count plus parent traversal cost. Bounded leaf sizes and midpoint fallback handle coincident centroids. A validation traversal proves reachable nodes, primitive uniqueness and bounds enclosure. CPU and GPU traversal visit the near child first and reduce the ray interval on each hit.

## 8. Wavefront scheduling

Vulkan executes generate, intersect, shade and shadow stages. Queue arrays hold indices into stable path slots. Atomic append compacts active paths into the next queue. The fixed per-batch capacity bounds all queues since each stage emits at most one continuation and one shadow job per active path. An explicit counter makes an overflow a failed batch. Compute barriers publish state to consumers and a fence makes completed events available to the host.

## 9. Majorants

Trilinear interpolation is a convex combination of the eight grid nodes around a point. The largest node density therefore bounds the whole grid. Multiplying by the sum of spectral absorption and scattering upper bounds gives a conservative scalar majorant. Float32 packing expands the bound to cover rounding. Traversal aborts a batch if an evaluated extinction exceeds the bound.

## 10. Transmittance

A homogeneous segment attenuates by exp of negative extinction times distance. Heterogeneous free flight samples the majorant exponential process and accepts each candidate in proportion to local extinction. A real collision contributes the scattering-to-extinction fraction. Shadow rays use a product of one minus the candidate extinction ratios. Tracking stops at the segment endpoint or reports an exhausted proposal budget. [Volume derivation reference](https://pbr-book.org/4ed/Volume_Scattering/Transmittance).

## 11. Variance and bias

The steady film uses Welford mean and M2 per pixel, including zero-contribution paths. Fixed independent sampling supports standard errors. Low-discrepancy and stratified modes use randomized streams and require multiple independent seeds for uncertainty claims. Russian roulette rescales surviving paths and uses a separate index balancing term to avoid penalizing entry into glass. Finite depth truncates the path integral, and the acceptance controls hold depth fixed across strategies.

The opt-in adaptive mode draws 16 CPU pilot paths per pixel with a separate seed. It estimates mean Y and sample variance from those paths. The primary target is the pilot variance divided by the square of the requested relative error times max(mean Y, 1), rounded up and clamped between 16 and the requested cap. The pilot controls allocation and contributes no film energy. Primary sample indices start at zero and use the original seed, so the conditional expectation of their mean is independent of the random budget under the independent-stream model. This estimates precision from a small pilot and can underallocate rare high-contribution paths. The reported pilot cost includes its CPU ray segments, even when the primary film runs on Vulkan. Resume recomputes deterministic targets and retains existing sample indices.

## 12. Camera and film

The raster position is uniformly jittered within its pixel. Thin-lens samples are uniform on the aperture disk and map to the focal plane. The camera estimator averages samples from the configured image, aperture and exposure distributions. It has no physical lens vignetting or sensor irradiance weighting. Shutter time interpolates geometry and stays constant across a complete path.

## 13. Color and display

Observer-weighted XYZ and sample variance remain in the EXR and checkpoint. The display uses a linear XYZ-to-sRGB conversion, an exposure multiplier, a nonnegative gamut clamp, and a compressive curve followed by the sRGB transfer function. Changing the display gate or exposure leaves accumulated data intact.

## 14. Performance

The benchmark includes host scheduling, submission, GPU completion, event readback and film updates in its timed section. The BVH build and device setup are reported separately. The useful count includes primary and subsequent path segments plus explicit shadow segments. Warm-up samples are cleared before timing. Large scene and density allocations fail at configured limits. The comparison reports CPU and GPU rates independently.

## 15. Time of flight

The estimator accumulates segment length multiplied by the group refractive index, starting at the sensor. Under the Cauchy model, the phase index is A plus B over wavelength squared in micrometres. The group index is A plus three B over wavelength squared. The final camera segment is part of the optical length. No delay is assigned to infinite emitters. Geometry moves with shutter time and stays fixed while that path accumulates time of flight. [Transient transport reference](https://cs.dartmouth.edu/~wjarosz/publications/jarabo14framework.html).

## 16. Pulse and sensor response

A zero-width pulse is a delta. A nonzero pulse and instrument response are separately normalized causal boxes. Convolution gives a trapezoid, triangle or box depending on their widths. Integrating the resulting CDF across each half-open bin preserves temporal energy within the gate. Excluded energy tracks finite-pulse truncation and untimed light. Spectral weight and pulse mass are folded after tracing, without drawing another random dimension.
