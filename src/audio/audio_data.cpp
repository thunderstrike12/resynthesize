#include "audio_data.hpp"
#include "raylib.h"
#include <cstdio>

namespace resynth {

    bool AudioData::load(const std::string& path) {
        Wave wave = LoadWave(path.c_str()); // reads file from disk, decodes based on extension
        if (wave.data == nullptr || wave.frameCount == 0) {
            printf("Failed to load audio file: %s\n", path.c_str());
            return false;
        }

        sample_rate = (int)wave.sampleRate;
        channels = (int)wave.channels;

        samples.clear();
        samples.reserve(wave.frameCount);

        if (wave.sampleSize == 16) {
            int16_t* raw = (int16_t*)wave.data;
            for (unsigned int i = 0; i < wave.frameCount; i++) {
                float sum = 0.0f;
                for (int c = 0; c < channels; c++) sum += raw[i * channels + c] / 32768.0f;
                samples.push_back(sum / (float)channels);
            }
        }
        else if (wave.sampleSize == 32) {
            float* raw = (float*)wave.data;
            for (unsigned int i = 0; i < wave.frameCount; i++) {
                float sum = 0.0f;
                for (int c = 0; c < channels; c++) sum += raw[i * channels + c];
                samples.push_back(sum / (float)channels);
            }
        }
        else if (wave.sampleSize == 8) {
            unsigned char* raw = (unsigned char*)wave.data;
            for (unsigned int i = 0; i < wave.frameCount; i++) {
                float sum = 0.0f;
                for (int c = 0; c < channels; c++) sum += ((float)raw[i * channels + c] - 128.0f) / 128.0f;
                samples.push_back(sum / (float)channels);
            }
        }

        UnloadWave(wave);
        return true;
    }

    void AudioData::unload() {
        samples.clear();
        sample_rate = 0;
        channels = 0;
    }

}  // namespace tmt