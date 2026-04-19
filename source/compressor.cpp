#define DR_WAV_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION

#include "compressor.h"
#include <chrono>
#include <iostream>
#include <cstring>

// Helper to handle the prediction
std::vector<int16_t> CalculateResiduals(const std::vector<int16_t>& rawAudio) {
    std::vector<int16_t> residuals;
    residuals.reserve(rawAudio.size());

    int16_t previousSample = 0;
    for (size_t i = 0; i < rawAudio.size(); ++i) {
        int16_t currentSample = rawAudio[i];
        
        // 1st-Order Prediction: We guess the current sample is exactly the same as the previous one.
        // The "residual" is the difference between our guess and reality.
        int16_t residual = currentSample - previousSample;
        
        residuals.push_back(residual);
        previousSample = currentSample; // Update previous for the next loop
    }
    return residuals;
}

// Helper to reverse the prediction
std::vector<int16_t> ReconstructAudio(const std::vector<int16_t>& residuals) {
    std::vector<int16_t> reconstructed;
    reconstructed.reserve(residuals.size());

    int16_t previousSample = 0;
    for (size_t i = 0; i < residuals.size(); ++i) {
        // To rebuild, we add the residual back to our last known sample
        int16_t currentSample = previousSample + residuals[i];
        
        reconstructed.push_back(currentSample);
        previousSample = currentSample;
    }
    return reconstructed;
}


CompressionResult CompressAudio(const std::vector<int16_t>& rawAudio) {
    // START TIMING
    auto start = std::chrono::high_resolution_clock::now();

    // 1. Prediction Phase (Make the numbers small)
    std::vector<int16_t> residuals = CalculateResiduals(rawAudio);

    // 2. Entropy Phase (Pack into bits)
    // TODO: Implement Rice Coding here. 
    // For right now, we are just blindly copying the 16-bit residuals into an 8-bit array 
    // so the program compiles. This will NOT compress the file yet!
    std::vector<uint8_t> dummyBitstream(
        reinterpret_cast<uint8_t*>(residuals.data()),
        reinterpret_cast<uint8_t*>(residuals.data() + residuals.size())
    );

    // STOP TIMING
    auto stop = std::chrono::high_resolution_clock::now();
    double runtime = std::chrono::duration<double, std::milli>(stop - start).count();

    return {dummyBitstream, runtime};
}


std::vector<int16_t> DecompressAudio(const std::vector<uint8_t>& compressedData) {
    // Reverse the dummy copy (Extract the residuals)
    std::vector<int16_t> residuals(compressedData.size() / 2);
    memcpy(residuals.data(), compressedData.data(), compressedData.size());

    // Reconstruct the original audio from the residuals
    return ReconstructAudio(residuals);
}


bool VerifyBitPerfect(const std::vector<int16_t>& original, const std::vector<int16_t>& decompressed) {
    // If the sizes don't match, it's instantly a failure
    if (original.size() != decompressed.size()) return false;

    // Check sample by sample. If even one bit is different, it is not lossless!
    for (size_t i = 0; i < original.size(); ++i) {
        if (original[i] != decompressed[i]) {
            return false;
        }
    }
    return true;
}