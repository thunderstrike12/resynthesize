// app.cpp
#include "app.hpp"
#include "app_font.hpp"
#include "raygui.h"
#include "raymath.h"
#include "../audio/audio_player.hpp"
#include "../ui/graph.hpp"
#include "../ui/ui_helper.hpp"
#include "tinyfiledialogs.h"
#include <algorithm>
#include <iostream>

namespace {
	// A0 is index 0, so i % 12 gives A A# B C C# D D# E F F# G G#
	inline bool IsBlackKey(int i) {
		int s = i % 12;
		return s == 1 || s == 4 || s == 6 || s == 9 || s == 11;
	}
	const char* kNoteNames[12] = { "A","A#","B","C","C#","D","D#","E","F","F#","G","G#" };

	// octave rolls over at C, which sits 3 semitones above A0
	inline int OctaveOf(int i) { return (i + 9) / 12; }

	const int kKeyCodes[] = {
		KEY_Z, KEY_S, KEY_X, KEY_D, KEY_C, KEY_V, KEY_G,
		KEY_B, KEY_H, KEY_N, KEY_J, KEY_M, KEY_COMMA, KEY_L, KEY_PERIOD,
		KEY_Q, KEY_TWO, KEY_W, KEY_THREE, KEY_E, KEY_R, KEY_FIVE,
		KEY_T, KEY_SIX, KEY_Y, KEY_SEVEN, KEY_U
	};
	const char* kKeyNames[] = {
		"Z","S","X","D","C","V","G","B","H","N","J","M",",","L",".",
		"Q","2","W","3","E","R","5","T","6","Y","7","U"
	};
	constexpr int kKeyCodeCount = (int)(sizeof(kKeyCodes) / sizeof(kKeyCodes[0]));

	inline float BandX(Rectangle r, float u) { return r.x + u * r.width; }

	void DrawFilterBand(Rectangle r, float lp_u, float hp_u, bool invert, float lp_curr, float hp_curr) {
		DrawRectangleRec(r, Color{ 252, 252, 254, 255 });
		const float marks[3] = { 100.0f, 1000.0f, 10000.0f };
		const char* labels[3] = { "100", "1k", "10k" };
		for (int i = 0; i < 3; i++) {
			float u = logf(marks[i] / 20.0f) / logf(1000.0f);
			float x = BandX(r, u);
			DrawLine((int)x, (int)r.y, (int)x, (int)(r.y + r.height), Color{ 232, 232, 238, 255 });
			DrawTextEx(resynth::g_app_font, labels[i], { x + 3, r.y + r.height - 15 }, 11, 1.0f,
				Color{ 175, 175, 185, 255 });
		}

		float lx = BandX(r, lp_u), hx = BandX(r, hp_u);
		if (invert) {
			if (lx > hx) {
				DrawRectangleRec(Rectangle{ r.x, r.y, hx - r.x, r.height }, Color{ 190, 220, 248, 255 });
				DrawRectangleRec(Rectangle{ lx, r.y, r.width - (lx - r.x) , r.height }, Color{ 190, 220, 248, 255 });
			}
		}
		else {
			if (lx > hx)
				DrawRectangleRec(Rectangle{ hx, r.y, lx - hx, r.height }, Color{ 190, 220, 248, 255 });
		}
		DrawLineEx({ hx, r.y }, { hx, r.y + r.height }, 2.0f, Color{ 190, 90, 60, 255 });
		DrawLineEx({ lx, r.y }, { lx, r.y + r.height }, 2.0f, Color{ 60, 120, 190, 255 });

		lx = BandX(r, lp_curr), hx = BandX(r, hp_curr);
		DrawLineEx({ hx, r.y }, { hx, r.y + r.height }, 2.0f, Color{ 190, 90, 60, 255 });
		DrawLineEx({ lx, r.y }, { lx, r.y + r.height }, 2.0f, Color{ 60, 120, 190, 255 });

		DrawRectangleLinesEx(r, 1, Color{ 200, 200, 208, 255 });
	}

	inline float GrainEnvAt(float u, float shape, float tilt) {
		float max_dur = shape;
		float fade_in = (1.0f - max_dur) * tilt;
		float fade_out = 1.0f - max_dur - fade_in;

		if (fade_in > 0.0f && u < fade_in)              return u / fade_in;
		if (fade_out > 0.0f && u > fade_in + max_dur)   return 1.0f - (u - fade_in - max_dur) / fade_out;
		return 1.0f;
	}

	void DrawGrainEnv(Rectangle r, float shape, float tilt, float rate, float life) {
		DrawRectangleRec(r, Color{ 252, 252, 254, 255 });

		BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);

		const float span = 0.6f;
		const float px_per_s = r.width / span;
		const float life_w = life * px_per_s;
		const float step_w = (rate > 0.0f) ? px_per_s / rate : r.width;

		const int count = (int)ceilf((r.width + life_w) / std::max(step_w, 1.0f));

		for (int g = 0; g < count; g++) {
			float x0 = r.x + g * step_w;
			if (x0 > r.x + r.width) break;

			Vector2 prev{};
			for (int i = 0; i <= 32; i++) {
				float u = (float)i / 32.0f;
				float gain = GrainEnvAt(u, shape, tilt);
				Vector2 p = { x0 + u * life_w,
							  r.y + r.height - gain * (r.height - 8) - 4 };
				if (i > 0) DrawLineEx(prev, p, 1.5f, Color{ 90, 190, 150, 255 });
				prev = p;
			}
		}

		EndScissorMode();
		DrawRectangleLinesEx(r, 1, Color{ 200, 200, 208, 255 });
	}
}

namespace resynth {
	void App::DrawViewTabs() {
		if (GuiButton(Rectangle{ 20, 5, 120, 30 }, "Analysis")) view = View::Analysis;
		if (GuiButton(Rectangle{ 150, 5, 120, 30 }, "Synth"))    view = View::Synth;
		if (view == View::Synth) {
			GuiSliderBar(Rectangle{ 380, 5, 200, 30 }, "master volume",
				TextFormat("%.0f%%", synth.params[P_AMPLITUDE].base * 100.0f),
				&synth.params[P_AMPLITUDE].base, 0.0f, 2.0f);
		}
	}

