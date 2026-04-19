#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <string>
#include "compressor.h"

// Helper to open a native Ubuntu file dialog
std::string OpenWavFileDialog() {
    char filename[1024];
    // Call zenity to open a file picker filtered to .wav files
    FILE *f = popen("zenity --file-selection --file-filter='*.wav' --title='Select a WAV File' 2>/dev/null", "r");
    if (!f) return "";
    
    if (fgets(filename, 1024, f) != NULL) {
        std::string path(filename);
        // Zenity returns the path with a newline at the end, so we trim it
        path.erase(path.find_last_not_of(" \n\r\t") + 1); 
        pclose(f);
        return path;
    }
    pclose(f);
    return "";
}
// Helper to catch and print GLFW errors
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char**) {
    // 1. Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    // GL 3.0 + GLSL 130 is a safe default for Ubuntu/Mesa drivers
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Audio Compressor Tool", nullptr, nullptr);
    if (window == nullptr) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // 2. Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui::StyleColorsDark();
    
    // 3. Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    std::string selectedFilePath = "No file selected...";
    std::vector<int16_t> loadedAudioData;
    unsigned int audioSampleRate = 0;
    unsigned int audioChannels = 0;

    // --- Compression State Variables ---
    std::vector<int16_t> decompressedAudioData;
    std::string verificationStatus = "N/A";
    double calculatedRatio = 0.0;
    double calculatedRuntime = 0.0;
    
    // --- Miniaudio Setup ---
    ma_engine engine;
    ma_result result = ma_engine_init(NULL, &engine);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize audio engine.\n");
        return -1;
    }

    ma_audio_buffer decompressedBuffer;
    ma_sound decompressedSound;
    bool isDecompressedSoundLoaded = false;

    ma_sound originalSound;
    bool isOriginalSoundLoaded = false;
    // 4. Main rendering loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- UI ---
        ImGui::Begin("Lossless Audio Compressor");

        // Required identifier
        ImGui::Text("Student: Nguyen Hoang Hiep - 20224282");
        ImGui::Separator();

        // --- File Input ---
        if (ImGui::Button("Select WAV File")) {
            std::string newPath = OpenWavFileDialog();
            
            if (!newPath.empty()) {
                selectedFilePath = newPath;
                
                // --- dr_wav Loading Logic ---
                unsigned int channels;
                unsigned int sampleRate;
                drwav_uint64 totalPCMFrameCount;
                
                // Decode the audio into 16-bit PCM format
                int16_t* pSampleData = drwav_open_file_and_read_pcm_frames_s16(
                    selectedFilePath.c_str(), &channels, &sampleRate, &totalPCMFrameCount, NULL);
                    
                if (pSampleData == NULL) {
                    selectedFilePath = "ERROR: Failed to load WAV!";
                } else {
                    // Copy the raw C-array into our safe C++ std::vector
                    loadedAudioData.assign(pSampleData, pSampleData + (totalPCMFrameCount * channels));
                    audioSampleRate = sampleRate;
                    audioChannels = channels;
                    
                    if (isOriginalSoundLoaded) {
                        ma_sound_uninit(&originalSound); // Clean up previous file
                        isOriginalSoundLoaded = false;
                    }
                    
                    if (ma_sound_init_from_file(&engine, selectedFilePath.c_str(), 0, NULL, NULL, &originalSound) == MA_SUCCESS) {
                        isOriginalSoundLoaded = true;
                    }
                    
                    drwav_free(pSampleData, NULL);
                }
            }
        }
        
        ImGui::SameLine();
        ImGui::Text("%s", selectedFilePath.c_str());
        
        // Optional: Show some stats to prove it loaded!
        if (!loadedAudioData.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 
                "Loaded: %zu samples | %d Hz | %d Channels", 
                loadedAudioData.size(), audioSampleRate, audioChannels);
        }

        // --- Execution ---
        if (ImGui::Button("Compress & Verify Live", ImVec2(200, 30))) {
            if (!loadedAudioData.empty()) {
                // 1. Run the compression engine
                CompressionResult result = CompressAudio(loadedAudioData, audioChannels);
                calculatedRuntime = result.runtimeMs;

                // 2. Immediately decompress to test it 
                decompressedAudioData = DecompressAudio(result.compressedData, loadedAudioData.size(), audioChannels);
                // 3. Verify bit-perfect reconstruction
                bool isLossless = VerifyBitPerfect(loadedAudioData, decompressedAudioData);
                verificationStatus = isLossless ? "PASS (Bit-Perfect)" : "FAIL";

                // 4. Calculate Compression Ratio (Original Bytes vs Compressed Bytes)
                size_t originalBytes = loadedAudioData.size() * sizeof(int16_t);
                size_t compressedBytes = result.compressedData.size();
                
                if (originalBytes > 0) {
                    calculatedRatio = (double)compressedBytes / originalBytes * 100.0;
                }
            } else {
                verificationStatus = "ERROR: Load a WAV first!";
            }

            // --- NEW: Load Decompressed Data into Audio Engine ---
            
            // Clean up old sound if you click the button multiple times
            if (isDecompressedSoundLoaded) {
                ma_sound_uninit(&decompressedSound);
                ma_audio_buffer_uninit(&decompressedBuffer);
                isDecompressedSoundLoaded = false;
            }

            // Tell miniaudio about our std::vector's raw memory
            ma_audio_buffer_config config = ma_audio_buffer_config_init(
                ma_format_s16,                                        // 16-bit PCM
                audioChannels,                                        // Stereo/Mono
                decompressedAudioData.size() / audioChannels,         // Total frames
                decompressedAudioData.data(),                         // Pointer to vector
                NULL
            );

            config.sampleRate = audioSampleRate;
            
            ma_audio_buffer_init(&config, &decompressedBuffer);
            
            // Hook the buffer to a sound object so we can play it
            if (ma_sound_init_from_data_source(&engine, &decompressedBuffer, 0, NULL, &decompressedSound) == MA_SUCCESS) {
                isDecompressedSoundLoaded = true;
            } else {
                verificationStatus = "ERROR: Failed to load decompressed audio into engine!";
            }
        }

        

        ImGui::Text("Compression Size: %.2f%% of original", calculatedRatio);
        ImGui::Text("Runtime: %.3f ms", calculatedRuntime);

        ImGui::Spacing();
        ImGui::Separator();

        /// --- Metrics Dashboard ---
        ImGui::Text("System Verification:");
        
        // Color-coded status
        if (verificationStatus.find("PASS") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: %s", verificationStatus.c_str());
        } else if (verificationStatus.find("FAIL") != std::string::npos || verificationStatus.find("ERROR") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Status: %s", verificationStatus.c_str());
        } else {
            ImGui::Text("Status: %s", verificationStatus.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();

        // --- Playback Verification ---
        ImGui::Text("Playback:");
        
        // Dynamic button label based on playing state
        const char* originalBtnText = (isOriginalSoundLoaded && ma_sound_is_playing(&originalSound)) ? "Stop Original" : "Play Original";

        if (ImGui::Button(originalBtnText, ImVec2(120, 0))) {
            if (isOriginalSoundLoaded) {
                if (ma_sound_is_playing(&originalSound)) {
                    // If playing, stop it and rewind to the beginning
                    ma_sound_stop(&originalSound);
                    ma_sound_seek_to_pcm_frame(&originalSound, 0); 
                } else {
                    // If stopped, start it
                    ma_sound_start(&originalSound);
                }
            }
        }
        
        ImGui::SameLine();
        
        // Dynamic button label based on playing state
        const char* decompBtnText = (isDecompressedSoundLoaded && ma_sound_is_playing(&decompressedSound)) ? "Stop Decompressed" : "Play Decompressed";
        if (ImGui::Button(decompBtnText, ImVec2(140, 0))) {
            if (isDecompressedSoundLoaded) {
                if (ma_sound_is_playing(&decompressedSound)) {
                    // Stop and rewind
                    ma_sound_stop(&decompressedSound);
                    ma_sound_seek_to_pcm_frame(&decompressedSound, 0); 
                } else {
                    // If original is playing, stop it so they don't overlap
                    if (isOriginalSoundLoaded && ma_sound_is_playing(&originalSound)) {
                        ma_sound_stop(&originalSound);
                        ma_sound_seek_to_pcm_frame(&originalSound, 0);
                    }
                    ma_sound_start(&decompressedSound);
                }
            }
        }

        ImGui::End();
        // --------------------------

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.00f); // Dark grey background
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // 5. Cleanup
    // --- Miniaudio Cleanup ---
    if (isOriginalSoundLoaded) {
        ma_sound_uninit(&originalSound);
    }
    if (isDecompressedSoundLoaded) {
        ma_sound_uninit(&decompressedSound);
        ma_audio_buffer_uninit(&decompressedBuffer);
    }
    // --- End Miniaudio Cleanup ---
    ma_engine_uninit(&engine);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
