// synth/synth.cpp
#include "synth.hpp"
#include <cmath>
#include <algorithm>

#include <thread>


namespace resynth {
	namespace {
		Synth* g_synth = nullptr;

		void StreamCallback(void* buffer, unsigned int frames) {
			if (g_synth) g_synth->Render((float*)buffer, (int)frames);
		}
	}

	void Synth::Init(int rate) {
		if (running) return;
		sample_rate = rate;

		g_synth = this;
		init_params();
		stream = LoadAudioStream(sample_rate, 32, 2);
		SetAudioStreamCallback(stream, StreamCallback);
		PlayAudioStream(stream);
		running = true;

		for (int i = 0; i < 88; i++) {
			keys[i].frequency = base_frequency * powf(key_ratio, (float)i);
			keys[i].idx = i;
		}
		for (int i = 0; i < delay_buffer_size; i++)	delay_buffer[i] = 0.0f;

		for (int i = 0; i < 4; i++) {
			comb_pos_l[i] = 0; comb_store_l[i] = 0.0f;
			comb_pos_r[i] = 0; comb_store_r[i] = 0.0f;
		}
		for (int j = 0; j < 4; j++) for (int i = 0; i < 2000; i++) {
			comb_buf_l[j][i] = 0.0f; comb_buf_r[j][i] = 0.0f;
		}

		for (int i = 0; i < 2; i++) { 
			allpass_pos_l[i] = 0; allpass_pos_r[i] = 0; 
		}
		for (int j = 0; j < 2; j++) for (int i = 0; i < 600; i++) {
			allpass_buf_l[j][i] = 0.0f; allpass_buf_r[j][i] = 0.0f;
		}
	}

	void Synth::Shutdown() {
		if (!running) return;
		running = false;
		g_synth = nullptr;
		StopAudioStream(stream);
		UnloadAudioStream(stream);
	}

	void Synth::init_params()
	{
		params[P_LP_CUTOFF] = { "high cut",    1.0f,   0.0f,   1.0f,   1.0f };
		params[P_HP_CUTOFF] = { "low cut",     0.0f,   0.0f,   1.0f,   0.0f };
		params[P_RESONANCE] = { "resonance",   0.0f,   0.0f,   1.3f,   0.0f };
		params[P_DRIVE] = { "drive",       0.0f,   0.0f,   1.0f,   0.0f };
		params[P_DETUNE] = { "detune",      8.0f, -50.0f,  50.0f,   8.0f };
		params[P_OSC_AMP] = { "osc amp",     1.0f,   0.0f,   2.0f,   1.0f };
		params[P_OSC_MIX] = { "osc mix",     0.5f,   0.0f,   1.0f,   0.5f };
		params[P_OSC_STEREO] = { "osc stereo",  0.0f,   0.0f,   1.0f,   0.5f };
		params[P_GRAIN_RATE] = { "rate",       20.0f,   0.0f, 20.0f,  50.0f };
		params[P_GRAIN_POS] = { "position",    0.3f,   0.0f,   1.0f,   0.3f };
		params[P_GRAIN_POS_SPRAY] = { "pos spray",   0.1f,   0.0f,   1.0f,   0.1f };
		params[P_GRAIN_LIFE] = { "life",        0.08f,  0.005f, 0.5f,   0.08f };
		params[P_GRAIN_LIFE_SPRAY] = { "life spray",  0.1f,   0.0f,   1.0f,   0.1f };
		params[P_GRAIN_SHAPE] = { "shape",       0.3f,   0.0f,   1.0f,   0.3f };
		params[P_GRAIN_TILT] = { "tilt",        0.5f,   0.02f,  0.98f,  0.5f };
		params[P_GRAIN_AMP] = { "grain amp",   0.0f,   0.0f,   2.0f,   0.0f };
		params[P_GRAIN_STEREO] = { "grain stereo", 0.0f,   0.0f,   1.0f,   0.5f };
		params[P_SAMPLE_AMP] = { "sample amp",  0.0f,   0.0f,   2.0f,   0.0f };
		params[P_SAMPLE_STEREO] = { "sample stereo", 0.0f,   0.0f,   1.0f,   0.5f };
		params[P_SAMPLE_START] = { "start",       0.0f,   0.0f,   0.99f,  0.0f };
		params[P_SAMPLE_LOOP_START] = { "loop from",   0.0f,   0.0f,   0.99f,  0.0f };
		params[P_DELAY_TIME] = { "time",        0.3f,   0.01f,  2.0f,   0.3f };
		params[P_DELAY_FEEDBACK] = { "feedback",    0.35f,  0.0f,   0.95f,  0.35f };
		params[P_DELAY_AMP] = { "delay amp",   0.0f,   0.0f,   1.0f,   0.0f };
		params[P_DELAY_STEREO] = { "delay stereo", 0.0f,   0.0f,   1.0f,   0.5f };
		params[P_AMPLITUDE] = { "amplitude",   0.15f,  0.0f,   1.0f,   0.15f };
	}

