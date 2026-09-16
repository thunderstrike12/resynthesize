#pragma once

#include "audio_data.hpp"

constexpr int render_detail = 44100;
constexpr int max_nyquist = 24000;

namespace resynth {

	struct Peak {
		float freq;
		float magnitude;
		float phase;
	};

	class Wave {
	public:
		float frequency = 0.0f;
		float amplitude = 0.0f;
		float phase = 0.0f;
	};

	struct Chunk {
		float time_offset;
		int nyquist;
		std::vector<float> spec_real, spec_imag;  // full n, FFT of Hann-windowed frame
		std::vector<float> gain;                  // per-bin multiplier, edits live here
		std::vector<float> xf, yf, yh;            // time domain, for the curve graph
	};

	class Fourier {
	public:
		Fourier() {};

		std::vector<Wave> waves;
		std::vector<Chunk> chunks;
		int max_peaks_per_chunk = 10;

		int nyquist = 0;
		int window_size = 1000;

		std::vector<float> xc, yc;
		std::vector<float> ych;
		std::vector<float> phase;
		std::vector<float> phase_h;
		std::vector<float> xf, yf;
		std::vector<float> yh;
		std::vector<float> xs, ys;
		std::vector<float> xsh, ysh;

		std::vector<Peak> peaks;
		int highest_frequency_count = 10;
		float test_frequency = 1.0f;
		float animation_time = 0.0f;
		bool animate = false;
		bool use_wave_data = false;
		bool constructed = false;
		AudioData data;

		float effective_sample_rate() const {
			return use_wave_data ? (float)window_size : (float)data.sample_rate;
		}
		float time_for_index(int i) const {
			return (float)i / effective_sample_rate();
		}
		float bin_to_hz(int k) const {
			return (float)k * effective_sample_rate() / (float)window_size;
		}

		void construct_fourier_from_audio_data(std::vector<float>& x, std::vector<float>& y, std::vector<float>& data, int window_size, int start_offset = 0);
		void construct_fourier_from_waves(std::vector<float>& x, std::vector<float>& y, const std::vector<resynth::Wave>& waves, int window_size);
		void apply_hann_window(const std::vector<float>& y, std::vector<float>& yh, int window_size);
		void compute_spectrum_FFT_from_fourier(const std::vector<float>& y, std::vector<float>& spec_x, std::vector<float>& spec_y, std::vector<float>& phase, int nyquist);
		void compute_spectrum_DFT_from_fourier(const std::vector<float>& y, std::vector<float>& spec_x, std::vector<float>& spec_y, std::vector<float>& phase, int nyquist);
		void fill_spectrum_from_real_and_imag(const std::vector<float>& real, const std::vector<float>& imag, std::vector<float>& spec_x, std::vector<float>& spec_y, std::vector<float>& phase, int nyquist) const;
		void sphere_fourier_at_frequency(const std::vector<float>& y, std::vector<float>& xs, std::vector<float>& ys, float frequency, int window_size);
		void find_peaks_in_graph(const std::vector<float>& x, const std::vector<float>& y, std::vector<float>& phase, std::vector<Peak>& peaks);
		//void rederive_chunk_peaks(Chunk& c);


		std::vector<Peak> compute_peaks(const Chunk& c) const;

		void construct_chunks_from_audio_data();
		void compute_fourier_data(bool use_fft = true);

		std::vector<float> build_buffer_from_audio_data() const;
		std::vector<float> build_buffer_from_fourier_curve(int repeat_count = 200) const;
		std::vector<float> build_buffer_from_waves() const;
		std::vector<float> build_buffer_from_chunks_using_IFFT() const;
		std::vector<float> build_buffer_from_chunks_using_peaks() const;
		std::vector<float> build_buffer_from_single_chunk_using_IFFT(const Chunk& c, int repeat_count) const;
		std::vector<float> build_buffer_from_single_chunk_using_peaks(const Chunk& c, int repeat_count) const;
	};

}  // namespace resynth