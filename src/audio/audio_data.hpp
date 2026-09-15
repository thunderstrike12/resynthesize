#pragma once
#include <vector>
#include <string>

namespace resynth {

    struct AudioData {
        AudioData() = default;

        bool load(const std::string& path);
        void unload();

        std::vector<float> samples; // mono, normalized [-1, 1]
        int sample_rate = 0;
        int channels = 0;
    };

}  // namespace tmt