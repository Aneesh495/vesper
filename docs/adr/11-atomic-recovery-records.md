# 11 Atomic recovery records

Write a versioned film and publish it by atomic rename instead of overwriting in place.

Partial writes leave the last complete checkpoint available. Source ABI and scene identity prevent incompatible resumes. [Implementation](../../src/checkpoint.c).
