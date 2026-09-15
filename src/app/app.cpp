// app.cpp
#include "app.hpp"
#include "app_font.hpp"
#include "raygui.h"
#include "raymath.h"
#include "../audio/audio_player.hpp"
#include "../ui/graph.hpp"
#include "tinyfiledialogs.h"
#include <algorithm>

namespace resynth {
	void App::Unload(Sound& s, bool& has) { if (has) { UnloadSound(s); has = false; } }

	void App::PlayBuffer(Sound& s, bool& has, const std::vector<float>& buffer, int sample_rate) {
		Unload(s, has);
		if (buffer.empty()) return;
		s = MakeSoundFromBuffer(buffer, sample_rate);
		has = true;
		PlaySound(s);
	}

	void App::DrawTopBar() {
		int y = 45 - (int)scroll_offset;
		if (GuiButton(Rectangle{ 20, (float)y, 120, 30 }, "Browse...")) {
			const char* filter_patterns[2] = { "*.wav", "*.mp3" };
			const char* selected = tinyfd_openFileDialog(
				"Select Audio File",
				"",
				2, filter_patterns,
				"Audio files",
				0
			);
			if (selected != nullptr) {
				strncpy(audio_path, selected, sizeof(audio_path) - 1);
				audio_path[sizeof(audio_path) - 1] = '\0';
			}
		}

		GuiLabel(Rectangle{ 150, (float)y + 5, 400, 20 }, audio_path);

		if (GuiButton(Rectangle{ 560, (float)y, 100, 30 }, "Load")) {
			fourier.data.load(audio_path);
			fourier.use_wave_data = false;
		}
		if (GuiButton(Rectangle{ 670, (float)y, 160, 30 }, "Play Audio Sound"))
			PlayBuffer(sound_audio, has_audio, fourier.build_buffer_from_audio_data(), fourier.data.sample_rate);

		if (GuiButton(Rectangle{ 840, (float)y, 160, 30 }, "Stop All Sounds")) {
			if (has_audio) StopSound(sound_audio);
			if (has_curve) StopSound(sound_curve);
			if (has_waves) StopSound(sound_waves);
			if (has_chunks) StopSound(sound_chunks);
			if (has_live) StopSound(sound_live);
		}

		float window_size = (float)fourier.window_size;
		GuiSliderBar(Rectangle{ (float)1060, 50, 300, 20 }, "window size", TextFormat("%.1f", (float)fourier.window_size), &window_size, 2.0f, 22050);
		if (window_size_power_of_2) {
			int power = (int)round(log2(window_size));
			window_size = (float)pow(2, power);
		}
		fourier.window_size = (int)window_size;
		GuiCheckBox(Rectangle{ 1410, 50, 20, 20 }, "Window Size Power of 2", &window_size_power_of_2);

		int sample_rate = fourier.data.sample_rate > 0 ? fourier.data.sample_rate : 44100;
		float segment_duration = sample_rate > 0 ? (float)fourier.window_size / (float)sample_rate : 0.0f;
		int frequencies_per_segment = fourier.window_size / 2;
		float frequency_spacing = sample_rate > 0 ? (float)sample_rate / (float)fourier.window_size : 0.0f;

		DrawTextEx(g_app_font, TextFormat("Samples per segment: %d", fourier.window_size), { 1060, 75 }, 14, 1.0f, DARKGRAY);
		DrawTextEx(g_app_font, TextFormat("Segment duration: %.2f ms", segment_duration * 1000.0f), { 1060, 93 }, 14, 1.0f, DARKGRAY);
		DrawTextEx(g_app_font, TextFormat("Frequencies per segment: %d", frequencies_per_segment), { 1060, 111 }, 14, 1.0f, DARKGRAY);
		DrawTextEx(g_app_font, TextFormat("Frequency spacing: %.2f Hz", frequency_spacing), { 1060, 129 }, 14, 1.0f, DARKGRAY);
	}

