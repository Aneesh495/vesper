# 10 Analytic pulse accumulation

Integrate the convolved pulse analytically per bin instead of jittering arrival time.

A single path can deposit fractional energy in several bins with lower timing variance. Work scales with occupied bins. [Implementation](../../src/film.c).
