#include "fourier.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cassert>

namespace {
	void normalize_buffer(std::vector<float>& buffer) {
		float max_abs = 0.0f;
		for (float s : buffer) max_abs = std::max(max_abs, std::fabs(s));
		if (max_abs > 1.0f) for (float& s : buffer) s /= max_abs;
	}

	void FFT(std::vector<float>& real, std::vector<float>& imag) {
		int n = (int)real.size();
		assert(imag.size() == (size_t)n);

		// base case: nothing to split further
		if (n <= 1) return;

		// must be a power of 2 — this assert will fire loudly rather than silently
		// producing wrong results if `detail` isn't one.
		assert((n & (n - 1)) == 0 && "FFT size must be a power of 2");

		int half = n / 2;
		std::vector<float> even_real(half), even_imag(half);
		std::vector<float> odd_real(half), odd_imag(half);

		for (int i = 0; i < half; i++) {
			even_real[i] = real[2 * i];
			even_imag[i] = imag[2 * i];
			odd_real[i] = real[2 * i + 1];
			odd_imag[i] = imag[2 * i + 1];
		}

		// recurse on both halves
		FFT(even_real, even_imag);
		FFT(odd_real, odd_imag);

		// combine
		for (int k = 0; k < half; k++) {
			float angle = -2.0f * PI * (float)k / (float)n;
			float twiddle_real = cosf(angle);
			float twiddle_imag = sinf(angle);

			// complex multiplication: twiddle * odd[k]
			float t_real = twiddle_real * odd_real[k] - twiddle_imag * odd_imag[k];
			float t_imag = twiddle_real * odd_imag[k] + twiddle_imag * odd_real[k];

			real[k] = even_real[k] + t_real;
			imag[k] = even_imag[k] + t_imag;
			real[k + half] = even_real[k] - t_real;
			imag[k + half] = even_imag[k] - t_imag;
		}
	}
} // namespace

