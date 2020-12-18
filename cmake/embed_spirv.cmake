file(READ "${INPUT}" binary HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${binary}")
file(WRITE "${OUTPUT}" "_Alignas(4) static const unsigned char vs_wavefront[] = {${bytes}};\n")
