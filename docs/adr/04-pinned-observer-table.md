# 04 Pinned observer table

Check and embed the upstream CIE table at build time.

The input bytes have a stable provenance and builds work without network data access. Configure fails if the checksum changes. [Implementation](../../cmake/observer.cmake).