	// A slider bound to a Param. Draws the base value normally, overlays the
	// modulation range as a band, and a live marker at the current value.
	// Right-click opens the routing popup for this parameter.
	void App::ModSlider(Rectangle r, const char* label, const char* fmt, ParamId id, float display_scale) {
		Param& p = synth.params[id];
		Vector2 mouse = GetMousePosition();

		float swing = 0.0f;
		for (int i = 0; i < max_routings; i++) {
			const Routing& rt = synth.routings[i];
			if (rt.lfo >= 0 && rt.param == (int)id) swing += fabsf(rt.depth);
		}
		swing = std::min(swing, 1.0f);

		const char* txt;
		if (strcmp(fmt, "hz") == 0)
			txt = TextFormat("%.0f Hz", 20.0f * powf(1000.0f, p.base));
		else
			txt = TextFormat(fmt, p.base * display_scale);

		float v = p.base;
		GuiSliderBar(r, label, TextFormat(txt, p.base), &v, p.lo, p.hi);
		p.base = v;

		if (swing > 0.0001f) {
			const float span = p.hi - p.lo;
			auto to_x = [&](float val) {
				return r.x + std::clamp((val - p.lo) / span, 0.0f, 1.0f) * r.width;
				};

			// band showing the reachable range
			float x0 = to_x(p.base - swing * span);
			float x1 = to_x(p.base + swing * span);
			DrawRectangleRec(Rectangle{ x0, r.y + r.height - 4, x1 - x0, 3 },
				Color{ 235, 140, 60, 170 });

			// live marker at the modulated value
			float xc = to_x(p.current);
			DrawRectangleRec(Rectangle{ xc - 1.0f, r.y, 2.0f, r.height },
				Color{ 235, 140, 60, 255 });
		}

		if (CheckCollisionPointRec(mouse, r) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
			popup_param = (int)id;
			popup_pos = { r.x, r.y + r.height + 4 };
			mouse_up_since_popup = false;
		}
	}

	void App::DrawModPopup() {
		if (popup_param < 0) return;

		const float w = 500.0f, row = 28.0f;
		const float h = 42.0f + max_lfos * row;
		Rectangle box = { popup_pos.x + 10, popup_pos.y + 10, w, h };

		// keep it on screen
		box.x = std::min(box.x, (float)GetScreenWidth() - w - 8);
		box.y = std::min(box.y, (float)GetScreenHeight() - h - 8);

		Vector2 mouse = GetMousePosition();
		bool inside = CheckCollisionPointRec(mouse, box);

		if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) mouse_up_since_popup = true;

		/// click outside closes
		if ((!inside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
			IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) && mouse_up_since_popup) {
			popup_param = -1;
			return;
		}
		if (IsKeyPressed(KEY_SPACE)) { popup_param = -1; return; }

		DrawRectangleRec(box, Color{ 250, 250, 252, 255 });
		DrawRectangleLinesEx(box, 1, Color{ 160, 160, 170, 255 });

		DrawTextEx(g_app_font, synth.params[popup_param].name,
			{ box.x + 12, box.y + 10 }, 15, 1.0f, DARKGRAY);

