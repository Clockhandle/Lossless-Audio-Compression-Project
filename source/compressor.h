#include <vector>
#include <cstdint>
#include "dr_wav.h"
#include "miniaudio.h"
class CompressionResult {
public:
    std::vector<uint8_t> compressedData; // The compressed audio data
    double compressionTimeMs; // Time taken to compress in milliseconds
};