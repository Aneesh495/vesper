# 05 Binned SAH

Binned surface area cost instead of a centroid median throughout the tree.

Reduces overlap where the geometry permits a lower-cost split. A midpoint fallback bounds large degenerate leaves. [Implementation](../../src/bvh.c).
