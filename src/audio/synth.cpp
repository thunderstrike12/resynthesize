// synth/synth.cpp
#include "synth.hpp"
#include <cmath>
#include <algorithm>

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
		stream = LoadAudioStream(sample_rate, 32, 1);   // 32-bit float, mono
		SetAudioStreamCallback(stream, StreamCallback);
		PlayAudioStream(stream);
		running = true;

		for (int i = 0; i < 88; i++)
			keys[i].frequency = base_frequency * powf(key_ratio, (float)i);
	}

	void Synth::Shutdown() {
		if (!running) return;
		running = false;
		g_synth = nullptr;            // before unload, so a late callback is a no-op
		StopAudioStream(stream);
		UnloadAudioStream(stream);
	}

	float Synth::oscillate(float phase)
	{
		float t = phase / (2.0f * PI);
		t -= floorf(t);
		float value = 0.0f;
		switch (wave_shape)
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
			level = sustain_level;
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
		key.hp_last = key.hp_last + (in - key.hp_last) * hp_coeff;
		float hp_out = in - key.hp_last;

		key.lp_last = key.lp_last + (hp_out - key.lp_last) * lp_coeff;
		return key.lp_last;
	}

	void Synth::Render(float* out, int frames) {
		for (int i = 0; i < frames; i++) out[i] = 0.0f;
		for (auto& key : keys) {
			if (key.stage == IDLE) continue;
			const float step = 2.0f * PI * key.frequency / (float)sample_rate;
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
				envelope(key.stage, key.level, attack_step, decay_step, sustain, release_step);

				envelope(key.lp_cutoff_stage, key.lp_cutoff_level, lp_attack_step, lp_decay_step, lp_sustain, lp_release_step);
				envelope(key.hp_cutoff_stage, key.hp_cutoff_level, hp_attack_step, hp_decay_step, hp_sustain, hp_release_step);
				float lp_cutoff_curr = std::clamp(lp_cutoff + lp_env_amount * key.lp_cutoff_level, 0.0f, 1.0f);
				float hp_cutoff_curr = std::clamp(hp_cutoff + hp_env_amount * key.hp_cutoff_level, 0.0f, 1.0f);
				dbg_lp = lp_cutoff_curr, dbg_hp = hp_cutoff_curr;
				lp_cutoff_curr = 20 * powf(1000, lp_cutoff_curr);
				hp_cutoff_curr = 20 * powf(1000, hp_cutoff_curr);

				float lp_coeff = 1.f - expf(-2.0f * PI * lp_cutoff_curr / sample_rate);
				float hp_coeff = 1.f - expf(-2.0f * PI * hp_cutoff_curr / sample_rate);


				float in = oscillate(key.phase);
				float curr = filter(in, lp_coeff, hp_coeff, key);
				if (invert) curr = in - curr;
				out[i] += curr * amp * key.level;
				key.phase += step;
				if (key.phase >= 2.0f * PI) key.phase -= 2.0f * PI;
			}
		}
	}
	void Synth::Update() {
		//new hit
		for (auto& key : keys) {
			if (key.is_down && !key.was_down) {
				key.stage = ATTACK;
				key.lp_cutoff_stage = ATTACK;
				key.hp_cutoff_stage = ATTACK;
				key.lp_cutoff_level = 0.0f;
				key.hp_cutoff_level = 0.0f;
				key.was_down = true;

			}
			if (!key.is_down && key.was_down) {
				key.stage = RELEASE;
				key.lp_cutoff_stage = RELEASE;
				key.hp_cutoff_stage = RELEASE;
				key.was_down = false;
			}
		}
	}
	void Synth::fill_preview_graph(int resolution, int cycles) {
		//float coeff = 1.f - expf(-2.0f * PI * cutoff / sample_rate);
		if (resolution < 2) return;
		x.resize(resolution); y.resize(resolution);
		float last = 0.0f;
		for (int i = 0; i < resolution; i++) {
			float u = (float)i / (float)(resolution - 1);    // 0..1 across the graph
			x[i] = u;

			float in = oscillate(u * (float)cycles * 2.0f * PI);
			float new_last = 0.0f;
			//float curr = filter(in, last, coeff, new_last);
			//float curr = last + (in - last) * coeff;
			last = new_last;
			y[i] = /*curr*/ in;
		}
	}

}  // namespace resynth