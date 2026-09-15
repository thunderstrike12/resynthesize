#include "audio_player.hpp"
#include <cstring>

namespace resynth {

    Sound MakeSoundFromBuffer(const std::vector<float>& buffer, int sample_rate) {
        Wave wave = {};
        wave.frameCount = (unsigned int)buffer.size();
        wave.sampleRate = (unsigned int)sample_rate;
        wave.sampleSize = 32;
        wave.channels = 1;

        float* copy = (float*)MemAlloc((unsigned int)(buffer.size() * sizeof(float)));
        memcpy(copy, buffer.data(), buffer.size() * sizeof(float));
        wave.data = copy;

        Sound sound = LoadSoundFromWave(wave); // copies internally, safe to free after
        UnloadWave(wave);
        return sound;
    }

}  // namespace resynth