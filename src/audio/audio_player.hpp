#pragma once
#include "raylib.h"
#include <vector>

namespace resynth {
	Sound MakeSoundFromBuffer(const std::vector<float>& buffer, int sample_rate);
}