	void Synth::apply_modulation()
	{
		for (int i = 0; i < max_lfos; i++)
		{
			LFO& lfo = lfos[i];
			lfo.phase += 2.0f * PI * lfo.frequency / (float)sample_rate;
			if (lfo.phase > 2.0f * PI) lfo.phase -= 2.0f * PI;
			lfo.value = oscillate(lfo.phase, lfo.shape);
		}
		for (int i = 0; i < PARAM_COUNT; i++)
		{
			params[i].current = params[i].base;
		}
		for (int i = 0; i < routing_count; i++)
		{
			const Routing& r = routings[i];
			if (r.lfo < 0 || r.lfo >= max_lfos) continue;
			if (r.param < 0 || r.param >= PARAM_COUNT) continue;
			const LFO& lfo = lfos[r.lfo];
			float lfo_value = lfo.value;
			params[r.param].current += lfo_value * r.depth * (params[r.param].hi - params[r.param].lo);
			params[r.param].current = std::clamp(params[r.param].current, params[r.param].lo, params[r.param].hi);
		}
	}

	int Synth::find_routing(int lfo, int param) const {
		for (int i = 0; i < max_routings; i++)
			if (routings[i].lfo == lfo && routings[i].param == param) return i;
		return -1;
	}

	void Synth::set_routing(int lfo, int param, float depth) {
		int i = find_routing(lfo, param);

		if (fabsf(depth) < 0.0001f) {
			if (i >= 0) routings[i] = Routing{};
			routing_count--;
			return;
		}

		if (i >= 0) { routings[i].depth = depth; return; }

		for (int s = 0; s < max_routings; s++) {
			if (routings[s].lfo < 0) {
				routings[s] = { lfo, param, depth };
				routing_count++;
				return;
			}
		}
	}

	float Synth::oscillate(float phase, WaveShape shape)
	{
		float t = phase / (2.0f * PI);
		t -= floorf(t);
		float value = 0.0f;
		switch (shape)
		{
		case resynth::SINE:
			value = sinf(phase);
			break;
		case resynth::SAW:
			value = 2.0f * t - 1.0f;
			break;
		case resynth::SQUARE:
			value = t < 0.5f ? 1.0f : -1.0f;
			break;
		case resynth::TRIANGLE:
			value = 4.0f * fabsf(t - 0.5f) - 1.0f;
			break;
		default:
			break;
		}
		return value;
	}

	void Synth::envelope(EnvelopeStage& stage, float& level, float attack_step, float decay_step, float sustain_level, float release_step)
	{
		switch (stage) {
		case EnvelopeStage::ATTACK:
			level += attack_step;
			if (level >= 1.0f) { level = 1.0f; stage = EnvelopeStage::DECAY; }
			break;
		case EnvelopeStage::DECAY:
			level -= decay_step * (1.0f - sustain_level);
			if (level <= sustain_level) { level = sustain_level; stage = EnvelopeStage::SUSTAIN; }
			break;
		case EnvelopeStage::SUSTAIN:
			//sustain levels out smoothly to the sustain level incase the gliding set the level above or below the sustain level
			if (level < sustain_level) {
				level += attack_step;
				if (level > sustain_level) level = sustain_level;
			}
			else if (level > sustain_level) {
				level -= decay_step;
				if (level < sustain_level) level = sustain_level;
			}
			break;
			break;
		case EnvelopeStage::RELEASE:
			level -= release_step;
			if (level <= 0.0f) { level = 0.0f; stage = EnvelopeStage::IDLE; }
			break;
		case EnvelopeStage::IDLE:
			level = 0.0f;
			break;
		}
	}

