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

// Helper to handle the prediction (Now Channel-Aware)
std::vector<int16_t> CalculateResiduals(const std::vector<int16_t>& rawAudio, int numChannels) {
    std::vector<int16_t> residuals;
    residuals.reserve(rawAudio.size());

    for (size_t i = 0; i < rawAudio.size(); ++i) {
        int16_t currentSample = rawAudio[i];
        int16_t previousSample = 0;

        // Look back by the number of channels
        if (i >= (size_t)numChannels) {
            previousSample = rawAudio[i - numChannels];
        }
        
        residuals.push_back(currentSample - previousSample);
    }
    return residuals;
}

// Helper to reverse the prediction (Now Channel-Aware)
std::vector<int16_t> ReconstructAudio(const std::vector<int16_t>& residuals, int numChannels) {
    std::vector<int16_t> reconstructed;
    reconstructed.reserve(residuals.size());

    for (size_t i = 0; i < residuals.size(); ++i) {
        int16_t previousSample = 0;
        
        // Look back by the number of channels in the RECONSTRUCTED array
        if (i >= (size_t)numChannels) {
            previousSample = reconstructed[i - numChannels];
        }
        
        reconstructed.push_back(residuals[i] + previousSample);
    }
    return reconstructed;
}

int FindOptimalK(const std::vector<int16_t>& residuals, size_t start, size_t end) {
    int bestK = 8;
    uint64_t minBits = UINT64_MAX; 

    for (int k = 4; k <= 14; ++k) {
        uint64_t totalBits = 0;
        
        // Only loop through THIS specific frame's samples
        for (size_t i = start; i < end; ++i) {
            uint16_t mapped = ZigZagEncode(residuals[i]);
            uint16_t q = mapped >> k;
            totalBits += (q + 1) + k; 
        }
        
        if (totalBits < minBits) {
            minBits = totalBits;
            bestK = k;
        }
    }
    
    return bestK;
}

CompressionResult CompressAudio(const std::vector<int16_t>& rawAudio, int numChannels) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // 1. Prediction Phase (Global)
    std::vector<int16_t> residuals = CalculateResiduals(rawAudio, numChannels);

    // 2. Entropy Phase (Framed)
    BitWriter writer;
    const size_t FRAME_SIZE = 4096; // Standard FLAC block size

    // Step through the audio in 4096-sample chunks
    for (size_t i = 0; i < residuals.size(); i += FRAME_SIZE) {
        // Prevent out-of-bounds on the very last chunk
        size_t endIdx = std::min(i + FRAME_SIZE, residuals.size());

        // Find the mathematically perfect K for THIS specific frame
        int optimalK = FindOptimalK(residuals, i, endIdx);

        // Frame Header: Write the winning K into the bitstream (8 bits)
        writer.WriteBits(optimalK, 8); 

        // Frame Payload: Encode just the samples in this block
        for (size_t j = i; j < endIdx; ++j) {
            EncodeRice(writer, residuals[j], optimalK);
        }
    }
    
    writer.Flush();

    auto stop_time = std::chrono::high_resolution_clock::now();
    double runtime = std::chrono::duration<double, std::milli>(stop_time - start_time).count();

    return {writer.buffer, runtime};
}


std::vector<int16_t> DecompressAudio(const std::vector<uint8_t>& compressedData, size_t expectedSamples, int numChannels) {
    BitReader reader(compressedData);
    std::vector<int16_t> decodedResiduals;
    decodedResiduals.reserve(expectedSamples);

    const size_t FRAME_SIZE = 4096;

    // Step through the expected audio in 4096-sample chunks
    for (size_t i = 0; i < expectedSamples; i += FRAME_SIZE) {
        // Prevent out-of-bounds on the very last chunk
        size_t endIdx = std::min(i + FRAME_SIZE, expectedSamples);

        // Frame Header: Read the first 8 bits to find the K-value for this frame
        int optimalK = reader.ReadBits(8);

        // Frame Payload: Decode the samples for this block
        for (size_t j = i; j < endIdx; ++j) {
            decodedResiduals.push_back(DecodeRice(reader, optimalK));
        }
    }

    // Run the inverse prediction to get the original PCM back
    return ReconstructAudio(decodedResiduals, numChannels);
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