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

		// nothing to split further
		if (n <= 1) return;

		// must be a power of 2
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

			float t_real = twiddle_real * odd_real[k] - twiddle_imag * odd_imag[k];
			float t_imag = twiddle_real * odd_imag[k] + twiddle_imag * odd_real[k];

			real[k] = even_real[k] + t_real;
			imag[k] = even_imag[k] + t_imag;
			real[k + half] = even_real[k] - t_real;
			imag[k + half] = even_imag[k] - t_imag;
		}
	}
	void IFFT(std::vector<float>& real, std::vector<float>& imag) {
		int n = (int)real.size();
		for (float& v : imag) v = -v;
		FFT(real, imag);
		float inv = 1.0f / (float)n;
		for (int i = 0; i < n; i++) { real[i] *= inv; imag[i] = -imag[i] * inv; }
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
	void Fourier::apply_hann_window(const std::vector<float>& fourier_y, std::vector<float>& yh, int window_size) {
		yh.resize(window_size);
		for (int i = 0; i < window_size; i++) {
			float t = (float)i / (float)window_size;
			float w = 0.5f * (1.0f - cosf(2.0f * PI * t));
			yh[i] = fourier_y[i] * w;
		}
	}
	void Fourier::compute_spectrum_FFT_from_fourier(const std::vector<float>& fourier_y, std::vector<float>& spec_x, std::vector<float>& spec_y, std::vector<float>& phase, int nyquist) {
		spec_x.resize(nyquist); spec_y.resize(nyquist); phase.resize(nyquist);

		int n = (int)fourier_y.size();
		std::vector<float> real = fourier_y;
		std::vector<float> imag(n, 0.0f);
		FFT(real, imag);

		const float bin_hz = data.sample_rate / (float)n;
		for (int j = 0; j < nyquist; j++) {
			float scale = (j == 0 ? 1.0f : 2.0f) / (float)n;
			spec_x[j] = (float)j * bin_hz;
			spec_y[j] = sqrtf(real[j] * real[j] + imag[j] * imag[j]) * scale;
			phase[j] = atan2f(imag[j], real[j]);
		}
	}
	void Fourier::compute_spectrum_DFT_from_fourier(const std::vector<float>& fourier_y, std::vector<float>& spec_x, std::vector<float>& spec_y, std::vector<float>& phase, int nyquist) {
		spec_x.resize(nyquist); spec_y.resize(nyquist); phase.resize(nyquist);
		for (int j = 0; j < nyquist; j++) {
			float real_sum = 0.0f, imag_sum = 0.0f;
			for (int i = 0; i < (int)fourier_y.size(); i++) {
				float t = time_for_index(i);
				float angle = t * 2.0f * PI * (float)j;
				real_sum += fourier_y[i] * cosf(angle);
				imag_sum += fourier_y[i] * sinf(angle);
			}
			spec_x[j] = (float)j;
			spec_y[j] = sqrtf(real_sum * real_sum + imag_sum * imag_sum) / (float)fourier_y.size();
			phase[j] = atan2f(imag_sum, real_sum);
		}
	}
	void Fourier::sphere_fourier_at_frequency(const std::vector<float>& fourier_y, std::vector<float>& xs, std::vector<float>& ys, float frequency, int window_size) {
		xs.resize(window_size); ys.resize(window_size);
		for (int i = 0; i < window_size; i++) {
			float t = time_for_index(i);
			float angle = t * 2.0f * PI * frequency;
			Vector2 p = Vector2Scale(Vector2{ cosf(angle), sinf(angle) }, fourier_y[i]);
			xs[i] = p.x; ys[i] = p.y;
		}
	}
	void Fourier::find_peaks_in_graph(const std::vector<float>& x, const std::vector<float>& y, std::vector<float>& phase, std::vector<Peak>& peaks) {
		peaks.clear();
		int n = (int)y.size();
		for (int j = 1; j < n - 1; j++) {
			if (y[j] > y[j - 1] && y[j] > y[j + 1]) {
				peaks.push_back({ x[j], y[j], phase[j] });
			}
		}
		std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
	}
	void Fourier::fill_spectrum_from_real_and_imag(
		const std::vector<float>& real, const std::vector<float>& imag,
		std::vector<float>& spec_x, std::vector<float>& spec_y,
		std::vector<float>& phase, int sample_rate) const {
		int n = (int)real.size();
		int nq = n / 2;
		spec_x.resize(nq); spec_y.resize(nq); phase.resize(nq);
		for (int k = 0; k < nq; k++) {
			float re = real[k], im = imag[k];
			float scale = (k == 0 ? 1.0f : 2.0f) / (float)n;
			spec_x[k] = k * sample_rate / (float)n;
			spec_y[k] = sqrtf(re * re + im * im) * scale;
			phase[k] = atan2f(im, re);
		}
	}
	std::vector<Peak> Fourier::compute_peaks(const Chunk& c) const {
		std::vector<float> xc, mag, ph;
		fill_spectrum_from_real_and_imag(c.spec_real, c.spec_imag, xc, mag, ph, c.sample_rate);
		for (int k = 0; k < c.nyquist; k++) mag[k] *= c.gain[k];

		std::vector<Peak> peaks;
		for (int k = 1; k < c.nyquist - 1; k++)
			if (mag[k] > mag[k - 1] && mag[k] > mag[k + 1])
				peaks.push_back({ xc[k], mag[k], ph[k] });

		std::sort(peaks.begin(), peaks.end(),
			[](const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
		if ((int)peaks.size() > max_peaks_per_chunk) peaks.resize(max_peaks_per_chunk);
		return peaks;
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
			c.gain.assign(c.nyquist, 1.0f);
			c.sample_rate = data.sample_rate;


			construct_fourier_from_audio_data(c.xf, c.yf, data.samples, window_size, start);
			apply_hann_window(c.yf, c.yh, window_size);

			c.spec_real = c.yh;
			c.spec_imag.assign(window_size, 0.0f);

			FFT(c.spec_real, c.spec_imag);
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
			compute_spectrum_FFT_from_fourier(yf, xc, yc, phase, nyquist);
			find_peaks_in_graph(xc, yc, phase, peaks);
		}
		else {
			sphere_fourier_at_frequency(yf, xs, ys, test_frequency, window_size);
			sphere_fourier_at_frequency(yh, xsh, ysh, test_frequency, window_size);

			compute_spectrum_DFT_from_fourier(yf, xc, yc, phase, nyquist);
			compute_spectrum_DFT_from_fourier(yh, xc, ych, phase_h, nyquist);

			find_peaks_in_graph(xc, ych, phase_h, peaks);
		}
	}
	void Fourier::subtract_profile(const SpectralProfile& profile, float alpha, float floor_g) {
		if (profile.mag.empty()) return;
		for (Chunk& c : chunks) {
			std::vector<float> xc, mag, ph;
			fill_spectrum_from_real_and_imag(c.spec_real, c.spec_imag, xc, mag, ph, c.sample_rate);
			for (int k = 0; k < c.nyquist; k++) {
				if (mag[k] < 1e-9f) continue;
				float p = profile_at_hz(profile, xc[k]);
				c.gain[k] = std::max(1.0f - alpha * p / mag[k], floor_g);
			}
		}
	}
	float Fourier::profile_at_hz(const SpectralProfile& p, float hz) const {
		if (p.mag.empty() || p.bin_hz <= 0.0f) return 0.0f;
		float b = hz / p.bin_hz;
		if (b <= 0.0f) return p.mag.front();
		if (b >= (float)(p.mag.size() - 1)) return p.mag.back();
		int i = (int)b;
		return Lerp(p.mag[i], p.mag[i + 1], b - (float)i);
	}

	SpectralProfile Fourier::capture_profile_from_audio_data(const AudioData& src, const char* name, float percentile) const {
		SpectralProfile prof;
		prof.name = name ? name : "";
		prof.bin_hz = 0.0f;
		prof.frame_count = 0;

		const int n = window_size;
		const int nq = n / 2;
		const int hop = n / 2;

		// same constraint the chunk pipeline has
		if (n <= 1 || (n & (n - 1)) != 0) return prof;
		if (src.sample_rate <= 0) return prof;
		if ((int)src.samples.size() < n) return prof;

		// precompute the Hann window once
		std::vector<float> win(n);
		for (int i = 0; i < n; i++)
			win[i] = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)n));

		const int total = (int)src.samples.size();
		const int frames = (total - n) / hop + 1;

		// frame-major magnitude table: [f * nq + k]
		std::vector<float> table((size_t)frames * nq);
		std::vector<float> energy(frames, 0.0f);

		std::vector<float> re(n), im(n);
		for (int f = 0; f < frames; f++) {
			const int base = f * hop;
			for (int i = 0; i < n; i++) {
				re[i] = src.samples[base + i] * win[i];
				im[i] = 0.0f;
			}
			FFT(re, im);

			float e = 0.0f;
			for (int k = 0; k < nq; k++) {
				// must match fill_spectrum_from_real_and_imag exactly
				float scale = (k == 0 ? 1.0f : 2.0f) / (float)n;
				float m = sqrtf(re[k] * re[k] + im[k] * im[k]) * scale;
				table[(size_t)f * nq + k] = m;
				e += m * m;
			}
			energy[f] = e;
		}

		// energy gate: keep frames above 10% of the median frame energy,
		// so leading/trailing silence doesn't drag the estimate down
		std::vector<char> keep(frames, 1);
		int kept = frames;
		{
			std::vector<float> sorted = energy;
			std::nth_element(sorted.begin(), sorted.begin() + frames / 2, sorted.end());
			float gate = sorted[frames / 2] * 0.1f;
			kept = 0;
			for (int f = 0; f < frames; f++) {
				keep[f] = energy[f] >= gate ? 1 : 0;
				kept += keep[f];
			}
			if (kept == 0) { std::fill(keep.begin(), keep.end(), 1); kept = frames; }
		}

		// per-bin 25th percentile across the kept frames — tracks the
		// steady floor rather than the transients
		prof.mag.assign(nq, 0.0f);
		std::vector<float> scratch;
		scratch.reserve(kept);
		for (int k = 0; k < nq; k++) {
			scratch.clear();
			for (int f = 0; f < frames; f++)
				if (keep[f]) scratch.push_back(table[(size_t)f * nq + k]);

			size_t idx = std::min(scratch.size() - 1, (size_t)(percentile * (float)(scratch.size() - 1)));
			std::nth_element(scratch.begin(), scratch.begin() + idx, scratch.end());
			prof.mag[k] = scratch[idx];
		}

		prof.bin_hz = (float)src.sample_rate / (float)n;
		prof.frame_count = kept;
		return prof;
	}

	SpectralProfile Fourier::capture_profile_from_spectrogram(const std::vector<Chunk>& c, const char* name, float percentile) const {
		SpectralProfile prof;
		if (c.empty())return prof;
		int nq = c[0].nyquist;
		prof.name = name ? name : "";
		prof.bin_hz = c[0].sample_rate / (float)(2 * nq);
		prof.frame_count = c.size();

		prof.mag.assign(nq, 0.0f);

		std::vector<float> table((size_t)c.size() * nq);
		for (int i = 0; i < c.size(); i++) {
			std::vector<float> xc, mag, ph;
			fill_spectrum_from_real_and_imag(c[i].spec_real, c[i].spec_imag, xc, mag, ph, c[i].sample_rate);
			for (int j = 0; j < nq; j++) {
				table[i * nq + j] = mag[j] * c[i].gain[j];
			}
		}

		int idx = std::min((int)((float)c.size() * percentile), (int)c.size() - 1);
		for (int f = 0; f < nq; f++)
		{
			std::vector<float> bin_mags;
			bin_mags.assign(c.size(), 0.0f);
			for (int b = 0; b < c.size(); b++) {
				bin_mags[b] = table[b * nq + f];
			}
			std::sort(bin_mags.begin(), bin_mags.end());

			prof.mag[f] = bin_mags[idx];
		}
		return prof;
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

	std::vector<float> Fourier::build_buffer_from_chunks_using_IFFT() const {
		if (chunks.empty()) return {};
		const int n = chunks[0].n();
		const int hop = n / 2;
		std::vector<float> buffer(hop * ((int)chunks.size() - 1) + n, 0.0f);

		for (int ci = 0; ci < (int)chunks.size(); ci++) {
			const Chunk& c = chunks[ci];
			std::vector<float> re = c.spec_real, im = c.spec_imag;

			// apply the edit mask to each bin and its conjugate twin
			for (int k = 0; k < c.nyquist; k++) {
				float g = c.gain[k];
				if (g == 1.0f) continue;
				re[k] *= g; im[k] *= g;
				if (k > 0) { re[n - k] *= g; im[n - k] *= g; }
			}

			IFFT(re, im);

			int base = ci * hop;
			for (int i = 0; i < n; i++) buffer[base + i] += re[i];
		}
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_chunks_using_peaks() const {
		if (chunks.empty()) return {};
		const int n = chunks[0].n();
		const int hop = n / 2;
		std::vector<float> buffer(hop * ((int)chunks.size() - 1) + n, 0.0f);

		for (int ci = 0; ci < (int)chunks.size(); ci++) {
			std::vector<Peak> peaks = compute_peaks(chunks[ci]);
			int base = ci * hop;
			for (int i = 0; i < n; i++) {
				float t = (float)i / (float)chunks[ci].sample_rate;
				float sample = 0.0f;
				for (const auto& p : peaks) sample += cosf(2.0f * PI * p.freq * t + p.phase) * p.magnitude;
				float w = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)n));
				buffer[base + i] += sample * w;
			}
		}
		normalize_buffer(buffer);
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_single_chunk_using_IFFT(const Chunk& c, int repeat_count) const {
		const int n = c.n();
		std::vector<float> re = c.spec_real, im = c.spec_imag;
		for (int k = 0; k < c.nyquist; k++) {
			float g = c.gain[k];
			if (g == 1.0f) continue;
			re[k] *= g; im[k] *= g;
			if (k > 0) { re[n - k] *= g; im[n - k] *= g; }
		}
		IFFT(re, im);

		std::vector<float> buffer(n * repeat_count);
		for (int r = 0; r < repeat_count; r++)
			std::copy(re.begin(), re.begin() + n, buffer.begin() + r * n);
		return buffer;
	}

	std::vector<float> Fourier::build_buffer_from_single_chunk_using_peaks(const Chunk& c, int repeat_count) const {
		const int n = c.n();
		std::vector<Peak> peaks = compute_peaks(c);

		std::vector<float> single(n, 0.0f);
		for (int i = 0; i < n; i++) {
			float t = (float)i / (float)c.sample_rate;
			float sample = 0.0f;
			for (const auto& p : peaks)
				sample += cosf(2.0f * PI * p.freq * t + p.phase) * p.magnitude;
			single[i] = sample;
		}
		normalize_buffer(single);

		// overlap the tails so repeats don't click
		const int fade = std::min(n / 8, 256);
		const int stride = n - fade;
		std::vector<float> buffer(stride * (repeat_count - 1) + n, 0.0f);

		for (int r = 0; r < repeat_count; r++) {
			int base = r * stride;
			for (int i = 0; i < n; i++) {
				float w = 1.0f;
				if (i < fade)            w = (float)i / (float)fade;
				else if (i >= n - fade)  w = (float)(n - 1 - i) / (float)fade;
				buffer[base + i] += single[i] * w;
			}
		}
		return buffer;
	}
}  // namespace resynth