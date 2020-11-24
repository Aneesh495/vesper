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