namespace resynth {
	void Fourier::construct_fourier_from_audio_data(std::vector<float>& x, std::vector<float>& y, std::vector<float>& data, int window_size, int start_offset) {
		x.resize(window_size); y.resize(window_size);
		for (int i = 0; i < window_size; i++) {
			float t = time_for_index(i);
			x[i] = t;
			y[i] = data[start_offset + i];
		}
	}
	void Fourier::construct_fourier_from_waves(std::vector<float>& x, std::vector<float>& y, const std::vector<resynth::Wave>& waves, int window_size) {
		x.resize(window_size); y.resize(window_size);
		for (int i = 0; i < window_size; i++) {
			float t = time_for_index(i);
			x[i] = t;
			y[i] = 0.0f;
			for (const auto& w : waves) {
				float angle = t * 2.0f * PI * w.frequency;
				y[i] += sinf(angle + w.phase) * w.amplitude;
			}
		}
	}
	void Fourier::apply_hann_window(const std::vector<float>& y, std::vector<float>& yh, int window_size) {
		yh.resize(window_size);
		for (int i = 0; i < window_size; i++) {
			float t = (float)i / (float)window_size;
			float w = 0.5f * (1.0f - cosf(2.0f * PI * t));
			yh[i] = y[i] * w;
		}
	}
	void Fourier::compute_spectrum_FFT(const std::vector<float>& y, std::vector<float>& xc, std::vector<float>& yc, std::vector<float>& phase, int nyquist) {
		xc.resize(nyquist); yc.resize(nyquist); phase.resize(nyquist);

		int n = (int)y.size();
		std::vector<float> real = y;
		std::vector<float> imag(n, 0.0f);
		FFT(real, imag);

		const float bin_hz = data.sample_rate / (float)n;
		for (int j = 0; j < nyquist; j++) {
			float scale = (j == 0 ? 1.0f : 2.0f) / (float)n;
			xc[j] = (float)j * bin_hz;
			yc[j] = sqrtf(real[j] * real[j] + imag[j] * imag[j]) * scale;
			phase[j] = atan2f(imag[j], real[j]);
		}
	}
	void Fourier::compute_spectrum_DFT(const std::vector<float>& y, std::vector<float>& xc, std::vector<float>& yc, std::vector<float>& phase, int nyquist) {
		xc.resize(nyquist); yc.resize(nyquist); phase.resize(nyquist); 
		for (int j = 0; j < nyquist; j++) {
			float real_sum = 0.0f, imag_sum = 0.0f;
			for (int i = 0; i < (int)y.size(); i++) {
				float t = time_for_index(i);
				float angle = t * 2.0f * PI * (float)j;
				real_sum += y[i] * cosf(angle);
				imag_sum += y[i] * sinf(angle);
			}
			xc[j] = (float)j;
			yc[j] = sqrtf(real_sum * real_sum + imag_sum * imag_sum) / (float)y.size();
			phase[j] = atan2f(imag_sum, real_sum);
		}
	}
	void Fourier::sphere_fourier_at_frequency(const std::vector<float>& y, std::vector<float>& xs, std::vector<float>& ys, float frequency, int window_size) {
		xs.resize(window_size); ys.resize(window_size);
		for (int i = 0; i < window_size; i++) {
			float t = time_for_index(i);
			float angle = t * 2.0f * PI * frequency;
			Vector2 p = Vector2Scale(Vector2{ cosf(angle), sinf(angle) }, y[i]);
			xs[i] = p.x; ys[i] = p.y;
		}
	}
	void Fourier::find_peaks_in_graph(const std::vector<float>& x, const std::vector<float>& y, std::vector<float>& phase, std::vector<Peak>& peaks) {
		peaks.clear();
		int n = (int)y.size();
		for (int j = 1; j < n - 1; j++) {
			if (y[j] > y[j - 1] && y[j] > y[j + 1]) {
				peaks.push_back({ x[j], y[j], phase[j]});
			}
		}
		std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
	}
	void Fourier::rederive_chunk_peaks(Chunk& c) {
		find_peaks_in_graph(c.xc, c.ych, c.phase_h, c.peaks);

		int top_n = std::min(max_peaks_per_chunk, (int)c.peaks.size());
		c.yr.assign(window_size, 0.0f);
		for (int i = 0; i < window_size; i++) {
			float t = time_for_index(i);
			for (int p = 0; p < top_n; p++)
				c.yr[i] += cosf(2.0f * PI * c.peaks[p].freq * t + c.peaks[p].phase) * c.peaks[p].magnitude;
		}
		c.xcr.resize(c.peaks.size() * 3);
		c.ycr.resize(c.peaks.size() * 3);
		for (int j = 0; j < (int)c.peaks.size(); j++) {
			auto& peak = c.peaks[j];
			c.xcr[j * 3] = peak.freq;   c.ycr[j * 3] = 0.0f;
			c.xcr[j * 3 + 1] = peak.freq; c.ycr[j * 3 + 1] = peak.magnitude;
			c.xcr[j * 3 + 2] = peak.freq; c.ycr[j * 3 + 2] = 0.0f;
		}
	}

	void Fourier::construct_chunks_from_audio_data() {
		chunks.clear();
		if (data.samples.empty()) return;

		int total_samples = (int)data.samples.size();
		int chunk_nyquist = window_size / 2;

		for (int start = 0; start + window_size <= total_samples; start += window_size / 2) {
			Chunk c;
			c.time_offset = (float)start / (float)data.sample_rate;
			c.nyquist = chunk_nyquist;

			construct_fourier_from_audio_data(c.xf, c.yf, data.samples, window_size, start);
			apply_hann_window(c.yf, c.yh, window_size);

			compute_spectrum_FFT(c.yf, c.xc, c.yc, c.phase, chunk_nyquist);
			compute_spectrum_FFT(c.yh, c.xc, c.ych, c.phase_h, chunk_nyquist);

			find_peaks_in_graph(c.xc, c.ych, c.phase_h, c.peaks);

			c.yr.assign(window_size, 0.0f);
			for (auto& peak : c.peaks) {
				for (int i = 0; i < window_size; i++) {
					float t = time_for_index(i);
					c.yr[i] += sinf(2.0f * PI * t * peak.freq + peak.phase) * peak.magnitude;
				}
			}

			c.xcr.resize(c.peaks.size() * 3);
			c.ycr.resize(c.peaks.size() * 3);
			for (int j = 0; j < (int)c.peaks.size(); j++) {
				auto& peak = c.peaks[j];
				c.xcr[j * 3] = peak.freq;     c.ycr[j * 3] = 0.0f;
				c.xcr[j * 3 + 1] = peak.freq; c.ycr[j * 3 + 1] = peak.magnitude;
				c.xcr[j * 3 + 2] = peak.freq; c.ycr[j * 3 + 2] = 0.0f;
			}

			chunks.push_back(std::move(c));
		}
	}

