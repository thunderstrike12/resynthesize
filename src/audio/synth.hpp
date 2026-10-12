// synth/synth.hpp
#pragma once
#include "fourier.hpp"
#include "raylib.h"
#include <vector>

const float key_ratio = 1.05946309436;
const int key_amount = 88;
const int live_spectrum_window_size = 4096;
const int grain_amount = 20;
const int delay_buffer_size = 96000;

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
	struct Grain {
		double pos;        // read position in the sample
		int    age;        // samples since birth
		int    life;       // samples it lives for
		bool   active;
	};

	class Key {
	public:
		int idx = -1;
		float frequency = 440.0f;

		EnvelopeStage stage = IDLE;
		float level = 0.0f;
		bool was_down = false;
		bool is_down = false;

		// granular synthesis state
		Grain grains[grain_amount];
		double grain_spawn_accumulator = 0.0;

		// sample state
		AudioData sample;
		double sample_pos = 0.0;

		// oscillator state
		float phase = 0.0f;
		float phase2 = 0.0f;

		// filter state
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
			sample_pos = o.sample_pos;

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


		int sample_rate = 48000;
		bool running = false;

		Key keys[key_amount];

		float dbg_lp = 0.0f, dbg_hp = 0.0f;
		float curr_out[live_spectrum_window_size];
		int curr_out_idx = 0;
		Fourier live_spectrum;

		// UI-thread parameters. Read by the audio thread every block.
		float base_frequency = 27.5;
		float amplitude = 0.1f;   // start silent
		float lp_cutoff = 48000.0f;
		float hp_cutoff = 0.0f;
		bool invert = false;
		float osc_lerp = 0.0f;
		float osc_amp = 1.0f;
		WaveShape wave_shape = SINE;
		WaveShape wave_shape2 = SAW;

		// effect parameters
		float delay_time = 0.5f;						// delay time in ms
		float delay_feedback = 0.5f;					// feedback in % - (0.0 to 1.0)
		float delay_buffer[delay_buffer_size];
		int delay_buffer_pos = 0.0f;
		float delay_amp = 0.5f;							// delay amplitude in % - (0.0 to 1.0)	

		bool drive_enabled = false;							// enable drive
		float drive = 0.0f;								// drive in % - (0.0 to 1.0)
		bool drive_before_filter = true;				// drive before filter or after filter

		// grain parameters
		float grain_mix = 1.0f;							// mix between sample and grainsin % - (0.0 to 1.0)
		float grain_spawn_rate_per_second = 10.0f;		// grains per second
		float grain_spawn_pos = 1.0f;					// position in sample in %  - (0.0 to 1.0)
		float grain_spawn_pos_spray = 0.0f;				// position spray in sample in ms
		float grain_life = 0.1f;						// life in ms
		float grain_life_spray = 0.0f;					// life spray in ms
		float grain_shape = 0.5f;						// shape of the grain envelope (0.0 to 1.0)
		float grain_tilt = 0.0f;						// tilt of the grain envelope (0.0 to 1.0)
		float grain_amp = 0.0f;

		// sample parameters
		void load_sample_into_keys();
		AudioData sample; 
		int sample_root_key = 27; 
		bool sample_loop = false;
		float sample_loop_fade = false;
		float sample_loop_fade_length = 1.0f;
		float sample_length = 1.0f;
		float sample_mix = 0.0f;
		float sample_start = 0.0f;
		float sample_loop_start = 0.0f;
		float sample_amp = 1.0f;

		// slide parameters
		int sounding_key = -1;
		int slide_start_key = -1;
		bool  slide = false;
		float slide_speed = 0.01f;
		float glide_freq = 440.0f;
		float glide_target = 440.0f;
		float glide_phase = 0.0f;
		std::vector<int> last_keys_pressed;

		// filter parameters
		int poles = 4;
		float resonance = 1.0f;
		float cents = 10.0f;

		float attack = 0.01f;
		float decay = 1.0f;
		float sustain = 1.0f;
		float release = 0.1f;

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