		for (int l = 0; l < max_lfos; l++) {
			float y = box.y + 36 + l * row;

			int idx = synth.find_routing(l, popup_param);

			DrawTextEx(resynth::g_app_font, TextFormat("LFO %i", l + 1), { box.x + 12, y }, 11, 1.0f,
				Color{ 175, 175, 185, 255 });

			if (idx < 0) {
				if (GuiButton(Rectangle{ box.x + 62, y, 200, 18 }, "Make Connection")) {
					synth.set_routing(l, popup_param, 0.5f);
					idx = synth.find_routing(l, popup_param);
				}
			}
			else {
				if (GuiButton(Rectangle{ box.x + 62, y, 200, 18 }, "Delete Connection")) {
					synth.set_routing(l, popup_param, 0.0f);
					idx = synth.find_routing(l, popup_param);
				}
			}

			if (idx >= 0) {
				float d = synth.routings[idx].depth;
				GuiSliderBar(Rectangle{ box.x + 330, y, 120, 18 }, "",
					TextFormat("%+.2f", d), &d, -1.0f, 1.0f);
				synth.routings[idx].depth = d;
			}
		}
	}

	void App::DrawSynthSection() {
		const float py = 60.0f, ph = 250.0f;
		const float py2 = 326.0f, ph2 = 250.0f;
		const float py3 = 592.0f, ph3 = 250.0f;
		const float py4 = 858.0f, ph4 = 250.0f;
		const float py5 = 1124.0f, ph5 = 250.0f;

		auto Panel = [&](float x, float y, float w, float h, const char* title) {
			DrawRectangleRec(Rectangle{ x, y, w, h }, Color{ 248, 248, 250, 255 });
			DrawRectangleLinesEx(Rectangle{ x, y, w, h }, 1, Color{ 200, 200, 208, 255 });
			DrawTextEx(g_app_font, title, { x + 16, y + 12 }, 16, 1.0f, DARKGRAY);
			};

		auto EnvSliders = [&](float x, float w, float y,
			float& a, float& d, float& s, float& r) {
				const float sx = x + 86, sw = w - 150;
				GuiSliderBar(Rectangle{ sx, y, sw, 20 }, "attack",
					TextFormat("%.0f ms", a * 1000.0f), &a, 0.0f, 1.0f);  y += 32;
				GuiSliderBar(Rectangle{ sx, y, sw, 20 }, "decay",
					TextFormat("%.0f ms", d * 1000.0f), &d, 0.0f, 1.0f);  y += 32;
				GuiSliderBar(Rectangle{ sx, y, sw, 20 }, "sustain",
					TextFormat("%.2f", s), &s, 0.0f, 1.0f);               y += 32;
				GuiSliderBar(Rectangle{ sx, y, sw, 20 }, "release",
					TextFormat("%.0f ms", r * 1000.0f), &r, 0.0f, 1.0f);  y += 32;
				return y;
			};

		Vector2 mouse = GetMousePosition();

		auto ShapeRow = [&](float x, float y, WaveShape& target) {
			const WaveShape shapes[4] = { SINE, SAW, SQUARE, TRIANGLE };
			const char* names[4] = { "sine", "saw", "square", "tri" };
			for (int i = 0; i < 4; i++) {
				Rectangle r = { x + (float)i * 110, y, 104, 40 };
				bool active = target == shapes[i];
				bool hover = CheckCollisionPointRec(mouse, r);
				if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) target = shapes[i];

				DrawRectangleRec(r, active ? Color{ 225, 238, 252, 255 }
				: (hover ? Color{ 244, 244, 248, 255 } : Color{ 252, 252, 254, 255 }));
				DrawRectangleLinesEx(r, active ? 2.0f : 1.0f,
					active ? Color{ 60, 120, 190, 255 } : Color{ 200, 200, 208, 255 });

				Vector2 ns = MeasureTextEx(g_app_font, names[i], 13, 1.0f);
				DrawTextEx(g_app_font, names[i],
					{ r.x + (104 - ns.x) * 0.5f, r.y + 13 }, 13, 1.0f,
					active ? Color{ 40, 100, 175, 255 } : Color{ 130, 130, 140, 255 });
			}
			};

		// ---- oscillators ------------------------------------------------
		const float osx = 20.0f, osw = 560.0f;
		Panel(osx, py, osw, ph, "Oscillators");
		{
			const float sx = osx + 86, sw = 300;
			float sy = py + 42;

			DrawTextEx(g_app_font, "osc 1", { osx + 20, py + 52 }, 14, 1.0f, DARKGRAY);
			ShapeRow(osx + 76, sy, synth.wave_shape);
			sy += 48;

			DrawTextEx(g_app_font, "osc 2", { osx + 20, sy + 10 }, 14, 1.0f, DARKGRAY);
			ShapeRow(osx + 76, sy, synth.wave_shape2);
			sy += 54;

			ModSlider(Rectangle{ sx, sy, sw, 20 }, "stereo", "%.0f%%", P_OSC_STEREO, 100.0f);  sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "amp", "%.0f%%", P_OSC_AMP, 100.0f);  sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "mix", "%.0f%%", P_OSC_MIX, 100.0f);  sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "detune", "%.1f ct", P_DETUNE);          sy += 28;

			GuiCheckBox(Rectangle{ sx, sy, 20, 20 }, "slide", &synth.slide);
			GuiSliderBar(Rectangle{ sx + 140, sy, 160, 20 }, "speed",
				TextFormat("%.2f", synth.slide_speed), &synth.slide_speed, 0.0f, 1.0f);
		}

		// ---- amp envelope -----------------------------------------------
		const float ax = 596.0f, aw = 400.0f;
		Panel(ax, py, aw, ph, "Amp Envelope");
		EnvSliders(ax, aw, py + 52, synth.attack, synth.decay, synth.sustain, synth.release);

		// ---- filter -----------------------------------------------------
		const float fx = 1012.0f, fw = 440.0f;
		Panel(fx, py, fw, ph, "Filter");
		{
			DrawFilterBand(Rectangle{ fx + 20, py + 40, fw - 40, 44 },
				synth.params[P_LP_CUTOFF].current, synth.params[P_HP_CUTOFF].current,
				synth.invert, synth.dbg_lp, synth.dbg_hp);

			const float sx = fx + 86, sw = 280;
			float sy = py + 96;

			ModSlider(Rectangle{ sx, sy, sw, 20 }, "low cut", "hz", P_HP_CUTOFF);   sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "high cut", "hz", P_LP_CUTOFF);   sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "resonance", "%.2f", P_RESONANCE); sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "drive", "%.0f%%", P_DRIVE, 100.0f);
			GuiCheckBox(Rectangle{ fx + 56, sy, 20, 20 }, "", &synth.drive_enabled);
			sy += 30;

			GuiCheckBox(Rectangle{ sx, sy, 20, 20 }, "invert", &synth.invert);
			float poles_f = (float)synth.poles;
			GuiSliderBar(Rectangle{ sx + 150, sy, 130, 20 }, "poles",
				TextFormat("%d", synth.poles), &poles_f, 1.0f, 4.0f);
			synth.poles = std::clamp((int)poles_f, 1, 4);
			sy += 28;

			GuiCheckBox(Rectangle{ sx, sy, 20, 20 }, "drive pre-filter",
				&synth.drive_before_filter);
		}

		// ---- waveform preview -------------------------------------------
		const float vx = 1468.0f, vw = 430.0f;
		Panel(vx, py, vw, ph, "Waveform");
		{
			synth.fill_preview_graph(512, 3);
			GraphSeries preview_series[] = {
				{ synth.x.data(), synth.y.data(), (int)synth.x.size(), SKYBLUE, nullptr },
			};
			DrawGraph(preview_series, 1,
				Rectangle{ vx + 70, py + 56, vw - 100, ph - 110 }, view_synth_preview);
		}

		// ---- filter envelopes -------------------------------------------
		Panel(20.0f, py2, 400.0f, ph2, "Low-cut Envelope");
		{
			float y = EnvSliders(20.0f, 400.0f, py2 + 52,
				synth.hp_attack, synth.hp_decay, synth.hp_sustain, synth.hp_release);
			GuiSliderBar(Rectangle{ 20.0f + 86, y + 8, 250, 20 }, "amount",
				TextFormat("%+.2f", synth.hp_env_amount), &synth.hp_env_amount, -1.0f, 1.0f);
		}

		Panel(436.0f, py2, 400.0f, ph2, "High-cut Envelope");
		{
			float y = EnvSliders(436.0f, 400.0f, py2 + 52,
				synth.lp_attack, synth.lp_decay, synth.lp_sustain, synth.lp_release);
			GuiSliderBar(Rectangle{ 436.0f + 86, y + 8, 250, 20 }, "amount",
				TextFormat("%+.2f", synth.lp_env_amount), &synth.lp_env_amount, -1.0f, 1.0f);
		}

		// ---- spectrum + filter response ---------------------------------
		const float spx = 852.0f, spw = 1046.0f;
		Panel(spx, py2, spw, ph2, "Output Spectrum");
		{
			const int points = 200;
			std::vector<float> fxs(points), fys(points);

			float lp_hz = 20.0f * powf(1000.0f, synth.params[P_LP_CUTOFF].current);
			float hp_hz = 20.0f * powf(1000.0f, synth.params[P_HP_CUTOFF].current);
			float lp_coeff = 1.0f - expf(-2.0f * PI * lp_hz / synth.sample_rate);
			float hp_coeff = 1.0f - expf(-2.0f * PI * hp_hz / synth.sample_rate);

			for (int p = 0; p < points; p++) {
				float u = (float)p / (float)(points - 1);
				float hz = 20.0f * powf(1000.0f, u);

				Key probe;
				float step = 2.0f * PI * hz / (float)synth.sample_rate;
				float phase = 0.0f, peak = 0.0f;

				int cycle = (int)(synth.sample_rate / hz) + 1;
				int settle = std::max(2000, cycle * 4);
				int measure = cycle * 2;

				for (int i = 0; i < settle + measure; i++) {
					float in = sinf(phase);
					float out = synth.filter(in, lp_coeff, hp_coeff, probe);
					if (synth.invert) out = in - out;
					if (i >= settle) peak = std::max(peak, fabsf(out));
					phase += step;
					if (phase >= 2.0f * PI) phase -= 2.0f * PI;
				}

				fxs[p] = log10f(hz);
				fys[p] = peak/* * synth.params[P_OSC_AMP].current*/;
			}

			synth.fill_live_spectrum();
			std::vector<float> sxs(synth.live_spectrum.xc.size());
			for (size_t i = 0; i < sxs.size(); i++)
				sxs[i] = log10f(std::max(synth.live_spectrum.xc[i], 20.0f));

			GraphSeries spectrum_series[] = {
				{ sxs.data(), synth.live_spectrum.yc.data(),
				  (int)sxs.size(), MAROON, "Output" },
				{ fxs.data(), fys.data(), points, SKYBLUE, "Filter" }
			};
			DrawGraph(spectrum_series, 2,
				Rectangle{ spx + 70, py2 + 56, spw - 100, ph2 - 110 },
				view_synth_spectrum, "log10 Hz");
		}

		// ---- sampler ----------------------------------------------------
		const float smx = 20.0f, smw = 560.0f;
		Panel(smx, py3, smw, ph3, "Sampler");
		{
			if (GuiButton(Rectangle{ smx + 20, py3 + 40, 120, 26 }, "Load Sample...")) {
				const char* filter_patterns[2] = { "*.wav", "*.mp3" };
				const char* selected = tinyfd_openFileDialog(
					"Select Sample", "", 2, filter_patterns, "Audio files", 0);
				if (selected != nullptr) {
					strncpy(sample_path, selected, sizeof(sample_path) - 1);
					sample_path[sizeof(sample_path) - 1] = '\0';
					synth.sample.load(sample_path);
				}
			}
			if (GuiButton(Rectangle{ smx + 420, py3 + 40, 120, 26 }, "Load into keys"))
				synth.load_sample_into_keys();

			const char* shown = "no sample loaded";
			if (!synth.sample.samples.empty()) {
				shown = sample_path;
				for (const char* p = sample_path; *p; p++)
					if (*p == '\\' || *p == '/') shown = p + 1;
			}
			DrawTextEx(g_app_font, shown, { smx + 152, py3 + 40 }, 13, 1.0f,
				synth.sample.samples.empty() ? Color{ 170,170,180,255 } : DARKGRAY);

			if (!synth.sample.samples.empty()) {
				float secs = (float)synth.sample.samples.size()
					/ (float)std::max(synth.sample.sample_rate, 1);
				DrawTextEx(g_app_font,
					TextFormat("%.2f s   %d Hz", secs, synth.sample.sample_rate),
					{ smx + 152, py3 + 56 }, 12, 1.0f, Color{ 150, 150, 160, 255 });
			}

			const float c1 = smx + 92, c2 = smx + 330, w = 150;
			float sy = py3 + 80;

			ModSlider(Rectangle{ c1, sy, w, 20 }, "stereo", "%.0f%%", P_SAMPLE_STEREO, 100.0f);		sy += 28;
			ModSlider(Rectangle{ c1, sy, w, 20 }, "amp", "%.0f%%", P_SAMPLE_AMP, 100.0f);
			{
				float root_f = (float)synth.sample_root_key;
				GuiSliderBar(Rectangle{ c2, sy, w, 20 }, "root",
					TextFormat("%s%d", kNoteNames[synth.sample_root_key % 12],
						OctaveOf(synth.sample_root_key)),
					&root_f, 0.0f, 87.0f);
				synth.sample_root_key = std::clamp((int)root_f, 0, 87);
			}
			sy += 28;

			ModSlider(Rectangle{ c1, sy, w, 20 }, "start", "%.0f%%", P_SAMPLE_START, 100.0f);
			GuiSliderBar(Rectangle{ c2, sy, w, 20 }, "length",
				TextFormat("%.0f%%", synth.sample_length * 100.0f),
				&synth.sample_length, 0.01f, 1.0f);
			sy += 32;

			GuiCheckBox(Rectangle{ c1, sy, 20, 20 }, "loop", &synth.sample_loop);
			sy += 28;

			if (synth.sample_loop) {
				ModSlider(Rectangle{ c1, sy, w, 20 }, "loop from", "%.0f%%",
					P_SAMPLE_LOOP_START, 100.0f);
				GuiSliderBar(Rectangle{ c2, sy, w, 20 }, "fade",
					TextFormat("%.0f%%", synth.sample_loop_fade_length * 100.0f),
					&synth.sample_loop_fade_length, 0.0f, 0.5f);
			}
		}

		// ---- granular ---------------------------------------------------
		const float grx = 596.0f, grw = 560.0f;
		Panel(grx, py3, grw, ph3, "Granular");
		{
			const float c1 = grx + 92, c2 = grx + 330, w = 150;
			float sy = py3 + 44;

			ModSlider(Rectangle{ c2, sy, w, 20 }, "stereo", "%.2f r", P_GRAIN_STEREO);
			sy += 28;

			ModSlider(Rectangle{ c1, sy, w, 20 }, "amp", "%.0f%%", P_GRAIN_AMP, 100.0f);
			ModSlider(Rectangle{ c2, sy, w, 20 }, "rate", "%.2f g/s", P_GRAIN_RATE);
			sy += 28;

			ModSlider(Rectangle{ c1, sy, w, 20 }, "position", "%.0f%%", P_GRAIN_POS, 100.0f);
			ModSlider(Rectangle{ c2, sy, w, 20 }, "spray", "%.0f%%", P_GRAIN_POS_SPRAY, 100.0f);
			sy += 28;

			ModSlider(Rectangle{ c1, sy, w, 20 }, "life", "%.0f ms", P_GRAIN_LIFE, 1000.0f);
			ModSlider(Rectangle{ c2, sy, w, 20 }, "spray", "%.0f%%", P_GRAIN_LIFE_SPRAY, 100.0f);
			sy += 28;

			ModSlider(Rectangle{ c1, sy, w, 20 }, "shape", "%.2f", P_GRAIN_SHAPE);
			ModSlider(Rectangle{ c2, sy, w, 20 }, "tilt", "%.2f", P_GRAIN_TILT);
			sy += 30;

			int overlap = (int)ceilf(synth.params[P_GRAIN_RATE].current
				* synth.params[P_GRAIN_LIFE].current);
			bool over = overlap > grain_amount;
			DrawTextEx(g_app_font,
				TextFormat("~%d overlapping  (pool %d)", overlap, grain_amount),
				{ grx + 20, sy + 14 }, 13, 1.0f,
				over ? MAROON : Color{ 150, 150, 160, 255 });

			DrawGrainEnv(Rectangle{ grx + 190, sy, grw - 210, 44 },
				synth.params[P_GRAIN_SHAPE].current, synth.params[P_GRAIN_TILT].current,
				synth.params[P_GRAIN_RATE].current, synth.params[P_GRAIN_LIFE].current);
		}

		// ---- delay ------------------------------------------------------
		const float dlx = 1172.0f, dlw = 340.0f;
		Panel(dlx, py3, dlw, ph3, "Delay");
		{
			const float sx = dlx + 100, sw = 200;
			float sy = py3 + 48;

			ModSlider(Rectangle{ sx, sy, sw, 20 }, "stereo", "%.0f%%", P_DELAY_STEREO, 100.0f);      sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "amp", "%.0f%%", P_DELAY_AMP, 100.0f);      sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "time", "%.0f ms", P_DELAY_TIME, 1000.0f);   sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "feedback", "%.0f%%", P_DELAY_FEEDBACK, 100.0f); sy += 34;

			float fb = synth.params[P_DELAY_FEEDBACK].current;
			if (fb > 0.001f) {
				float repeats = logf(0.001f) / logf(std::max(fb, 0.001f));
				DrawTextEx(g_app_font,
					TextFormat("~%.0f repeats   tail %.1f s",
						repeats, repeats * synth.params[P_DELAY_TIME].current),
					{ dlx + 24, sy }, 13, 1.0f, Color{ 150, 150, 160, 255 });
			}
			sy += 20;

			if (synth.params[P_DELAY_TIME].current * synth.sample_rate > (float)delay_buffer_size) {
				DrawTextEx(g_app_font,
					TextFormat("buffer holds %.2f s max",
						(float)delay_buffer_size / (float)synth.sample_rate),
					{ dlx + 24, sy }, 13, 1.0f, MAROON);
			}
			sy += 20;

			ModSlider(Rectangle{ sx, sy, sw, 20 }, "amp", "%.0f%%", P_REVERB_AMP, 100.0f); sy += 28;
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "damp", "%.0f%%", P_REVERB_DAMP, 100.0f); sy += 28;	
			ModSlider(Rectangle{ sx, sy, sw, 20 }, "feedback", "%.0f%%", P_REVERB_FEEDBACK, 100.0f); sy += 28;
		}

		// ---- LFOs -------------------------------------------------------
		const float lfx = 20.0f, lfw = 1878.0f, lfh = 150.0f;
		Panel(lfx, py4, lfw, lfh, "LFOs");
		{
			const float col_w = (lfw - 40.0f) / (float)max_lfos;

			for (int l = 0; l < max_lfos; l++) {
				LFO& lfo = synth.lfos[l];
				const float cx = lfx + 20 + l * col_w;
				const float cw = col_w - 16.0f;

				DrawTextEx(g_app_font, TextFormat("LFO %d", l + 1),
					{ cx, py4 + 36 }, 14, 1.0f, DARKGRAY);

				// ---- waveform strip with a live playhead
				Rectangle wr = { cx, py4 + 56, cw, 40 };
				DrawRectangleRec(wr, Color{ 252, 252, 254, 255 });

				Vector2 prev{};
				for (int i = 0; i <= 96; i++) {
					float u = (float)i / 96.0f;
					float v = synth.oscillate(u * 2.0f * PI, lfo.shape);
					Vector2 p = { wr.x + u * wr.width,
								  wr.y + wr.height * 0.5f - v * (wr.height * 0.40f) };
					if (i > 0) DrawLineEx(prev, p, 1.5f, Color{ 90, 190, 150, 255 });
					prev = p;
				}

				float ph = lfo.phase / (2.0f * PI);
				BeginScissorMode(cx, py4 + 56, cw, 40);
				DrawLineEx({ wr.x + ph * wr.width, wr.y },
					{ wr.x + ph * wr.width, wr.y + wr.height },
					1.5f, Color{ 235, 140, 60, 200 });
				DrawRectangleLinesEx(wr, 1, Color{ 200, 200, 208, 255 });
				EndScissorMode();

				// ---- shape buttons
				const WaveShape shapes[4] = { SINE, SAW, SQUARE, TRIANGLE };
				const char* names[4] = { "sin", "saw", "sqr", "tri" };
				for (int i = 0; i < 4; i++) {
					Rectangle br = { cx + i * (cw / 4.0f), py4 + 100, cw / 4.0f - 4, 20 };
					bool active = lfo.shape == shapes[i];
					bool hover = CheckCollisionPointRec(mouse, br);
					if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) lfo.shape = shapes[i];

					DrawRectangleRec(br, active ? Color{ 225, 238, 252, 255 }
					: (hover ? Color{ 244, 244, 248, 255 } : Color{ 252, 252, 254, 255 }));
					DrawRectangleLinesEx(br, active ? 2.0f : 1.0f,
						active ? Color{ 60, 120, 190, 255 } : Color{ 200, 200, 208, 255 });
					Vector2 ns = MeasureTextEx(g_app_font, names[i], 12, 1.0f);
					DrawTextEx(g_app_font, names[i],
						{ br.x + (br.width - ns.x) * 0.5f, br.y + 4 }, 12, 1.0f,
						active ? Color{ 40, 100, 175, 255 } : Color{ 130, 130, 140, 255 });
				}

				// ---- rate, log-mapped so slow and fast are both reachable
				float u = log10f(std::max(lfo.frequency, 0.01f) / 0.05f) / log10f(40.0f / 0.05f);
				u = std::clamp(u, 0.0f, 1.0f);
				GuiSliderBar(Rectangle{ cx + 30, py4 + 126, cw - 66, 18 }, "rate",
					TextFormat("%.2f Hz", lfo.frequency), &u, 0.0f, 1.0f);
				lfo.frequency = 0.05f * powf(40.0f / 0.05f, u);

				// ---- what this LFO is driving, and a clear-all
				std::string dests;
				int count = 0;
				for (int i = 0; i < max_routings; i++) {
					const Routing& rt = synth.routings[i];
					if (rt.lfo != l || rt.param < 0) continue;
					if (count) dests += ", ";
					dests += synth.params[rt.param].name;
					count++;
				}
				DrawTextEx(g_app_font,
					count ? dests.c_str() : "not routed",
					{ cx, py4 + 150 }, 12, 1.0f,
					count ? Color{ 130, 130, 140, 255 } : Color{ 185, 185, 195, 255 });

				if (count && GuiButton(Rectangle{ cx + cw - 50, py4 + 36, 50, 16 }, "clear")) {
					for (int i = 0; i < max_routings; i++)
						if (synth.routings[i].lfo == l) synth.routings[i] = Routing{};
				}
			}
		}

		// ---- keyboard ---------------------------------------------------
		const float kpy = py4 + ph4 - 80;
		const float ex = 20.0f;
		DrawTextEx(g_app_font, "Keyboard", { ex, kpy }, 16, 1.0f, DARKGRAY);

		{
			float start_f = (float)key_view_start;
			GuiSliderBar(Rectangle{ ex + 180, kpy, 240, 20 }, "first",
				TextFormat("%s%d", kNoteNames[key_view_start % 12], OctaveOf(key_view_start)),
				&start_f, 0.0f, 87.0f);
			key_view_start = std::clamp((int)start_f, 0, 87);
			while (key_view_start > 0 && IsBlackKey(key_view_start)) key_view_start--;

			float count_f = (float)key_view_count;
			GuiSliderBar(Rectangle{ ex + 560, kpy, 240, 20 }, "shown",
				TextFormat("%d", key_view_count), &count_f, 7.0f, 61.0f);
			key_view_count = std::clamp((int)count_f, 7, 88 - key_view_start);

			DrawTextEx(g_app_font,
				TextFormat("%.1f Hz  -  %.1f Hz",
					synth.keys[key_view_start].frequency,
					synth.keys[key_view_start + key_view_count - 1].frequency),
				{ ex + 880, kpy + 3 }, 14, 1.0f, Color{ 140, 140, 150, 255 });
		}

		const int first = key_view_start;
		const int last = first + key_view_count;
		const float ky = kpy + 36, wh = 150, bh = 96;

		int white_count = 0;
		for (int i = first; i < last; i++) if (!IsBlackKey(i)) white_count++;
		const float ww = std::min(56.0f, 1500.0f / (float)std::max(white_count, 1));
		const float bw = ww * 0.62f;

		bool mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

		std::vector<float> kx(key_view_count);
		{
			int w = 0;
			for (int i = first; i < last; i++) {
				if (IsBlackKey(i)) kx[i - first] = ex + (float)w * ww - bw * 0.5f;
				else               kx[i - first] = ex + (float)w++ * ww;
			}
		}

		int hit = -1;
		for (int i = first; i < last && hit < 0; i++)
			if (IsBlackKey(i) && CheckCollisionPointRec(mouse, Rectangle{ kx[i - first], ky, bw, bh }))
				hit = i;
		for (int i = first; i < last && hit < 0; i++)
			if (!IsBlackKey(i) && CheckCollisionPointRec(mouse, Rectangle{ kx[i - first], ky, ww, wh }))
				hit = i;

		// pass 1: white keys
		for (int i = first; i < last; i++) {
			if (IsBlackKey(i)) continue;
			Key& k = synth.keys[i];
			int j = i - first;
			bool kb = j < kKeyCodeCount && IsKeyDown(kKeyCodes[j]);
			k.is_down = (hit == i && mouse_down) || kb;

			Rectangle r = { kx[j], ky, ww, wh };
			float lvl = std::clamp(k.level, 0.0f, 1.0f);
			DrawRectangleRec(r, Color{
				(unsigned char)Lerp(252.0f, 120.0f, lvl),
				(unsigned char)Lerp(252.0f, 180.0f, lvl),
				(unsigned char)Lerp(255.0f, 245.0f, lvl), 255 });
			DrawRectangleLinesEx(r, 1, Color{ 170, 170, 178, 255 });

			const char* note = TextFormat("%s%d", kNoteNames[i % 12], OctaveOf(i));
			Vector2 ns = MeasureTextEx(g_app_font, note, 13, 1.0f);
			DrawTextEx(g_app_font, note, { r.x + (ww - ns.x) * 0.5f, ky + wh - 22 },
				13, 1.0f, Color{ 150, 150, 160, 255 });

			if (j < kKeyCodeCount) {
				Vector2 ts = MeasureTextEx(g_app_font, kKeyNames[j], 17, 1.0f);
				DrawTextEx(g_app_font, kKeyNames[j], { r.x + (ww - ts.x) * 0.5f, ky + wh - 46 },
					17, 1.0f, Color{ 90, 90, 100, 255 });
			}
		}

		// pass 2: black keys, over the whites
		for (int i = first; i < last; i++) {
			if (!IsBlackKey(i)) continue;
			Key& k = synth.keys[i];
			int j = i - first;
			bool kb = j < kKeyCodeCount && IsKeyDown(kKeyCodes[j]);
			k.is_down = (hit == i && mouse_down) || kb;

			Rectangle r = { kx[j], ky, bw, bh };
			float lvl = std::clamp(k.level, 0.0f, 1.0f);
			DrawRectangleRec(r, Color{
				(unsigned char)Lerp(40.0f, 90.0f, lvl),
				(unsigned char)Lerp(40.0f, 150.0f, lvl),
				(unsigned char)Lerp(48.0f, 220.0f, lvl), 255 });
			DrawRectangleLinesEx(r, 1, Color{ 20, 20, 26, 255 });

			if (j < kKeyCodeCount) {
				Vector2 ts = MeasureTextEx(g_app_font, kKeyNames[j], 14, 1.0f);
				DrawTextEx(g_app_font, kKeyNames[j], { r.x + (bw - ts.x) * 0.5f, ky + bh - 24 },
					14, 1.0f, Color{ 220, 220, 228, 255 });
			}
		}

		DrawModPopup();
	}

	void App::RefreshDisplayChunk() {
		if (fourier.chunks.empty()) return;
		const Chunk& c = fourier.chunks[selected_chunk];
		fourier.fill_spectrum_from_real_and_imag(c.spec_real, c.spec_imag, disp_xc, disp_mag, disp_phase, c.sample_rate);
		for (int k = 0; k < c.nyquist; k++) disp_mag[k] *= c.gain[k];
		disp_peaks = fourier.compute_peaks(c);

		disp_xcr.resize(disp_peaks.size() * 3);
		disp_ycr.resize(disp_peaks.size() * 3);
		for (int j = 0; j < (int)disp_peaks.size(); j++) {
			const Peak& p = disp_peaks[j];
			disp_xcr[j * 3] = p.freq;   disp_ycr[j * 3] = 0.0f;
			disp_xcr[j * 3 + 1] = p.freq; disp_ycr[j * 3 + 1] = p.magnitude;
			disp_xcr[j * 3 + 2] = p.freq; disp_ycr[j * 3 + 2] = 0.0f;
		}

		disp_yr.assign(c.n(), 0.0f);
		for (int i = 0; i < c.n(); i++) {
			float t = (float)i / (float)c.sample_rate;
			for (const Peak& p : disp_peaks)
				disp_yr[i] += cosf(2.0f * PI * p.freq * t + p.phase) * p.magnitude;
		}
		disp_chunk = selected_chunk;
	}

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
		if (GuiButton(Rectangle{ 320, (float)y + 40, 100, 30 }, "Capture Profile")) {
			profile = fourier.capture_profile_from_audio_data(fourier.data, "profile", percentile);
		}
		if (GuiButton(Rectangle{ 320, (float)y + 80, 100, 30 }, "Capture Profile from spectro")) {
			profile = fourier.capture_profile_from_spectrogram(fourier.chunks, "profile", percentile);
		}
		GuiSliderBar(Rectangle{ 500, (float)y + 40, 100, 30 }, "percentile",
			TextFormat("%.2f", percentile), &percentile, 0.0f, 1.0f);

		if (GuiButton(Rectangle{ 670, (float)y, 160, 30 }, "Play Audio Sound"))
			PlayBuffer(sound_audio, has_audio, fourier.build_buffer_from_audio_data(), fourier.data.sample_rate);

		if (GuiButton(Rectangle{ 840, (float)y, 160, 30 }, "Stop All Sounds")) {
			if (has_audio) StopSound(sound_audio);
			if (has_curve) StopSound(sound_curve);
			if (has_waves) StopSound(sound_waves);
			if (has_chunks) StopSound(sound_chunks);
			if (has_live) StopSound(sound_live);
			if (has_spectra) StopSound(sound_spectra);
		}

		float window_size = (float)fourier.window_size;
		GuiSliderBar(Rectangle{ (float)1100, (float)y, 300, 20 }, "window size", TextFormat("%.1f", (float)fourier.window_size), &window_size, 2.0f, 22050);
		if (window_size_power_of_2) {
			int power = (int)round(log2(window_size));
			window_size = (float)pow(2, power);
		}
		fourier.window_size = (int)window_size;
		GuiCheckBox(Rectangle{ 1450, (float)y, 20, 20 }, "Window Size Power of 2", &window_size_power_of_2);

		int sample_rate = fourier.data.sample_rate > 0 ? fourier.data.sample_rate : 44100;
		float segment_duration = sample_rate > 0 ? (float)fourier.window_size / (float)sample_rate : 0.0f;
		int frequencies_per_segment = fourier.window_size / 2;
		float frequency_spacing = sample_rate > 0 ? (float)sample_rate / (float)fourier.window_size : 0.0f;

		DrawTextEx(g_app_font, TextFormat("Samples per segment: %d", fourier.window_size), { 1100, (float)y + 25 }, 14, 1.0f, DARKGRAY);
		DrawTextEx(g_app_font, TextFormat("Segment duration: %.2f ms", segment_duration * 1000.0f), { 1100, (float)y + 43 }, 14, 1.0f, DARKGRAY);
		DrawTextEx(g_app_font, TextFormat("Frequencies per segment: %d", frequencies_per_segment), { 1100, (float)y + 61 }, 14, 1.0f, DARKGRAY);
		DrawTextEx(g_app_font, TextFormat("Frequency spacing: %.2f Hz", frequency_spacing), { 1100, (float)y + 79 }, 14, 1.0f, DARKGRAY);
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

		int nf = std::min({ (int)fourier.xc.size(), (int)fourier.yc.size(), (int)fourier.ych.size() });
		GraphSeries strong_freq_series[] = {
			{ fourier.xc.data(),  fourier.yc.data(),  nf, MAROON, "Raw" },
			{ fourier.xc.data(), fourier.ych.data(), nf, ORANGE, "Smoothed" },
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
			GuiSliderBar(Rectangle{ (float)x + 280, (float)y, 200, 20 }, "amp", TextFormat("%.2f", w.amplitude), &w.amplitude, 0.0f, 2.0f);
			GuiSliderBar(Rectangle{ (float)x + 560, (float)y, 200, 20 }, "phase", TextFormat("%.2f", w.phase), &w.phase, -PI, PI);
			if (GuiButton(Rectangle{ (float)x + 820, (float)y, 60, 20 }, "Remove")) { fourier.waves.erase(fourier.waves.begin() + i); i--; }
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
			fourier.construct_chunks_from_audio_data();
			BuildSpectrogram(spectrogram, fourier, 48000.0f);
			selected_chunk = 0;
			disp_chunk = -1;
		}
		y += 60;

		float peaks_f = (float)fourier.max_peaks_per_chunk;
		GuiSliderBar(Rectangle{ (float)x, (float)y, 200, 20 }, "max peaks", TextFormat("%d", fourier.max_peaks_per_chunk), &peaks_f, 1.0f, 500.0f);
		if ((int)peaks_f != fourier.max_peaks_per_chunk) {
			fourier.max_peaks_per_chunk = (int)peaks_f;
			disp_chunk = -1;
		}
		y += 50;
		if (fourier.chunks.empty()) return;

		if (GuiButton(Rectangle{ (float)x, (float)y, 260, 30 }, "Play Chunk Sound using peaks"))
			PlayBuffer(sound_chunks, has_chunks, fourier.build_buffer_from_chunks_using_peaks(), fourier.data.sample_rate);
		if (GuiButton(Rectangle{ (float)x + 290, (float)y, 290, 30 }, "Play Spectrum Sound using IFFT"))
			PlayBuffer(sound_spectra, has_spectra, fourier.build_buffer_from_chunks_using_IFFT(), fourier.data.sample_rate);
		y += 60;

		float chunk_f = (float)selected_chunk;
		GuiSliderBar(Rectangle{ (float)x, (float)y, 480, 20 }, "chunk", TextFormat("%d / %d", selected_chunk, (int)fourier.chunks.size() - 1), &chunk_f, 0.0f, (float)(fourier.chunks.size() - 1));
		selected_chunk = std::clamp((int)chunk_f, 0, (int)fourier.chunks.size() - 1);
		y += 50;

		if (selected_chunk != disp_chunk) RefreshDisplayChunk();

		auto& c = fourier.chunks[selected_chunk];
		GuiLabel(Rectangle{ (float)x, (float)y, 300, 20 }, TextFormat("Time offset: %.3fs", c.time_offset));
		y += 20;

		if (GuiButton(Rectangle{ (float)x, (float)y, 220, 30 }, live_playing ? "Stop Live Playback" : "Start Live Playback")) {
			live_playing = !live_playing;
			if (!live_playing) { Unload(sound_live, has_live); last_live_chunk = -1; }
		}
		y += 60;

		if (live_playing && selected_chunk != last_live_chunk) {
			PlayBuffer(sound_live, has_live, fourier.build_buffer_from_single_chunk_using_IFFT(c, 20), fourier.data.sample_rate);
			last_live_chunk = selected_chunk;
		}

		int nr = std::min((int)c.xf.size(), (int)disp_yr.size());
		GraphSeries chunk_curve_series[] = {
			{ c.xf.data(), c.yf.data(), (int)c.yf.size(), BLUE,  "Raw" },
			{ c.xf.data(), c.yh.data(), (int)c.yh.size(), SKYBLUE, "Smoothed" },
			{ c.xf.data(), disp_yr.data(), nr, GREEN, "Reconstructed" },
		};
		DrawGraph(chunk_curve_series, 3, Rectangle{ (float)x, (float)y, 480, 150 }, view_chunk_curve, "Chunk Fourier Curve");
		y += 200;

		int ns = std::min((int)disp_xc.size(), (int)disp_mag.size());
		GraphSeries chunk_spectrum_series[] = {
			{ disp_xc.data(),  disp_mag.data(), ns, SKYBLUE, "Spectrum" },
			{ disp_xcr.data(), disp_ycr.data(), (int)disp_ycr.size(), ORANGE, "Peaks" },
		};
		DrawGraph(chunk_spectrum_series, 2, Rectangle{ (float)x, (float)y, 480, 150 }, view_chunk_reconstructed, "Chunk Spectrum");
		y += 200;

		GuiSliderBar(Rectangle{ (float)x + 350, (float)y, 100, 20 }, "alpha",
			TextFormat("%.2f", alpha), &alpha, 0.0f, 1.0f);
		y += 40;

		if (GuiButton(Rectangle{ (float)x + 200, (float)y, 110, 20 }, "Subtract Profile")) {
			fourier.subtract_profile(profile, alpha, 0.05f);
			BuildSpectrogram(spectrogram, fourier, 48000.0f);
		}
		y += 40;

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

		spectrogram.show_original = IsKeyDown(KEY_LEFT_SHIFT);

		Rectangle spec_bounds = { (float)x, (float)y, 480, 750 };
		DrawSpectrogram(spectrogram, spec_bounds, "Spectrogram");
		int t_lo, t_hi;
		if (EditSpectrogram(spectrogram, spec_bounds, fourier.chunks.data(),
			(int)fourier.chunks.size(), brush, t_lo, t_hi)) {
			edit_t_lo = edit_t_lo < 0 ? t_lo : std::min(edit_t_lo, t_lo);
			edit_t_hi = std::max(edit_t_hi, t_hi);
		}
		y += 800;

		if (edit_t_lo >= 0 && !brush.stroking && !brush.dragging) {
			if (selected_chunk >= edit_t_lo && selected_chunk <= edit_t_hi) disp_chunk = -1;
			edit_t_lo = edit_t_hi = -1;
		}
	}

	void App::Init()
	{
		synth.Init(48000);
	}

	void App::Update() {
		ResetGraphWheelConsumption();

		BeginDrawing();
		ClearBackground(RAYWHITE);
		DrawViewTabs();
		if (view == View::Analysis) {
			DrawTopBar();
			DrawManualSection();
			DrawChunkSection();
		}
		else {
			DrawSynthSection();
			synth.Update();
		}
		EndDrawing();

		if (!WasGraphWheelConsumed()) {
			scroll_offset -= GetMouseWheelMove() * 40.0f;
			scroll_offset = std::max(scroll_offset, 0.0f);
		}
	}

	void App::Shutdown()
	{
		synth.Shutdown();
	}

}  // namespace resynth