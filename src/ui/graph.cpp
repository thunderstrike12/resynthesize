#include "graph.hpp"
#include "raymath.h"
#include "../app/app_font.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include "../audio/fourier.hpp"

namespace resynth {

	namespace {

		constexpr Color kBackground = { 250, 250, 252, 255 };
		constexpr Color kGridLine = { 225, 225, 230, 255 };
		constexpr Color kAxisBorder = { 160, 160, 170, 255 };
		constexpr Color kTickText = { 90, 90, 100, 255 };
		constexpr Color kCrosshair = { 60, 60, 70, 180 };
		constexpr Color kHoverBoxBg = { 30, 30, 35, 230 };
		constexpr Color kHoverBoxText = { 255, 255, 255, 255 };
		constexpr float kSmoothSpeed = 12.0f;
		constexpr float kLineThickness = 2.0f;

		Vector2 MapToScreen(float x, float y, float x_min, float x_max, float y_min, float y_max, Rectangle bounds) {
			float nx = (x - x_min) / (x_max - x_min);
			float ny = (y - y_min) / (y_max - y_min);
			return { bounds.x + nx * bounds.width, bounds.y + bounds.height - ny * bounds.height };
		}

		float NiceStep(float range, int desired_ticks) {
			if (range <= 0.0f) return 1.0f;
			float raw = range / (float)desired_ticks;
			float mag = powf(10.0f, floorf(log10f(raw)));
			float norm = raw / mag;
			float nice = (norm < 1.5f) ? 1.0f : (norm < 3.0f) ? 2.0f : (norm < 7.0f) ? 5.0f : 10.0f;
			return nice * mag;
		}

		int FindNearestIndex(const float* xs, int count, float target_x) {
			int lo = 0, hi = count - 1;
			while (lo < hi) {
				int mid = (lo + hi) / 2;
				if (xs[mid] < target_x) lo = mid + 1; else hi = mid;
			}
			if (lo > 0 && std::fabs(xs[lo - 1] - target_x) < std::fabs(xs[lo] - target_x)) lo--;
			return lo;
		}
		bool g_wheel_consumed_this_frame = false;
	}  // namespace

	void ResetGraphWheelConsumption() { g_wheel_consumed_this_frame = false; }
	bool WasGraphWheelConsumed() { return g_wheel_consumed_this_frame; }

