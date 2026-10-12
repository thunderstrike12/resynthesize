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
		stream = LoadAudioStream(sample_rate, 32, 1);
		SetAudioStreamCallback(stream, StreamCallback);
		PlayAudioStream(stream);
		running = true;

		for (int i = 0; i < 88; i++) {
			keys[i].frequency = base_frequency * powf(key_ratio, (float)i);
			keys[i].idx = i;
		}
		for (int i = 0; i < delay_buffer_size; i++)
		{
			delay_buffer[i] = 0.0f;
		}
	}

	void Synth::Shutdown() {
		if (!running) return;
		running = false;
		g_synth = nullptr;
		StopAudioStream(stream);
		UnloadAudioStream(stream);
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
		sig -= key.out_last * resonance;
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

	void Synth::Render(float* out, int frames) {
		for (int i = 0; i < frames; i++) out[i] = 0.0f;
		for (int k = 0; k < 88; k++) {
			Key& key = keys[k];
			if (key.stage == IDLE) continue;
			if (!slide || k == sounding_key) glide_freq += (glide_target - glide_freq) * slide_speed;
			double frequency_c = slide ? glide_freq : key.frequency;

			double sample_step = key.frequency / keys[sample_root_key].frequency * ((double)sample.sample_rate / (double)sample_rate);
			if (slide) sample_step = glide_freq / keys[sample_root_key].frequency * ((float)sample.sample_rate / (float)sample_rate);
			const double step = 2.0f * PI * frequency_c / (double)sample_rate;
			float ratio = powf(2.0f, cents / 1200.0f);
			const float step2 = 2.0f * PI * (frequency_c * ratio) / (float)sample_rate;
			const float amp = amplitude;
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
				float lp_cutoff_curr = std::clamp(lp_cutoff + lp_env_amount * key.lp_cutoff_level, 0.0f, 1.0f);
				float hp_cutoff_curr = std::clamp(hp_cutoff + hp_env_amount * key.hp_cutoff_level, 0.0f, 1.0f);
				dbg_lp = lp_cutoff_curr, dbg_hp = hp_cutoff_curr;
				lp_cutoff_curr = 20 * powf(1000, lp_cutoff_curr);
				hp_cutoff_curr = 20 * powf(1000, hp_cutoff_curr);

				float lp_coeff = 1.f - expf(-2.0f * PI * lp_cutoff_curr / sample_rate);
				float hp_coeff = 1.f - expf(-2.0f * PI * hp_cutoff_curr / sample_rate);

				//oscillate
				float osc_in = (oscillate(key.phase, wave_shape) * osc_lerp + oscillate(key.phase2, wave_shape2) * (1.0f - osc_lerp));

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
					const double loop_begin = (double)sample_loop_start * n;
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
					if (grain_mix > 0.0001f) {
						key.grain_spawn_accumulator += dt;
						if (key.grain_spawn_accumulator > 1.0f / grain_spawn_rate_per_second) {
							double pos = grain_spawn_pos * (double)key.sample.samples.size() + (rand() / (float)RAND_MAX - 0.5f) * grain_spawn_pos_spray * (double)key.sample.samples.size();
							pos = std::clamp(pos, 0.0, (double)key.sample.samples.size() - 1.0);
							int life = (int)(grain_life * (double)sample_rate + (rand() / (float)RAND_MAX - 0.5f) * grain_life_spray * (double)sample_rate);
							life = std::clamp(life, 1, (int)sample_rate);
							for (int i = 0; i < grain_amount; i++)
							{
								if (!key.grains[i].active) {
									key.grains[i] = { pos, 0, life, true };
									break;
								}
							}
							key.grain_spawn_accumulator -= 1.0f / grain_spawn_rate_per_second;
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
							float max_level_duration = (float)g.life * grain_shape;
							float fade_in_duration = ((float)g.life - max_level_duration) * grain_tilt;
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

				//mix the sample and oscillator together
				//sample_in = sample_in * (1.0f - grain_mix) + grain_out * grain_mix;
				//in = in * (1.0f - sample_mix) + sample_in * sample_mix;

				float in_tot = osc_in * osc_amp + sample_in * sample_amp + grain_in * grain_amp;

				//apply filter and distortion
				float d = 1.0f + drive * 24.0f;
				if (drive_before_filter && drive_enabled) in_tot = tanhf(in_tot * d) / tanhf(d);
				float curr = filter(in_tot, lp_coeff, hp_coeff, key);
				if (invert) curr = in_tot - curr;
				if (!drive_before_filter && drive_enabled) curr = tanhf(curr * d) / tanhf(d);
				float output = curr * amp * key.level;
				out[i] += output;
				curr_out[curr_out_idx] += curr * amp * key.level;
				key.phase += step;
				key.phase2 += step2;
				if (key.phase >= 2.0f * PI) key.phase -= 2.0f * PI;
				if (key.phase2 >= 2.0f * PI) key.phase2 -= 2.0f * PI;
			}
		}
		for (int i = 0; i < frames; i++) {
			//apply delay
			if (delay_feedback > 0.001f) {
				int len = std::clamp((int)(delay_time * sample_rate), 1, delay_buffer_size);
				if (delay_buffer_pos >= len) delay_buffer_pos = 0;

				float delayed = delay_buffer[delay_buffer_pos];
				delay_buffer[delay_buffer_pos] = out[i] + delayed * delay_feedback;
				out[i] += delayed * delay_amp;

				delay_buffer_pos++;
			}

			curr_out[curr_out_idx] = out[i];
			curr_out_idx = (curr_out_idx + 1) % live_spectrum_window_size;
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
						key.sample_pos = sample_start * (float)sample.samples.size();
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
					key.sample_pos = sample_start * (float)sample.samples.size();
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