	float Synth::filter(float in, float lp_coeff, float hp_coeff, Key& key)
	{
		float sig = in;
		//beware of high resonance cuasing this clamp to be nessecary
		//key.out_last = std::clamp(sig, -4.0f, 4.0f);
		//if (!std::isfinite(key.out_last)) key.out_last = 0.0f;
		sig -= key.out_last * params[P_RESONANCE].current;
		for (int i = 0; i < poles; i++)
		{
			key.hp_last[i] = key.hp_last[i] + (sig - key.hp_last[i]) * hp_coeff;
			sig = sig - key.hp_last[i];

			key.lp_last[i] = key.lp_last[i] + (sig - key.lp_last[i]) * lp_coeff;
			sig = key.lp_last[i];
		}
		key.out_last = sig;
		return sig;
	}

	float comb(float in, float* buf, int len, int& pos, float fb, float damp, float& store) {
		float d = buf[pos];
		store = d * (1.0f - damp) + store * damp;
		buf[pos] = in + store * fb;
		pos = (pos + 1) % len;
		return d;
	}

	float allpass(float in, float* buf, int len, int& pos, float g) {
		float d = buf[pos];
		float out = -in + d;
		buf[pos] = in + d * g;
		pos = (pos + 1) % len;
		return out;
	}

	void Synth::Render(float* out, int frames) {
		for (int i = 0; i < frames * 2.0f; i++) out[i] = 0.0f;
		for (int k = 0; k < 88; k++) {
			apply_modulation();
			Key& key = keys[k];
			if (key.stage == IDLE) continue;
			if (!slide || k == sounding_key) glide_freq += (glide_target - glide_freq) * slide_speed;
			double frequency_c = slide ? glide_freq : key.frequency;

			double sample_step = key.frequency / keys[sample_root_key].frequency * ((double)sample.sample_rate / (double)sample_rate);
			if (slide) sample_step = glide_freq / keys[sample_root_key].frequency * ((float)sample.sample_rate / (float)sample_rate);
			const double step = 2.0f * PI * frequency_c / (double)sample_rate;
			float ratio = powf(2.0f, params[P_DETUNE].current / 1200.0f);
			const float step2 = 2.0f * PI * (frequency_c * ratio) / (float)sample_rate;
			const float amp = params[P_AMPLITUDE].current;
			float dt = 1.0f / sample_rate;

			float attack_step = attack > 0.0f ? dt / attack : 1.0f;
			float decay_step = decay > 0.0f ? dt / decay : 1.0f;
			float release_step = release > 0.0f ? dt / release : 1.0f;

			float lp_attack_step = lp_attack > 0.0f ? dt / lp_attack : 1.0f;
			float lp_decay_step = lp_decay > 0.0f ? dt / lp_decay : 1.0f;
			float lp_release_step = lp_release > 0.0f ? dt / lp_release : 1.0f;

			float hp_attack_step = hp_attack > 0.0f ? dt / hp_attack : 1.0f;
			float hp_decay_step = hp_decay > 0.0f ? dt / hp_decay : 1.0f;
			float hp_release_step = hp_release > 0.0f ? dt / hp_release : 1.0f;

			for (int i = 0; i < frames; i++) {
				//envelopes
				envelope(key.stage, key.level, attack_step, decay_step, sustain, release_step);

				envelope(key.lp_cutoff_stage, key.lp_cutoff_level, lp_attack_step, lp_decay_step, lp_sustain, lp_release_step);
				envelope(key.hp_cutoff_stage, key.hp_cutoff_level, hp_attack_step, hp_decay_step, hp_sustain, hp_release_step);
				if (slide && sounding_key != k)continue;//process envelopes of all keys, but only add to out when sliding and key == last key presed
				float lp_cutoff_curr = std::clamp(params[P_LP_CUTOFF].current + lp_env_amount * key.lp_cutoff_level, 0.0f, 1.0f);
				float hp_cutoff_curr = std::clamp(params[P_HP_CUTOFF].current + hp_env_amount * key.hp_cutoff_level, 0.0f, 1.0f);
				dbg_lp = lp_cutoff_curr, dbg_hp = hp_cutoff_curr;
				lp_cutoff_curr = 20 * powf(1000, lp_cutoff_curr);
				hp_cutoff_curr = 20 * powf(1000, hp_cutoff_curr);

				float lp_coeff = 1.f - expf(-2.0f * PI * lp_cutoff_curr / sample_rate);
				float hp_coeff = 1.f - expf(-2.0f * PI * hp_cutoff_curr / sample_rate);

				//oscillate
				float osc_in = (oscillate(key.phase, wave_shape) * params[P_OSC_MIX].current + oscillate(key.phase2, wave_shape2) * (1.0f - params[P_OSC_MIX].current));

				//sample playback
				float sample_in = 0.0f;
				const AudioData& src = (slide && slide_start_key >= 0) ? keys[slide_start_key].sample : key.sample;
				const int n = (int)src.samples.size();
				const double end = std::clamp((double)sample_length, 0.01, 1.0) * n;

				const float* s = src.samples.data();
				auto read = [&](double p) -> float {
					if (p < 0.0 || p + 1.0 >= (double)n) return 0.0f;
					int i = (int)p;
					float f = (float)(p - (double)i);
					return s[i] * (1.0f - f) + s[i + 1] * f;
					};

				if (n > 2) {
					const double loop_begin = (double)params[P_SAMPLE_LOOP_START].current * n;
					const double fade_len = (double)sample_loop_fade_length * n;
					const double fade_begin = (double)end - fade_len;

					sample_in = read(key.sample_pos);

					float early = 0.0f;
					if (sample_loop && fade_len >= 1.0 && key.sample_pos >= fade_begin) {
						float mix = (float)((key.sample_pos - fade_begin) / fade_len);
						mix = std::clamp(mix, 0.0f, 1.0f);
						early = read(loop_begin + (key.sample_pos - fade_begin));
						float a = cosf(mix * PI * 0.5f);
						float b = sinf(mix * PI * 0.5f);
						a = 1.0f - mix;
						b = mix;

						sample_in = sample_in * a + early * b;
					}

					key.sample_pos += sample_step;

					if (key.sample_pos + 1.0 >= end) {
						if (sample_loop) {
							if (fade_len >= 1.0)
								key.sample_pos = loop_begin + (key.sample_pos - fade_begin);
							else
								key.sample_pos = loop_begin + (key.sample_pos - end);
						}
						else {
							key.sample_pos = end;
						}
					}
				}

				//grain synthesis
				float grain_in = 0.0f;
				if (n > 2) {
					if (params[P_GRAIN_AMP].current > 0.0001f) {
						key.grain_spawn_accumulator += dt;
						if (key.grain_spawn_accumulator > 1.0f / params[P_GRAIN_RATE].current) {
							double pos = params[P_GRAIN_POS].current * (double)key.sample.samples.size() + (rand() / (float)RAND_MAX - 0.5f) * params[P_GRAIN_POS_SPRAY].current * (double)key.sample.samples.size();
							pos = std::clamp(pos, 0.0, (double)key.sample.samples.size() - 1.0);
							int life = (int)(params[P_GRAIN_LIFE].current * (double)sample_rate + (rand() / (float)RAND_MAX - 0.5f) * params[P_GRAIN_LIFE_SPRAY].current * (double)sample_rate);
							life = std::clamp(life, 1, (int)sample_rate);
							for (int i = 0; i < grain_amount; i++)
							{
								if (!key.grains[i].active) {
									key.grains[i] = { pos, 0, life, true };
									break;
								}
							}
							key.grain_spawn_accumulator -= 1.0f / params[P_GRAIN_RATE].current;
						}
						int active_grains = 0;
						for (int i = 0; i < grain_amount; i++)
						{
							Grain& g = key.grains[i];
							if (!g.active) continue;
							active_grains++;
							g.age++;
							if (g.age > g.life) {
								g.active = false;
							}
							g.pos += sample_step;

							float grain_level = 1.0f;
							float max_level_duration = (float)g.life * params[P_GRAIN_SHAPE].current;
							float fade_in_duration = ((float)g.life - max_level_duration) * params[P_GRAIN_TILT].current;
							float fade_out_duration = (float)g.life - max_level_duration - fade_in_duration;

							if (fade_in_duration > 0.0f && g.age < fade_in_duration) {
								grain_level = (float)g.age / fade_in_duration;
							}
							else if (fade_out_duration > 0.0f && g.age > fade_in_duration + max_level_duration) {
								grain_level = 1.0f - ((float)g.age - fade_in_duration - max_level_duration) / fade_out_duration;
							}

							grain_in += read(g.pos) * grain_level;
						}
						if (active_grains > 0) grain_in /= sqrtf((float)active_grains);
					}
				}

				//mix all sources together
				osc_in = osc_in * params[P_OSC_AMP].current;
				sample_in = sample_in * params[P_SAMPLE_AMP].current;
				grain_in = grain_in * params[P_GRAIN_AMP].current;
				float in_tot_l = osc_in * (1.0f - params[P_OSC_STEREO].current) + sample_in * (1.0f - params[P_SAMPLE_STEREO].current) + grain_in * (1.0f - params[P_GRAIN_STEREO].current);
				float in_tot_r = osc_in * params[P_OSC_STEREO].current + sample_in * params[P_SAMPLE_STEREO].current + grain_in * params[P_GRAIN_STEREO].current;

				//apply filter and distortion
				float d = 1.0f + params[P_DRIVE].current * 24.0f;
				if (drive_before_filter && drive_enabled) {
					in_tot_l = tanhf(in_tot_l * d) / tanhf(d);
					in_tot_r = tanhf(in_tot_r * d) / tanhf(d);
				}
				float curr_l = filter(in_tot_l, lp_coeff, hp_coeff, key);
				float curr_r = filter(in_tot_r, lp_coeff, hp_coeff, key);
				if (invert) curr_l = in_tot_l - curr_l;
				if (invert) curr_r = in_tot_r - curr_r;
				if (!drive_before_filter && drive_enabled) curr_l = tanhf(curr_l * d) / tanhf(d);
				if (!drive_before_filter && drive_enabled) curr_r = tanhf(curr_r * d) / tanhf(d);
				float output_l = curr_l * amp * key.level;
				float output_r = curr_r * amp * key.level;
				out[i * 2] += output_l;
				out[i * 2 + 1] += output_r;
				curr_out[curr_out_idx] += (curr_l + curr_r) * amp * key.level;
				key.phase += step;
				key.phase2 += step2;
				if (key.phase >= 2.0f * PI) key.phase -= 2.0f * PI;
				if (key.phase2 >= 2.0f * PI) key.phase2 -= 2.0f * PI;
			}
		}
		for (int i = 0; i < frames; i++) {
			//apply delay
			if (params[P_DELAY_FEEDBACK].current > 0.001f) {
				int len = std::clamp((int)(params[P_DELAY_TIME].current * sample_rate), 1, delay_buffer_size / 2);
				if (delay_buffer_pos >= len) delay_buffer_pos = 0;

				float delayed_l = delay_buffer[delay_buffer_pos * 2];
				float delayed_r = delay_buffer[delay_buffer_pos * 2 + 1];

				float fb = params[P_DELAY_FEEDBACK].current;
				float x = params[P_DELAY_STEREO].current;   // 0 = parallel, 1 = full ping-pong

				delay_buffer[delay_buffer_pos * 2] = out[i * 2] + (delayed_l * (1 - x) + delayed_r * x) * fb;
				delay_buffer[delay_buffer_pos * 2 + 1] = out[i * 2 + 1] + (delayed_r * (1 - x) + delayed_l * x) * fb;

				out[i * 2] += delayed_l * params[P_DELAY_AMP].current * params[P_AMPLITUDE].current;
				out[i * 2 + 1] += delayed_r * params[P_DELAY_AMP].current * params[P_AMPLITUDE].current;

				delay_buffer_pos++;
			}
			//apply reverb
			if (params[P_REVERB_AMP].current > 0.001f) {
				float mono = (out[i * 2] + out[i * 2 + 1]) * 0.5f;
				int r_offset = 23;

				float reverb_l = comb(mono, comb_buf_l[0], 1557, comb_pos_l[0], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_l[0]) +
					comb(mono, comb_buf_l[1], 1617, comb_pos_l[1], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_l[1]) +
					comb(mono, comb_buf_l[2], 1492, comb_pos_l[2], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_l[2]) +
					comb(mono, comb_buf_l[3], 1422, comb_pos_l[3], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_l[3]);

				float reverb_r = comb(mono, comb_buf_r[0], 1557 + r_offset, comb_pos_r[0], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_r[0]) +
					comb(mono, comb_buf_r[1], 1617 + r_offset, comb_pos_r[1], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_r[1]) +
					comb(mono, comb_buf_r[2], 1492 + r_offset, comb_pos_r[2], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_r[2]) +
					comb(mono, comb_buf_r[3], 1422 + r_offset, comb_pos_r[3], params[P_REVERB_FEEDBACK].current, params[P_REVERB_DAMP].current, comb_store_r[3]);

				reverb_l = allpass(allpass(reverb_l, allpass_buf_l[0], 225, allpass_pos_l[0], 0.5f), allpass_buf_l[1], 556, allpass_pos_l[1], 0.5f);
				reverb_r = allpass(allpass(reverb_r, allpass_buf_r[0], 225, allpass_pos_r[0], 0.5f), allpass_buf_r[1], 556, allpass_pos_r[1], 0.5f);

				out[i * 2] += reverb_l * params[P_REVERB_AMP].current;
				out[i * 2 + 1] += reverb_r * params[P_REVERB_AMP].current;
			}

			for (int i = 0; i < frames; i++) {
				curr_out[curr_out_idx] = (out[i * 2] + out[i * 2 + 1]) * 0.5f;
				curr_out_idx = (curr_out_idx + 1) % live_spectrum_window_size;
			}
		}
	}
	void Synth::Update() {
		//remove keys from list if no longer pressed
		for (int i = 0; i < (int)last_keys_pressed.size(); i++) {
			if (!keys[last_keys_pressed[i]].is_down) {
				last_keys_pressed.erase(last_keys_pressed.begin() + i);
				i--;
			}
		}

		//new hit
		for (auto& key : keys) {
			int key_idx = (int)(&key - keys);
			if (key.is_down && !key.was_down) {
				if (slide) {
					glide_target = key.frequency;
					last_keys_pressed.push_back(key_idx);
					if (!is_any_key_active(key_idx) || sounding_key == -1) {
						slide_start_key = key_idx;
						glide_freq = key.frequency;
						key.stage = ATTACK;
						key.lp_cutoff_stage = ATTACK;
						key.hp_cutoff_stage = ATTACK;
						key.sample_pos = params[P_SAMPLE_START].current * (float)sample.samples.size();
					}
					else {
						key.take_state_from(keys[sounding_key]);
						if (key.stage == RELEASE)key.stage = SUSTAIN;
					}
				}
				else {
					key.stage = ATTACK;
					key.lp_cutoff_stage = ATTACK;
					key.hp_cutoff_stage = ATTACK;
					key.sample_pos = params[P_SAMPLE_START].current * (float)sample.samples.size();
				}
				sounding_key = (int)(&key - keys);
				key.was_down = true;
			}
			if (!key.is_down && key.was_down) {
				if (slide && !last_keys_pressed.empty()) {
					int next = last_keys_pressed.back();
					if (next != sounding_key) keys[next].take_state_from(keys[sounding_key]);
					if (keys[next].stage == RELEASE)keys[next].stage = SUSTAIN;
					sounding_key = next;
					glide_target = keys[sounding_key].frequency;
				}
				key.stage = RELEASE;
				key.lp_cutoff_stage = RELEASE;
				key.hp_cutoff_stage = RELEASE;
				key.was_down = false;
			}
		}
	}
	void Synth::fill_preview_graph(int resolution, int cycles) {
		if (resolution < 2) return;
		x.resize(resolution); y.resize(resolution);
		float last = 0.0f;
		for (int i = 0; i < resolution; i++) {
			float u = (float)i / (float)(resolution - 1);
			x[i] = u;

			//no filter applied currently, just the raw oscillator output
			float in = oscillate(u * (float)cycles * 2.0f * PI, wave_shape);
			float new_last = 0.0f;
			last = new_last;
			y[i] = in;
		}
	}
	void Synth::fill_live_spectrum() {
		const int idx = curr_out_idx;
		auto& s = live_spectrum.data.samples;
		s.clear();
		s.insert(s.end(), curr_out + idx, curr_out + live_spectrum_window_size);
		s.insert(s.end(), curr_out, curr_out + idx);
		live_spectrum.window_size = live_spectrum_window_size;
		live_spectrum.use_wave_data = false;
		live_spectrum.data.sample_rate = 48000;
		live_spectrum.compute_fourier_data();
	}
	bool Synth::is_any_key_active(int exclude)
	{
		for (int k = 0; k < 88; k++) {
			Key& key = keys[k];
			if (key.stage != IDLE && k != exclude) {
				return true;
			}
		}
		return false;
	}

	//void Synth::load_sample_into_keys()
	//{
	//	for (int k = 0; k < 88; k++) {
	//		Key& key = keys[k];
	//		float factor = key.frequency / keys[root_key].frequency;
	//		key.sample.samples = live_spectrum.stretch(sample.samples, factor, 8192.0f);
	//	}
	//}
	void Synth::load_sample_into_keys() {
		const int n_threads = std::max(1u, std::thread::hardware_concurrency());
		std::vector<std::thread> workers;

		for (int t = 0; t < n_threads; t++) {
			workers.emplace_back([&, t] {
				for (int k = t; k < 88; k += n_threads) {
					float ratio = keys[k].frequency / keys[sample_root_key].frequency;
					keys[k].sample.samples = live_spectrum.stretch(sample.samples, ratio, 8192.0f);
					keys[k].sample.sample_rate = sample.sample_rate;
				}
				});
		}
		for (auto& w : workers) w.join();
	}
} // resynth