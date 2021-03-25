# Scene format v1

The first directive is **vesper 1 metres**. Whitespace separates tokens and a hash starts a comment. Declare assets before referencing them. The default medium is vacuum. Geometry uses metres, pulse and response widths use nanoseconds and shutter endpoints use normalized exposure coordinates.

| Directive | Arguments |
|---|---|
| camera | name, eye XYZ, target XYZ, up XYZ, vertical FOV in degrees, aperture radius, focus plane distance, shutter open and close |
| use | camera name |
| diffuse | name, model RGB in [0,1] |
| conductor | name, perceptual roughness, eta, k |
| glass | name, interior medium, Cauchy A and B |
| rough_glass | name, interior medium, Cauchy A and B, roughness |
| boundary | name, interior medium |
| reflectance / eta / k | material name, eight equally spaced knots from 360 to 830 nm |
| emission | material name, constant spectral radiance density, pulse start and width |
| emission_line | material name, peak spectral radiance density, center and sigma in nm, pulse start and width |
| sphere | material, center XYZ, radius, shutter displacement XYZ, boundary ID |
| triangle | material, three XYZ vertices, shutter displacement XYZ, boundary ID |
| point | position XYZ, spectral intensity density, pulse start and width |
| point_line | position XYZ, peak spectral intensity density, center and sigma in nm, pulse start and width |
| distant | direction towards source XYZ, spectral irradiance density at normal incidence |
| environment | constant spectral radiance density |
| medium | name, absorption and scattering in inverse metres, Henyey-Greenstein g, extinction majorant |
| world | medium name |
| response | box instrument-response width |

Use a unique positive boundary ID for each closed transmitting solid and orient its faces outwards. Its faces share an ID and material. Opaque surfaces use boundary zero. Emission follows the front geometric normal.

## Relative assets

- **obj** takes material name, relative OBJ path, positive scale XYZ, translation XYZ, shutter displacement XYZ and boundary ID. The subset accepts position vertices, UVs and triangular faces with positive or negative indices. OBJ v-up texture coordinates are converted to the image's top-down convention. Referenced vertex normals are validated and transport uses the geometric normal. Reusing the same asset with another transform compiles another set of world-space primitives. Polygonal faces are outside this subset.
- **grid** takes medium name, relative density path, absorption, scattering, anisotropy, majorant, integer grid dimensions XYZ and the lower and upper bounds XYZ. The density file contains whitespace-separated values in x-fastest order. The grid uses trilinear interpolation with zero density outside its world-space bounds. Its majorant bounds the product of maximum node density and the spectral extinction bound.


- **gltf** takes the same material, path, transform and boundary arguments as **obj**. The static glTF 2.0 subset loads the default scene, or the first scene if none is selected, and expands node hierarchies into world-space triangles. It accepts indexed and nonindexed triangle lists in GLB or glTF with plain relative buffer files. The importer preserves UV0, validates vertex normals, uses geometric normals for transport, and applies the Vesper material specified by the directive. Mirrored nodes preserve face orientation. Extensions, skins, animation, sparse accessors and other primitive types are rejected. Transmitting imports must describe a closed boundary.


- **texture** takes diffuse material name, relative P3 or 8-bit P6 PPM path, color space **srgb** or **linear**, then scale U, scale V, offset U and offset V. Vesper decodes the declared transfer function, converts texels to its bounded RGB-to-spectrum model and bilinearly filters them with repeat addressing. The filtered spectrum multiplies the diffuse base reflectance. The image origin is at the top left. Triangle UVs default to barycentric coordinates and spheres use longitude and colatitude. UV and texture changes affect the checkpoint scene identity. This path covers diffuse albedo. glTF material and image import remain open.

- **normal_map** takes diffuse material name, relative P3 or 8-bit P6 PPM path, then scale U, scale V, offset U, offset V and strength in [0,1]. The map stores linear tangent-space RGB. Red follows U, green follows V and blue follows the outward hemisphere. The sampler repeats and bilinearly filters encoded vectors before decoding. Tilt is capped at 30 degrees. Degenerate UV derivatives and sphere poles use the geometric normal. [Shading model](TRANSPORT.md#6-robust-geometry).
