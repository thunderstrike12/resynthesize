// synth/synth.hpp
#pragma once
#include "raylib.h"
#include <vector>

const float key_ratio = 1.05946309436;
const int key_amount = 88;

namespace resynth {
	enum EnvelopeStage {
		IDLE,
		ATTACK,
		DECAY,
		SUSTAIN,
		RELEASE

	};
	enum WaveShape {
		SINE,
		SAW,
		SQUARE,
		TRIANGLE
	};
	class Key {
	public:
		EnvelopeStage stage = IDLE;
		float level = 0.0f;
		bool was_down = false;
		bool is_down = false;

		float phase = 0.0f;
		float frequency = 440.0f;

		float lp_last = 0.0f;
		float hp_last = 0.0f;
		float lp_cutoff_adsr_state = 0.0f;
		float hp_cutoff_adsr_state = 0.0f;
		EnvelopeStage lp_cutoff_stage = IDLE;
		EnvelopeStage hp_cutoff_stage = IDLE;
		float lp_cutoff_level = 0.0f;
		float hp_cutoff_level = 0.0f;
	};

	class Synth {
	public:
		// Called once after InitAudioDevice(). Opens the stream and starts it.
		void Init(int sample_rate = 48000);
		void Shutdown();

		// Audio-thread only. Fills `frames` mono float samples.
		void Render(float* out, int frames);
		void Update();

		float oscillate(float phase);
		void envelope(EnvelopeStage& stage, float& level, float attack_step, float decay_step, float sustain_level, float release_step);
		float filter(float in, float lp_coeff, float hp_coeff, Key& curr);


		void fill_preview_graph(int resolution, int cycles);

		float dbg_lp = 0.0f, dbg_hp = 0.0f;

		// UI-thread parameters. Read by the audio thread every block.
		float base_frequency = 27.5;
		float amplitude = 0.1f;   // start silent
		float lp_cutoff = 0.0f;
		float hp_cutoff = 0.0f;
		bool invert = false;
		WaveShape wave_shape = SINE;

		int sample_rate = 48000;
		bool running = false;

		Key keys[key_amount];

		float attack = 0.0f;
		float decay = 0.0f;
		float sustain = 0.0f;
		float release = 0.0f;

		float lp_attack = 0.0f;
		float lp_decay = 0.0f;
		float lp_sustain = 0.0f;
		float lp_release = 0.0f;
		float lp_env_amount = 0.5f;

		float hp_attack = 0.0f;
		float hp_decay = 0.0f;
		float hp_sustain = 0.0f;
		float hp_release = 0.0f;
		float hp_env_amount = 0.5f;

		std::vector<float> x, y;

	private:
		AudioStream stream{};
	};

}  // namespace resynth