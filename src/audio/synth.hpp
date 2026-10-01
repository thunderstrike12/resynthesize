// synth/synth.hpp
#pragma once
#include "fourier.hpp"
#include "raylib.h"
#include <vector>

const float key_ratio = 1.05946309436;
const int key_amount = 88;
const int live_spectrum_window_size = 4096;

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
		int idx = -1;

		EnvelopeStage stage = IDLE;
		float level = 0.0f;
		bool was_down = false;
		bool is_down = false;

		float phase = 0.0f;
		float phase2 = 0.0f;
		float frequency = 440.0f;

		float out_last = 0.0f;
		float lp_last[4] = {};
		float hp_last[4] = {};
		float lp_cutoff_adsr_state = 0.0f;
		float hp_cutoff_adsr_state = 0.0f;
		EnvelopeStage lp_cutoff_stage = IDLE;
		EnvelopeStage hp_cutoff_stage = IDLE;
		float lp_cutoff_level = 0.0f;
		float hp_cutoff_level = 0.0f;

		void take_state_from(const Key& o) {
			stage = o.stage;
			level = o.level;

			phase = o.phase;
			phase2 = o.phase2;

			out_last = o.out_last;
			for (int i = 0; i < 4; i++) { lp_last[i] = o.lp_last[i]; hp_last[i] = o.hp_last[i]; }

			lp_cutoff_stage = o.lp_cutoff_stage;
			hp_cutoff_stage = o.hp_cutoff_stage;
			lp_cutoff_level = o.lp_cutoff_level;
			hp_cutoff_level = o.hp_cutoff_level;
		}
	};

	class Synth {
	public:
		// Called once after InitAudioDevice(). Opens the stream and starts it.
		void Init(int sample_rate = 48000);
		void Shutdown();

		// Audio-thread only. Fills `frames` mono float samples.
		void Render(float* out, int frames);
		void Update();

		float oscillate(float phase, WaveShape shape);
		void envelope(EnvelopeStage& stage, float& level, float attack_step, float decay_step, float sustain_level, float release_step);
		float filter(float in, float lp_coeff, float hp_coeff, Key& key);


		void fill_preview_graph(int resolution, int cycles);
		void fill_live_spectrum();

		bool is_any_key_active(int exclude);


		float dbg_lp = 0.0f, dbg_hp = 0.0f;
		float curr_out[live_spectrum_window_size];
		int curr_out_idx = 0;
		Fourier live_spectrum;

		// UI-thread parameters. Read by the audio thread every block.
		float base_frequency = 27.5;
		float amplitude = 0.1f;   // start silent
		float lp_cutoff = 0.0f;
		float hp_cutoff = 0.0f;
		bool invert = false;
		float osc_lerp = 0.0f;
		WaveShape wave_shape = SINE;
		WaveShape wave_shape2 = SAW;

		int sample_rate = 48000;
		bool running = false;

		Key keys[key_amount];

		int sounding_key = -1;
		bool  slide = false;
		float slide_speed = 0.01f;
		float glide_freq = 440.0f;
		float glide_target = 440.0f;
		float glide_phase = 0.0f;
		std::vector<int> last_keys_pressed;

		int poles = 4;
		float resonance = 1.0f;
		float cents = 10.0f;

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