	void App::DrawManualSection() {
		int y = 90 - (int)scroll_offset;
		int x = 80;

		GuiCheckBox(Rectangle{ 20, (float)y, 20, 20 }, "Manual Fourier Construction", &show_manual_section);
		if (!show_manual_section) return;
		y += 30;

		GuiCheckBox(Rectangle{ (float)x, (float)y, 20, 20 }, "Use Wave Data", &fourier.use_wave_data);
		y += 30;

		if (GuiButton(Rectangle{ (float)x, (float)y, 160, 30 }, "Construct Fourier"))
			fourier.compute_fourier_data(false);
		if (GuiButton(Rectangle{ (float)x + 190, (float)y, 160, 30 }, "Play Curve Sound"))
			PlayBuffer(sound_curve, has_curve, fourier.build_buffer_from_fourier_curve(), fourier.data.sample_rate);
		if (GuiButton(Rectangle{ (float)x + 380, (float)y, 160, 30 }, "Play Wave Sound"))
			PlayBuffer(sound_waves, has_waves, fourier.build_buffer_from_waves(), render_detail);

		y += 40;

		if (fourier.animate && fourier.constructed) {
			static bool forward = true;
			float highest = (float)fourier.window_size;
			fourier.test_frequency += (forward ? 1.0f : -1.0f) * fourier.animation_time * highest * GetFrameTime();
			if (fourier.test_frequency > highest) forward = false;
			if (fourier.test_frequency < 0.0f) forward = true;

			fourier.sphere_fourier_at_frequency(fourier.yf, fourier.xs, fourier.ys, fourier.test_frequency, fourier.window_size);
		}

		float wave_count = (float)fourier.highest_frequency_count;
		GuiSliderBar(Rectangle{ (float)x, (float)y, 300, 20 }, "waves amount", TextFormat("%d", fourier.highest_frequency_count), &wave_count, 1.0f, 1000.0f);
		fourier.highest_frequency_count = (int)wave_count;
		y += 40;

		if (GuiButton(Rectangle{ (float)x, (float)y, 260, 30 }, "Make Waves For Strongest Frequencies")) {
			fourier.waves.clear();
			for (int i = 0; i < fourier.highest_frequency_count && i < (int)fourier.peaks.size(); i++) {
				auto& peak = fourier.peaks[i];
				fourier.waves.push_back({ peak.freq, peak.magnitude, peak.phase });
			}
		}
		y += 60;

		if (!fourier.constructed) return;

		GraphSeries fourier_curve_series[] = {
			{ fourier.xf.data(), fourier.yf.data(), fourier.window_size, BLUE,     "Raw" },
			{ fourier.xf.data(), fourier.yh.data(), fourier.window_size, SKYBLUE,  "Smoothed" },
		};
		DrawGraph(fourier_curve_series, 2, Rectangle{ (float)x, (float)y, 500, 150 }, view_fourier_curve, "Fourier Curve");
		y += 200;

		GraphSeries strong_freq_series[] = {
			{ fourier.xc.data(),  fourier.yc.data(),  fourier.nyquist, MAROON, "Raw" },
			{ fourier.xc.data(), fourier.ych.data(), fourier.nyquist, ORANGE, "Smoothed" },
		};
		DrawGraph(strong_freq_series, 2, Rectangle{ (float)x, (float)y, 500, 250 }, view_strong_freq, "Strong Frequencies");
		y += 300;

		float max_freq = fourier.nyquist > 0 ? (float)fourier.nyquist : (float)fourier.window_size;
		max_freq = std::max(max_freq, 100.0f);
		GuiSliderBar(Rectangle{ (float)x, (float)y, 300, 20 }, "test freq", TextFormat("%.1f", fourier.test_frequency), &fourier.test_frequency, 0.0f, max_freq);
		y += 30;
		GuiCheckBox(Rectangle{ (float)x, (float)y, 20, 20 }, "Animate", &fourier.animate);
		y += 30;
		GuiSliderBar(Rectangle{ (float)x, (float)y, 300, 20 }, "anim speed", TextFormat("%.3f", fourier.animation_time), &fourier.animation_time, -0.01f, 0.01f);
		y += 50;

		Vector2 center = { 0, 0 };
		for (int i = 0; i < fourier.window_size; i++) { center.x += fourier.xs[i]; center.y += fourier.ys[i]; }
		center = Vector2Scale(center, 1.0f / fourier.window_size);

		GraphSeries sphere_series[] = {
			{ fourier.xs.data(), fourier.ys.data(), fourier.window_size, GREEN, "Raw"},
			{ &center.x,  &center.y,  1,      RED,   "Center", true },
		};
		DrawGraph(sphere_series, 2, Rectangle{ (float)x, (float)y, 500, 500 }, view_sphere, "Sphere Fourier");
		y += 550;

		float amplitude = sqrtf(center.x * center.x + center.y * center.y) * 2.0f;
		float phase = atan2f(center.y, center.x);
		DrawTextEx(g_app_font, TextFormat("amplitude: %.4f  phase: %.3f rad", amplitude, phase), { (float)x, (float)y }, 14, 1.0f, DARKGRAY);
		y += 100;

		GuiLabel(Rectangle{ (float)x, (float)y, 200, 20 }, TextFormat("Waves (%d)", (int)fourier.waves.size()));
		y += 45;
		for (int i = 0; i < (int)fourier.waves.size(); i++) {
			auto& w = fourier.waves[i];
			GuiSliderBar(Rectangle{ (float)x, (float)y, 200, 20 }, "freq", TextFormat("%.1f", w.frequency), &w.frequency, 0.0f, (float)fourier.nyquist);
			GuiSliderBar(Rectangle{ (float)x + 260, (float)y, 200, 20 }, "amp", TextFormat("%.2f", w.amplitude), &w.amplitude, 0.0f, 2.0f);
			GuiSliderBar(Rectangle{ (float)x + 520, (float)y, 200, 20 }, "phase", TextFormat("%.2f", w.phase), &w.phase, -PI, PI);
			if (GuiButton(Rectangle{ (float)x + 760, (float)y, 60, 20 }, "Remove")) { fourier.waves.erase(fourier.waves.begin() + i); i--; }
			y += 25;
		}
		if (GuiButton(Rectangle{ (float)x, (float)y, 120, 30 }, "Add Wave"))
			fourier.waves.push_back({ 1.0f, 1.0f, 0.0f });
	}