	void Fourier::compute_fourier_data(bool use_fft) {
		constructed = true;

		nyquist = use_wave_data ? (window_size / 2) : (data.sample_rate / 2);

		// fourier curve
		if (use_wave_data) {
			construct_fourier_from_waves(xf, yf, waves, window_size);
		}
		else {
			construct_fourier_from_audio_data(xf, yf, data.samples, window_size);
		}
		apply_hann_window(yf, yh, window_size);

		if (use_fft) {
			compute_spectrum_FFT(yf, xc, yc, phase, nyquist);
			find_peaks_in_graph(xc, yc, phase, peaks);
		}
		else {
			sphere_fourier_at_frequency(yf, xs, ys, test_frequency, window_size);
			sphere_fourier_at_frequency(yh, xsh, ysh, test_frequency, window_size);

			compute_spectrum_DFT(yf, xc, yc, phase, nyquist);
			compute_spectrum_DFT(yh, xc, ych, phase_h, nyquist);

			find_peaks_in_graph(xc, ych, phase_h, peaks);
		}
	}

	std::vector<float> Fourier::build_buffer_from_audio_data() const {
		std::vector<float> buffer = data.samples;
		normalize_buffer(buffer);
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_fourier_curve(int repeat_count) const {
		if (yf.empty()) return {};

		std::vector<float> buffer(yf.size() * repeat_count);
		for (int r = 0; r < repeat_count; r++) {
			std::copy(yf.begin(), yf.end(), buffer.begin() + r * yf.size());
		}
		normalize_buffer(buffer);
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_waves() const {
		std::vector<float> buffer(render_detail, 0.0f);
		for (int i = 0; i < render_detail; i++) {
			float t = (float)i / (float)render_detail;
			float sample = 0.0f;
			for (auto& w : waves) sample += sinf(2.0f * PI * t * w.frequency + w.phase) * w.amplitude;
			buffer[i] = sample;
		}
		normalize_buffer(buffer);
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_chunks() const {
		if (chunks.empty()) return {};
		const int n = window_size;
		const int hop = n / 2;
		std::vector<float> buffer(hop * ((int)chunks.size() - 1) + n, 0.0f);

		for (int ci = 0; ci < (int)chunks.size(); ci++) {
			const auto& chunk = chunks[ci];
			int top_n = std::min(max_peaks_per_chunk, (int)chunk.peaks.size());
			int base = ci * hop;
			for (int i = 0; i < n; i++) {
				float t = (float)i / (float)data.sample_rate;
				float sample = 0.0f;
				for (int p = 0; p < top_n; p++) {
					const auto& peak = chunk.peaks[p];
					sample += cosf(2.0f * PI * peak.freq * t + peak.phase) * peak.magnitude;
				}
				float w = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)n));
				buffer[base + i] += sample * w;
			}
		}
		normalize_buffer(buffer);
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_single_chunk(const Chunk& c, int repeat_count) const {
		int n = window_size;
		int top_n = std::min(max_peaks_per_chunk, (int)c.peaks.size());
		std::vector<float> single(n, 0.0f);
		for (int i = 0; i < n; i++) {
			float t = (float)i / (float)data.sample_rate;
			float sample = 0.0f;
			for (int p = 0; p < top_n; p++) {
				auto& peak = c.peaks[p];
				sample += sinf(2.0f * PI * peak.freq * t + peak.phase) * peak.magnitude;
			}
			single[i] = sample;
		}
		normalize_buffer(single);

		std::vector<float> buffer(n * repeat_count);
		for (int r = 0; r < repeat_count; r++) std::copy(single.begin(), single.end(), buffer.begin() + r * n);
		return buffer;
	}

}  // namespace resynth