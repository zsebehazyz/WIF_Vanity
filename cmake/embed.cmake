file(READ "${INPUT}" content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")
file(WRITE "${OUTPUT}" "#pragma once\nalignas(8) inline constexpr unsigned char cuda_image[]={${content}};\ninline constexpr int cuda_min_arch=${ARCH};\n")
