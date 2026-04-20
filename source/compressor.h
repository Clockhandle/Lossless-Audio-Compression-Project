#pragma once
#include <vector>
#include <cstdint>

#include "dr_wav.h"
#include "miniaudio.h"

struct CompressionResult {
    std::vector<uint8_t> compressedData; // The compressed audio data
    double runtimeMs; // Time taken to compress in milliseconds
};

std::vector<int16_t> CalculateResiduals(const std::vector<int16_t>& rawAudio, int numChannels);
std::vector<int16_t> ReconstructAudio(const std::vector<int16_t>& residuals);
CompressionResult CompressAudio(const std::vector<int16_t>& rawAudio, int numChannels);
std::vector<int16_t> DecompressAudio(const std::vector<uint8_t>& compressedData, size_t expectedSamples, int numChannels);
bool VerifyBitPerfect(const std::vector<int16_t>& original, const std::vector<int16_t>& decompressed);
