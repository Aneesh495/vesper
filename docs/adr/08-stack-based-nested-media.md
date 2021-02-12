# 08 Stack-based nested media

Track boundary IDs in a bounded stack instead of guessing interior from direction.

Mismatched exits and overlapping inconsistent solids fail. Reflected rays leave the stack unchanged. [Implementation](../../src/transport.c).