	void DrawGraph(const GraphSeries* series, int series_count, Rectangle bounds, GraphView& view, const char* label) {
		if (series_count <= 0) return;

		if (!view.initialized) {
			float x_min = std::numeric_limits<float>::max(), x_max = std::numeric_limits<float>::lowest();
			float y_min = std::numeric_limits<float>::max(), y_max = std::numeric_limits<float>::lowest();
			bool any_data = false;
			for (int s = 0; s < series_count; s++) {
				const auto& ser = series[s];
				if (!ser.xs || !ser.ys || ser.count <= 0) continue;   // GUARD
				any_data = true;
				for (int i = 0; i < ser.count; i++) {
					x_min = std::min(x_min, ser.xs[i]); x_max = std::max(x_max, ser.xs[i]);
					y_min = std::min(y_min, ser.ys[i]); y_max = std::max(y_max, ser.ys[i]);
				}
			}
			if (!any_data) return;                                    // GUARD
			if (x_min == x_max) x_max += 1.0f;
			if (y_min == y_max) { y_min -= 1.0f; y_max += 1.0f; }

			view.target_x_min = view.x_min = x_min;
			view.target_x_max = view.x_max = x_max;
			view.target_y_min = view.y_min = y_min;
			view.target_y_max = view.y_max = y_max;
			view.initialized = true;
		}

		Vector2 mouse = GetMousePosition();
		Rectangle y_axis_strip = { bounds.x - 50, bounds.y, 50, bounds.height };
		Rectangle x_axis_strip = { bounds.x, bounds.y + bounds.height, bounds.width, 25 };
		bool hovering_plot = CheckCollisionPointRec(mouse, bounds);
		bool hovering_y_axis = CheckCollisionPointRec(mouse, y_axis_strip);
		bool hovering_x_axis = CheckCollisionPointRec(mouse, x_axis_strip);

		if (hovering_plot || hovering_y_axis || hovering_x_axis) g_wheel_consumed_this_frame = true;

		float wheel = GetMouseWheelMove();
		if (hovering_plot && wheel != 0.0f) {
			float zoom = wheel > 0 ? 0.85f : 1.176f;
			float tx = (mouse.x - bounds.x) / bounds.width;
			float cursor_x = view.target_x_min + tx * (view.target_x_max - view.target_x_min);
			view.target_x_min = cursor_x - (cursor_x - view.target_x_min) * zoom;
			view.target_x_max = cursor_x + (view.target_x_max - cursor_x) * zoom;

			float ty = 1.0f - (mouse.y - bounds.y) / bounds.height;
			float cursor_y = view.target_y_min + ty * (view.target_y_max - view.target_y_min);
			view.target_y_min = cursor_y - (cursor_y - view.target_y_min) * zoom;
			view.target_y_max = cursor_y + (view.target_y_max - cursor_y) * zoom;
		}
		else if (hovering_y_axis && wheel != 0.0f) {
			float zoom = wheel > 0 ? 0.85f : 1.176f;
			float ty = 1.0f - (mouse.y - bounds.y) / bounds.height;
			float cursor_y = view.target_y_min + ty * (view.target_y_max - view.target_y_min);
			view.target_y_min = cursor_y - (cursor_y - view.target_y_min) * zoom;
			view.target_y_max = cursor_y + (view.target_y_max - cursor_y) * zoom;
		}
		else if (hovering_x_axis && wheel != 0.0f) {
			float zoom = wheel > 0 ? 0.85f : 1.176f;
			float tx = (mouse.x - bounds.x) / bounds.width;
			float cursor_x = view.target_x_min + tx * (view.target_x_max - view.target_x_min);
			view.target_x_min = cursor_x - (cursor_x - view.target_x_min) * zoom;
			view.target_x_max = cursor_x + (view.target_x_max - cursor_x) * zoom;
		}

		if (hovering_plot && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
			Vector2 delta = GetMouseDelta();
			float x_range = view.target_x_max - view.target_x_min;
			float y_range = view.target_y_max - view.target_y_min;
			view.target_x_min += -delta.x / bounds.width * x_range;
			view.target_x_max += -delta.x / bounds.width * x_range;
			view.target_y_min += delta.y / bounds.height * y_range;
			view.target_y_max += delta.y / bounds.height * y_range;
		}

		if ((hovering_plot || hovering_y_axis || hovering_x_axis) && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
			view.initialized = false;
		}

		float t = 1.0f - expf(-kSmoothSpeed * GetFrameTime());
		view.x_min = Lerp(view.x_min, view.target_x_min, t);
		view.x_max = Lerp(view.x_max, view.target_x_max, t);
		view.y_min = Lerp(view.y_min, view.target_y_min, t);
		view.y_max = Lerp(view.y_max, view.target_y_max, t);

		DrawRectangleRec(bounds, kBackground);
		DrawRectangleLinesEx(bounds, 1, kAxisBorder);
		if (label) DrawTextEx(g_app_font, label, { bounds.x, bounds.y - 20 }, 16, 1.0f, DARKGRAY);

		float y_step = NiceStep(view.y_max - view.y_min, 5);
		for (float v = ceilf(view.y_min / y_step) * y_step; v <= view.y_max; v += y_step) {
			float ny = (v - view.y_min) / (view.y_max - view.y_min);
			float sy = bounds.y + bounds.height - ny * bounds.height;
			DrawLine((int)bounds.x, (int)sy, (int)(bounds.x + bounds.width), (int)sy, kGridLine);
			DrawTextEx(g_app_font, TextFormat("%.2f", v), { bounds.x - 48, sy - 7 }, 13, 1.0f, kTickText);
		}

		float x_step = NiceStep(view.x_max - view.x_min, 6);
		for (float v = ceilf(view.x_min / x_step) * x_step; v <= view.x_max; v += x_step) {
			float nx = (v - view.x_min) / (view.x_max - view.x_min);
			float sx = bounds.x + nx * bounds.width;
			DrawLine((int)sx, (int)bounds.y, (int)sx, (int)(bounds.y + bounds.height), kGridLine);
			DrawTextEx(g_app_font, TextFormat("%.1f", v), { sx - 15, bounds.y + bounds.height + 4 }, 13, 1.0f, kTickText);
		}

		// --- curves, one per series ---
		BeginScissorMode((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height);
		for (int s = 0; s < series_count; s++) {
			const auto& ser = series[s];
			if (!ser.xs || !ser.ys || ser.count <= 0) continue;        // GUARD

			if (ser.draw_points) {
				for (int i = 0; i < ser.count; i++) {
					if (ser.xs[i] < view.x_min || ser.xs[i] > view.x_max) continue;
					Vector2 p = MapToScreen(ser.xs[i], ser.ys[i], view.x_min, view.x_max, view.y_min, view.y_max, bounds);
					DrawCircleV(p, 5, ser.color);
				}
			}
			else {
				for (int i = 0; i < ser.count - 1; i++) {
					if (ser.xs[i + 1] < view.x_min || ser.xs[i] > view.x_max) continue;
					Vector2 p0 = MapToScreen(ser.xs[i], ser.ys[i], view.x_min, view.x_max, view.y_min, view.y_max, bounds);
					Vector2 p1 = MapToScreen(ser.xs[i + 1], ser.ys[i + 1], view.x_min, view.x_max, view.y_min, view.y_max, bounds);
					DrawLineEx(p0, p1, kLineThickness, ser.color);
				}
			}
		}
		EndScissorMode();

		// --- legend (only drawn if at least one series has a name) ---
		bool any_named = false;
		for (int s = 0; s < series_count; s++) if (series[s].name) any_named = true;
		if (any_named) {
			int lx = (int)bounds.x + 10, ly = (int)bounds.y + 8;
			for (int s = 0; s < series_count; s++) {
				if (!series[s].name) continue;
				DrawRectangle(lx, ly, 12, 12, series[s].color);
				DrawTextEx(g_app_font, series[s].name, { (float)(lx + 18), (float)(ly - 2) }, 14, 1.0f, DARKGRAY);
				ly += 18;
			}
		}

		// --- hover crosshair + per-series value readout ---
		if (hovering_plot) {
			float mt = (mouse.x - bounds.x) / bounds.width;
			float data_x = view.x_min + mt * (view.x_max - view.x_min);

			DrawLine((int)mouse.x, (int)bounds.y, (int)mouse.x, (int)(bounds.y + bounds.height), kCrosshair);

			char box_text[512] = {};
			int offset = 0;
			offset += sprintf(box_text + offset, "x: %.3f\n", data_x);

			for (int s = 0; s < series_count; s++) {
				const auto& ser = series[s];
				if (!ser.xs || !ser.ys || ser.count <= 0) continue;    // GUARD

				int idx = std::clamp(FindNearestIndex(ser.xs, ser.count, data_x), 0, ser.count - 1);
				Vector2 pt = MapToScreen(ser.xs[idx], ser.ys[idx], view.x_min, view.x_max, view.y_min, view.y_max, bounds);
				DrawCircleV(pt, 4, ser.color);
				const char* name = ser.name ? ser.name : "y";
				offset += sprintf(box_text + offset, "%s: %.3f\n", name, ser.ys[idx]);
			}

			Vector2 text_size = MeasureTextEx(g_app_font, box_text, 14, 1.0f);
			int lines = series_count + 1;
			Vector2 box_pos = { std::min(mouse.x + 12, bounds.x + bounds.width - text_size.x - 12), mouse.y - (lines * 16.0f) - 6 };
			box_pos.y = std::max(box_pos.y, bounds.y + 2);
			DrawRectangle((int)box_pos.x - 4, (int)box_pos.y - 2, (int)text_size.x + 8, lines * 16 + 6, kHoverBoxBg);
			DrawTextEx(g_app_font, box_text, box_pos, 14, 1.0f, kHoverBoxText);
		}
	}


	namespace {
		Color MagnitudeToColor(float t) {
			t = std::clamp(t, 0.0f, 1.0f);
			struct Stop { float pos; Color color; };
			static const Stop stops[] = {
				{ 0.00f, { 5,   5,  15, 255 } },
				{ 0.25f, { 40,  10, 120, 255 } },
				{ 0.50f, { 140, 30, 120, 255 } },
				{ 0.75f, { 235, 100, 40, 255 } },
				{ 1.00f, { 255, 245, 180, 255 } },
			};
			for (int i = 0; i < 4; i++) {
				if (t >= stops[i].pos && t <= stops[i + 1].pos) {
					float local = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
					Color a = stops[i].color, b = stops[i + 1].color;
					return { (unsigned char)Lerp((float)a.r, (float)b.r, local),
							 (unsigned char)Lerp((float)a.g, (float)b.g, local),
							 (unsigned char)Lerp((float)a.b, (float)b.b, local), 255 };
				}
			}
			return stops[4].color;
		}
	}  // namespace

	void BuildSpectrogram(SpectrogramData& spec, const Fourier& fourier, float max_freq_hz) {
		const auto& chunks = fourier.chunks;
		int chunk_count = (int)chunks.size();
		if (chunk_count <= 0) { spec.built = false; return; }

		float bin_hz = fourier.bin_to_hz(1);
		int freq_count = chunks[0].nyquist;
		if (max_freq_hz > 0.0f && bin_hz > 0.0f)
			freq_count = std::min(freq_count, (int)ceilf(max_freq_hz / bin_hz));
		freq_count = std::max(freq_count, 1);

		spec.time_count = chunk_count;
		spec.freq_count = freq_count;
		spec.time_step = chunk_count > 1 ? (chunks[1].time_offset - chunks[0].time_offset) : 0.0f;
		spec.freq_step = bin_hz;

		spec.magnitudes.assign((size_t)chunk_count * freq_count, 0.0f);
		std::vector<float> xc, mag, ph;
		float max_mag = 0.0f;
		for (int t = 0; t < chunk_count; t++) {
			fourier.fill_spectrum_from_real_and_imag(chunks[t].spec_real, chunks[t].spec_imag, xc, mag, ph, chunks[t].nyquist);
			for (int f = 0; f < freq_count; f++) {
				float g = f < (int)chunks[t].gain.size() ? chunks[t].gain[f] : 1.0f;
				spec.magnitudes[(size_t)t * freq_count + f] = mag[f];
				max_mag = std::max(max_mag, mag[f] * g);
			}
		}
		spec.max_mag = max_mag > 1e-9f ? max_mag : 1.0f;

		Image img = GenImageColor(chunk_count, freq_count, BLACK);
		for (int t = 0; t < chunk_count; t++)
			for (int f = 0; f < freq_count; f++) {
				float m = spec.magnitudes[(size_t)t * freq_count + f] * chunks[t].gain[f];
				float norm = std::min(powf(std::clamp(m / spec.max_mag, 0.0f, 1.0f), 0.25f) * 1.8f, 1.0f);
				ImageDrawPixel(&img, t, freq_count - 1 - f, MagnitudeToColor(norm));
			}

		if (spec.built) {
			UnloadTexture(spec.texture);
			UnloadTexture(spec.original_texture);
			UnloadImage(spec.image);
		}
		spec.image = img;
		spec.texture = LoadTextureFromImage(img);
		spec.original_texture = LoadTextureFromImage(img);
		SetTextureFilter(spec.texture, TEXTURE_FILTER_POINT);
		SetTextureFilter(spec.original_texture, TEXTURE_FILTER_POINT);
		spec.built = true;
	}

	void DrawSpectrogram(SpectrogramData& spec, Rectangle bounds, const char* label) {
		if (!spec.built) return;

		DrawRectangleLinesEx(bounds, 1, kAxisBorder);
		if (label) DrawTextEx(g_app_font, label, { bounds.x, bounds.y - 20 }, 16, 1.0f, DARKGRAY);

		Texture2D& tex = (spec.show_original && spec.original_texture.id != 0)
			? spec.original_texture : spec.texture;
		Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
		DrawTexturePro(tex, src, bounds, { 0, 0 }, 0.0f, WHITE);

		if (spec.show_original)
			DrawTextEx(g_app_font, "ORIGINAL", { bounds.x + 8, bounds.y + 8 }, 14, 1.0f, RAYWHITE);

		// y-axis (frequency) labels
		int y_ticks = 6;
		for (int i = 0; i <= y_ticks; i++) {
			float frac = (float)i / y_ticks;
			float freq = frac * spec.freq_count * spec.freq_step;
			float sy = bounds.y + bounds.height - frac * bounds.height;
			DrawTextEx(g_app_font, TextFormat("%.0f Hz", freq), { bounds.x - 60, sy - 7 }, 13, 1.0f, kTickText);
		}

		// x-axis (time) labels
		int x_ticks = 6;
		for (int i = 0; i <= x_ticks; i++) {
			float frac = (float)i / x_ticks;
			float time = frac * spec.time_count * spec.time_step;
			float sx = bounds.x + frac * bounds.width;
			DrawTextEx(g_app_font, TextFormat("%.2fs", time), { sx - 15, bounds.y + bounds.height + 4 }, 13, 1.0f, kTickText);
		}

		// hover readout
		Vector2 mouse = GetMousePosition();
		if (CheckCollisionPointRec(mouse, bounds)) {
			float tx = (mouse.x - bounds.x) / bounds.width;
			float ty = 1.0f - (mouse.y - bounds.y) / bounds.height;
			int t_idx = std::clamp((int)(tx * spec.time_count), 0, spec.time_count - 1);
			int f_idx = std::clamp((int)(ty * spec.freq_count), 0, spec.freq_count - 1);
			float mag = spec.magnitudes[(size_t)t_idx * spec.freq_count + f_idx];

			DrawLine((int)mouse.x, (int)bounds.y, (int)mouse.x, (int)(bounds.y + bounds.height), kCrosshair);
			DrawLine((int)bounds.x, (int)mouse.y, (int)(bounds.x + bounds.width), (int)mouse.y, kCrosshair);

			const char* txt = TextFormat("t: %.3fs  f: %.0f Hz  mag: %.4f", t_idx * spec.time_step, f_idx * spec.freq_step, mag);
			Vector2 size = MeasureTextEx(g_app_font, txt, 14, 1.0f);
			Vector2 pos = { std::min(mouse.x + 12, bounds.x + bounds.width - size.x - 12), mouse.y - 24 };
			DrawRectangle((int)pos.x - 4, (int)pos.y - 3, (int)size.x + 8, 20, kHoverBoxBg);
			DrawTextEx(g_app_font, txt, pos, 14, 1.0f, kHoverBoxText);
		}
	}

	namespace {
		void RepaintPixel(SpectrogramData& spec, const Chunk* chunks, int t, int f) {
			float g = f < (int)chunks[t].gain.size() ? chunks[t].gain[f] : 1.0f;
			float m = spec.magnitudes[(size_t)t * spec.freq_count + f] * g;
			float norm = std::min(powf(std::clamp(m / spec.max_mag, 0.0f, 1.0f), 0.25f) * 1.8f, 1.0f);
			ImageDrawPixel(&spec.image, t, spec.freq_count - 1 - f, MagnitudeToColor(norm));
		}

		void FlushRegion(SpectrogramData& spec, const Chunk* chunks,
			int t_lo, int t_hi, int f_lo, int f_hi) {
			for (int t = t_lo; t <= t_hi; t++)
				for (int f = f_lo; f <= f_hi; f++) RepaintPixel(spec, chunks, t, f);

			int row_lo = spec.freq_count - 1 - f_hi;
			Rectangle dirty = { 0, (float)row_lo, (float)spec.time_count, (float)(f_hi - f_lo + 1) };
			UpdateTextureRec(spec.texture, dirty,
				(const Color*)spec.image.data + (size_t)row_lo * spec.time_count);
		}
	}

	static bool PaintSpectrogram(SpectrogramData& spec, Rectangle bounds, Chunk* chunks, int chunk_count, SpectrogramBrush& brush, int& out_t_lo, int& out_t_hi) {
		if (!spec.built || spec.time_count != chunk_count) return false;

		Vector2 mouse = GetMousePosition();
		bool inside = CheckCollisionPointRec(mouse, bounds);
		if (inside) {
			g_wheel_consumed_this_frame = true;
			DrawCircleLinesV(mouse, brush.radius_px, kCrosshair);
			float wheel = GetMouseWheelMove();
			if (wheel != 0.0f) brush.radius_px = std::clamp(brush.radius_px + wheel * 4.0f, 3.0f, 200.0f);
		}

		bool add = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
		bool sub = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
		if (!inside || (!add && !sub)) { brush.stroking = false; return false; }

		Vector2 from = brush.stroking ? brush.last_pos : mouse;
		brush.last_pos = mouse;
		brush.stroking = true;

		float px_per_col = bounds.width / (float)spec.time_count;
		float px_per_row = bounds.height / (float)spec.freq_count;
		float boost = 1.0f + brush.strength * 8.0f;

		int steps = std::max(1, (int)(Vector2Distance(from, mouse) / std::max(brush.radius_px * 0.4f, 1.0f)));
		int rt = (int)ceilf(brush.radius_px / px_per_col);
		int rf = (int)ceilf(brush.radius_px / px_per_row);

		int t_lo = spec.time_count, t_hi = -1, f_lo = spec.freq_count, f_hi = -1;

		for (int s = 0; s <= steps; s++) {
			Vector2 p = Vector2Lerp(from, mouse, (float)s / (float)steps);
			float tc = (p.x - bounds.x) / px_per_col;
			float fc = (bounds.y + bounds.height - p.y) / px_per_row;

			for (int t = (int)tc - rt; t <= (int)tc + rt; t++) {
				if (t < 0 || t >= spec.time_count) continue;
				std::vector<float>& g = chunks[t].gain;

				for (int f = (int)fc - rf; f <= (int)fc + rf; f++) {
					if (f < 0 || f >= spec.freq_count || f >= (int)g.size()) continue;

					float dx = ((float)t + 0.5f - tc) * px_per_col;
					float dy = ((float)f + 0.5f - fc) * px_per_row;
					float d = sqrtf(dx * dx + dy * dy) / brush.radius_px;
					if (d > 1.0f) continue;

					float falloff = brush.soft ? 0.5f + 0.5f * cosf(PI * d) : 1.0f;
					if (sub) g[f] *= (1.0f - falloff);
					else     g[f] = std::max(g[f], Lerp(1.0f, boost, falloff));

					t_lo = std::min(t_lo, t); t_hi = std::max(t_hi, t);
					f_lo = std::min(f_lo, f); f_hi = std::max(f_hi, f);
				}
			}
		}
		if (t_hi < 0) return false;

		FlushRegion(spec, chunks, t_lo, t_hi, f_lo, f_hi);
		out_t_lo = t_lo; out_t_hi = t_hi;
		return true;
	}

	static bool SelectSpectrogram(SpectrogramData& spec, Rectangle bounds, Chunk* chunks,
		int chunk_count, SpectrogramBrush& brush,
		int& out_t_lo, int& out_t_hi) {
		if (!spec.built || spec.time_count != chunk_count) return false;

		Vector2 mouse = GetMousePosition();
		bool inside = CheckCollisionPointRec(mouse, bounds);
		if (inside) g_wheel_consumed_this_frame = true;

		if (!brush.dragging) {
			if (inside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
				brush.dragging = true;
				brush.drag_add = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
				brush.drag_start = mouse;
			}
			return false;
		}

		Vector2 cur = { std::clamp(mouse.x, bounds.x, bounds.x + bounds.width),
						std::clamp(mouse.y, bounds.y, bounds.y + bounds.height) };
		Rectangle sel = { std::min(brush.drag_start.x, cur.x), std::min(brush.drag_start.y, cur.y),
						  fabsf(cur.x - brush.drag_start.x), fabsf(cur.y - brush.drag_start.y) };

		DrawRectangleRec(sel, brush.drag_add ? Color{ 255,245,180,70 } : Color{ 40,40,50,110 });
		DrawRectangleLinesEx(sel, 1, kCrosshair);

		bool released = IsMouseButtonReleased(brush.drag_add ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT);
		if (!released) return false;
		brush.dragging = false;
		if (sel.width < 1.0f || sel.height < 1.0f) return false;

		float px_per_col = bounds.width / (float)spec.time_count;
		float px_per_row = bounds.height / (float)spec.freq_count;

		int t_lo = std::clamp((int)floorf((sel.x - bounds.x) / px_per_col), 0, spec.time_count - 1);
		int t_hi = std::clamp((int)floorf((sel.x + sel.width - bounds.x) / px_per_col), 0, spec.time_count - 1);
		int f_hi = std::clamp((int)floorf((bounds.y + bounds.height - sel.y) / px_per_row), 0, spec.freq_count - 1);
		int f_lo = std::clamp((int)floorf((bounds.y + bounds.height - sel.y - sel.height) / px_per_row), 0, spec.freq_count - 1);

		float boost = 1.0f + brush.strength * 8.0f;
		int fn = f_hi - f_lo;

		for (int t = t_lo; t <= t_hi; t++) {
			std::vector<float>& g = chunks[t].gain;
			for (int f = f_lo; f <= f_hi; f++) {
				if (f >= (int)g.size()) continue;
				if (brush.drag_add) {
					float shape = 1.0f;
					if (brush.soft && fn > 0) {
						float u = (float)(f - f_lo) / (float)fn;
						shape = 0.5f - 0.5f * cosf(2.0f * PI * u);
					}
					g[f] = std::max(g[f], Lerp(1.0f, boost, shape));
				}
				else g[f] = 0.0f;
			}
		}

		FlushRegion(spec, chunks, t_lo, t_hi, f_lo, f_hi);
		out_t_lo = t_lo; out_t_hi = t_hi;
		return true;
	}

	bool EditSpectrogram(SpectrogramData& spec, Rectangle bounds, Chunk* chunks, int chunk_count,
		SpectrogramBrush& brush, int& out_t_lo, int& out_t_hi) {
		if (!brush.enabled) return false;
		return brush.tool == SpectrogramTool::Select
			? SelectSpectrogram(spec, bounds, chunks, chunk_count, brush, out_t_lo, out_t_hi)
			: PaintSpectrogram(spec, bounds, chunks, chunk_count, brush, out_t_lo, out_t_hi);
	}
}  // namespace resynth