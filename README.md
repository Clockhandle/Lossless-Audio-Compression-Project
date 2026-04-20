# Lossless Audio Compressor

A C++ application with a graphical user interface for lossless audio compression. This project compresses raw PCM audio from `.wav` files using predictive coding and Rice coding, verifies bit-perfect reconstruction, and allows for real-time playback comparison.

## Features
- **Lossless Compression Framework**: Built to utilize linear prediction and optimal Rice coding techniques.
- **Bit-Perfect Verification**: Automatically decompresses and validates every single sample against the original file.
- **Live Playback**: Listen to the original or decompressed audio directly within the app using the `miniaudio` backend.
- **Graphical Interface**: Built with Dear ImGui for real-time metrics and controls.

## Environment Setup (Ubuntu/Linux)

While the single-header libraries (`miniaudio.h`, `dr_wav.h`) and `ImGui` are bundled locally with the project, you need standard build tools and a few system UI libraries.

### 1. Install System Dependencies
Open your terminal and run:
```bash
sudo apt update
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev zenity
```
*(Note: `zenity` is required to open the native Ubuntu file selection dialog).*

## Building the Project

This project uses `CMake`. To compile the program, run the following commands from the root folder of the project:

```bash
# 1. Generate the build files into a folder named "build"
cmake -B build

# 2. Compile the project
cmake --build build
```

## Running the Application

Once the build finishes successfully, you can run the application with:

```bash
./build/Lossless_Audio_Compression_Project
```

## Usage
1. Click **"Select WAV File"** and choose a `.wav` file from your machine (you can record a test file easily using Audacity).
2. Click **"Compress & Verify Live"** to run the audio through the compression engine.
3. Check the **System Verification** panel to see the live compression ratio, execution time, and validation of the bit-perfect reconstruction.
4. Click **"Play Original"** or **"Play Decompressed"** to ensure the audio wasn't degraded!

---
**Author:** Nguyen Hoang Hiep - 20224282