	void App::DrawChunkSection() {
		int y = 90 - (int)scroll_offset, x = 780;

		GuiCheckBox(Rectangle{ 780, (float)y, 20, 20 }, "Chunk Generation And Playback", &show_chunk_section);
		if (!show_chunk_section) return;
		y += 30;

		if (GuiButton(Rectangle{ (float)x, (float)y, 160, 30 }, "Construct Chunks")) {
			float freq_spacing = fourier.data.sample_rate > 0 ? (float)fourier.data.sample_rate / (float)fourier.window_size : 1.0f;
			int max_bins_for_5khz = (int)(5000.0f / freq_spacing);

			fourier.construct_chunks_from_audio_data();
			BuildSpectrogram(spectrogram, fourier.chunks.data(), (int)fourier.chunks.size(), 5000);
			BuildSpectrogram(spectrogram_no_smoothing, fourier.chunks.data(), (int)fourier.chunks.size(), 8000, false);
			//fourier.construct_chunks_from_audio_data();
			//BuildSpectrogram(spectrogram, fourier.chunks.data(), (int)fourier.chunks.size());
		}
		y += 60;

		float peaks_f = (float)fourier.max_peaks_per_chunk;
		GuiSliderBar(Rectangle{ (float)x, (float)y, 200, 20 }, "max peaks", TextFormat("%d", fourier.max_peaks_per_chunk), &peaks_f, 1.0f, 500.0f);
		fourier.max_peaks_per_chunk = (int)peaks_f;
		y += 50;
		if (fourier.chunks.empty()) return;

		if (GuiButton(Rectangle{ (float)x, (float)y, 160, 30 }, "Play Chunk Sound"))
			PlayBuffer(sound_chunks, has_chunks, fourier.build_buffer_from_chunks(), fourier.data.sample_rate);
		y += 60;

		float chunk_f = (float)selected_chunk;
		GuiSliderBar(Rectangle{ (float)x, (float)y, 480, 20 }, "chunk", TextFormat("%d / %d", selected_chunk, (int)fourier.chunks.size() - 1), &chunk_f, 0.0f, (float)(fourier.chunks.size() - 1));
		selected_chunk = (int)chunk_f;
		y += 50;

		auto& c = fourier.chunks[selected_chunk];
		GuiLabel(Rectangle{ (float)x, (float)y, 300, 20 }, TextFormat("Time offset: %.3fs", c.time_offset));
		y += 20;

		if (GuiButton(Rectangle{ (float)x, (float)y, 220, 30 }, live_playing ? "Stop Live Playback" : "Start Live Playback")) {
			live_playing = !live_playing;
			if (!live_playing) { Unload(sound_live, has_live); last_live_chunk = -1; }
		}
		y += 60;

		if (live_playing && selected_chunk != last_live_chunk) {
			PlayBuffer(sound_live, has_live, fourier.build_buffer_from_single_chunk(c, 20), fourier.data.sample_rate);
			last_live_chunk = selected_chunk;
		}


		GraphSeries chunk_curve_series[] = {
			{ c.xf.data(), c.yf.data(), (int)c.yf.size(), BLUE,  "Raw" },
			{ c.xf.data(), c.yh.data(), (int)c.yh.size(), SKYBLUE, "Smoothed" },
			{ c.xf.data(), c.yr.data(), (int)c.yr.size(), GREEN, "Reconstructed" },
		};
		DrawGraph(chunk_curve_series, 3, Rectangle{ (float)x, (float)y, 480, 150 }, view_chunk_curve, "Chunk Fourier Curve");
		y += 200;

		GraphSeries chunk_reconstructed_series[] = {
			{ c.xc.data(), c.yc.data(), (int)c.yc.size(), GREEN, "Raw" },
			{ c.xc.data(), c.ych.data(), (int)c.ych.size(), SKYBLUE, "Reconstructed" },
			{ c.xcr.data(), c.ycr.data(), (int)c.ycr.size(), ORANGE, "Spikes" },
		};
		DrawGraph(chunk_reconstructed_series, 3, Rectangle{ (float)x, (float)y, 480, 150 }, view_chunk_reconstructed, "Chunk Reconstructed Spectrum");
		y += 200;

		GuiCheckBox(Rectangle{ (float)x, (float)y, 20, 20 }, "Edit Spectrogram", &brush.enabled);
		if (brush.enabled) {
			bool select = brush.tool == SpectrogramTool::Select;
			if (GuiButton(Rectangle{ (float)x + 200, (float)y, 110, 20 }, select ? "Mode: Select" : "Mode: Brush")) {
				brush.tool = select ? SpectrogramTool::Brush : SpectrogramTool::Select;
				brush.dragging = brush.stroking = false;
			}
			GuiSliderBar(Rectangle{ (float)x + 350, (float)y, 100, 20 }, "str",
				TextFormat("%.2f", brush.strength), &brush.strength, 0.0f, 1.0f);
			y += 26;
			GuiCheckBox(Rectangle{ (float)x, (float)y, 20, 20 }, "Soft", &brush.soft);
		}
		y += 30;

		bool show_orig = IsKeyDown(KEY_LEFT_SHIFT);
		spectrogram.show_original = show_orig;
		spectrogram_no_smoothing.show_original = show_orig;

		Rectangle spec_bounds = { (float)x, (float)y, 480, 750 };
		DrawSpectrogram(spectrogram, spec_bounds, "Spectrogram");
		int t_lo, t_hi;
		if (EditSpectrogram(spectrogram, spec_bounds, fourier.chunks.data(),
			(int)fourier.chunks.size(), brush, t_lo, t_hi)) {
			edit_t_lo = edit_t_lo < 0 ? t_lo : std::min(edit_t_lo, t_lo);
			edit_t_hi = std::max(edit_t_hi, t_hi);
		}
		y += 800;

		DrawSpectrogram(spectrogram_no_smoothing, Rectangle{ (float)x, (float)y, 480, 250 },
			"Spectrogram without Hann smoothing");

		// re-derive once the stroke ends, not every frame
		if (edit_t_lo >= 0 && !brush.stroking) {
			for (int ci = edit_t_lo; ci <= edit_t_hi; ci++) fourier.rederive_chunk_peaks(fourier.chunks[ci]);
			edit_t_lo = edit_t_hi = -1;
		}
	}

	void App::Update() {
		ResetGraphWheelConsumption();

		BeginDrawing();
		ClearBackground(RAYWHITE);
		DrawTopBar();
		DrawManualSection();
		DrawChunkSection();
		EndDrawing();

		if (!WasGraphWheelConsumed()) {
			scroll_offset -= GetMouseWheelMove() * 40.0f;
			scroll_offset = std::max(scroll_offset, 0.0f);
		}
	}

}  // namespace resynth