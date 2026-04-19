#define DR_WAV_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION

#include "compressor.h"
#include <chrono>
#include <iostream>
#include <cstring>

// --- 1. The Bit Bucket ---
class BitWriter {
public:
    std::vector<uint8_t> buffer;
    uint8_t currentByte = 0;
    int bitCount = 0;

    // Push a single 1 or 0 into the bucket
    void WriteBit(int bit) {
        if (bit) {
            currentByte |= (1 << (7 - bitCount)); // Set the bit at the correct position
        }
        bitCount++;
        
        // If the bucket is full (8 bits), push it to the vector and reset
        if (bitCount == 8) {
            buffer.push_back(currentByte);
            currentByte = 0;
            bitCount = 0;
        }
    }

    // Push multiple bits
    void WriteBits(uint32_t value, int numBits) {
        for (int i = numBits - 1; i >= 0; i--) {
            WriteBit((value >> i) & 1);
        }
    }

    // Push any leftover bits when we finish
    void Flush() {
        if (bitCount > 0) {
            buffer.push_back(currentByte);
        }
    }
};

// --- 2. ZigZag Helper ---
inline uint16_t ZigZagEncode(int16_t value) {
    return (value << 1) ^ (value >> 15);
}

// --- 3. The Rice Encoder ---
void EncodeRice(BitWriter& writer, int16_t residual, int k) {
    // Map negative/positive to unsigned
    uint16_t mapped = ZigZagEncode(residual);

    // Fast bitwise math for Quotient and Remainder
    uint16_t q = mapped >> k; 
    uint16_t r = mapped & ((1 << k) - 1); 

    // Write Unary Quotient (q '1's followed by a '0')
    for (uint16_t i = 0; i < q; i++) {
        writer.WriteBit(1);
    }
    writer.WriteBit(0);

    // Write Binary Remainder (k bits long)
    writer.WriteBits(r, k);
}

// --- 1. The Bit Bucket Reader ---
class BitReader {
public:
    const std::vector<uint8_t>& buffer;
    size_t byteIndex = 0;
    int bitCount = 0;

    // Must be initialized with an existing byte array
    BitReader(const std::vector<uint8_t>& buf) : buffer(buf) {}

    // Pop a single bit from the bucket
    int ReadBit() {
        if (byteIndex >= buffer.size()) return 0; // Safety check

        // Extract the bit at the current position
        int bit = (buffer[byteIndex] >> (7 - bitCount)) & 1;
        bitCount++;
        
        // If we've read 8 bits, move to the next byte
        if (bitCount == 8) {
            bitCount = 0;
            byteIndex++;
        }
        return bit;
    }

    // Pop multiple bits
    uint32_t ReadBits(int numBits) {
        uint32_t value = 0;
        for (int i = 0; i < numBits; i++) {
            value = (value << 1) | ReadBit();
        }
        return value;
    }
};

// --- 2. ZigZag Decoder ---
inline int16_t ZigZagDecode(uint16_t value) {
    // Reverses the bitwise ZigZag logic back into signed integers
    return (value >> 1) ^ -(value & 1);
}

// --- 3. The Rice Decoder ---
int16_t DecodeRice(BitReader& reader, int k) {
    uint16_t q = 0;
    
    // Read the Unary Quotient (Count the '1's until we hit a '0')
    while (reader.ReadBit() == 1) {
        q++;
    }

    // Read the Binary Remainder
    uint16_t r = reader.ReadBits(k);

    // Recombine them and reverse the ZigZag mapping
    uint16_t mapped = (q << k) | r;
    return ZigZagDecode(mapped);
}

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
    auto start = std::chrono::high_resolution_clock::now();

    // 1. Prediction Phase
    std::vector<int16_t> residuals = CalculateResiduals(rawAudio);

    // 2. Entropy Phase (Rice Coding)
    BitWriter writer;
    int k = 8; // Standard starting K-value for audio

    for (size_t i = 0; i < residuals.size(); ++i) {
        EncodeRice(writer, residuals[i], k);
    }
    
    writer.Flush(); // Don't forget the leftover bits!

    auto stop = std::chrono::high_resolution_clock::now();
    double runtime = std::chrono::duration<double, std::milli>(stop - start).count();

    // Return our tightly packed bitstream
    return {writer.buffer, runtime};
}


std::vector<int16_t> DecompressAudio(const std::vector<uint8_t>& compressedData, size_t expectedSamples) {
    BitReader reader(compressedData);
    std::vector<int16_t> decodedResiduals;
    decodedResiduals.reserve(expectedSamples);

    int k = 8; // Must match the k-value used in CompressAudio

    // Decode exactly the number of samples we expect
    for (size_t i = 0; i < expectedSamples; ++i) {
        decodedResiduals.push_back(DecodeRice(reader, k));
    }

    // Run the inverse prediction to get the original PCM back
    return ReconstructAudio(decodedResiduals);
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