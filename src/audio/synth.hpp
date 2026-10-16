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

	// ---- modulation ---------------------------------------------------
	constexpr int max_lfos = 8;
	constexpr int max_routings = 64;

	enum ParamId {
		P_LP_CUTOFF, P_HP_CUTOFF, P_RESONANCE, 
		P_DRIVE,
		P_DETUNE, P_OSC_MIX, P_OSC_AMP, P_OSC_STEREO,
		P_GRAIN_RATE, 
		P_GRAIN_POS, P_GRAIN_POS_SPRAY, 
		P_GRAIN_LIFE, P_GRAIN_LIFE_SPRAY,
		P_GRAIN_SHAPE, P_GRAIN_TILT,
		P_GRAIN_AMP, P_GRAIN_STEREO,
		P_SAMPLE_START, P_SAMPLE_LOOP_START, P_SAMPLE_AMP, P_SAMPLE_STEREO,
		P_DELAY_AMP,P_DELAY_TIME, P_DELAY_FEEDBACK,  P_DELAY_STEREO,
		P_REVERB_AMP, P_REVERB_FEEDBACK, P_REVERB_DAMP,
		P_AMPLITUDE,
		PARAM_COUNT
	};

	struct Param {
		const char* name = "";
		float base = 0.0f;				// the slider owns this
		float lo = 0.0f, hi = 1.0f;
		float current = 0.0f;			// Render writes this every sample; UI only reads it
	};

	struct LFO {
		float     frequency = 1.0f;     // Hz
		WaveShape shape = SINE;
		float     phase = 0.0f;         // audio thread only
		float     value = 0.0f;         // audio thread writes, UI reads for display
		bool      retrigger = false;    // reset phase on note-on
	};

	struct Routing {
		int   lfo = -1;                 // -1 = empty slot
		int   param = -1;
		float depth = 0.0f;             // -1..1, fraction of the param's range
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
		void Init(int sample_rate = 48000);
		void Shutdown();

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
		bool invert = false;
		WaveShape wave_shape = SINE;
		WaveShape wave_shape2 = SAW;

		// modulation parameters
		Param   params[PARAM_COUNT];
		LFO     lfos[max_lfos];
		Routing routings[max_routings];
		int     routing_count = 0;

		void  init_params();
		float param(ParamId id) const { return params[id].current; }
		void  apply_modulation();
		int   find_routing(int lfo, int param) const;
		void  set_routing(int lfo, int param, float depth);

		// effect parameters
		float delay_buffer[delay_buffer_size];
		int delay_buffer_pos = 0.0f;

		bool drive_enabled = false;
		bool drive_before_filter = true;

		float comb_buf_l[4][2000];
		int   comb_pos_l[4];
		float comb_store_l[4];
		float allpass_buf_l[2][600];
		int   allpass_pos_l[2];

		float comb_buf_r[4][2000];
		int   comb_pos_r[4];
		float comb_store_r[4];
		float allpass_buf_r[2][600];
		int   allpass_pos_r[2];

		// sample parameters
		void load_sample_into_keys();
		AudioData sample; 
		int sample_root_key = 27; 
		bool sample_loop = false;
		float sample_loop_fade = false;
		float sample_loop_fade_length = 1.0f;
		float sample_length = 1.0f;

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