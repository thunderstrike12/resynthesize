#pragma once
#include "raylib.h"
#include "../audio/fourier.hpp"
#include "tinyfiledialogs.h"
#include "../ui/graph.hpp"

namespace resynth {

    class App {
    public:
        void Update();

    private:
        float scroll_offset = 0.0f;

        Fourier fourier;
        char audio_path[256] = "";
        bool audio_path_edit = false;
		bool window_size_power_of_2 = true;

        bool show_manual_section = true;
        bool show_chunk_section = false;

        Sound sound_audio{}, sound_curve{}, sound_waves{}, sound_chunks{}, sound_live{}, sound_spectra{};
        bool has_audio = false, has_curve = false, has_waves = false, has_chunks = false, has_live = false, has_spectra = false;

        bool draw_on_spectrogram = false;
        bool live_playing = false;
        int selected_chunk = 0, last_live_chunk = -1;

        std::vector<float> disp_xc, disp_mag, disp_phase, disp_xcr, disp_ycr, disp_yr;
        std::vector<Peak> disp_peaks;
        int disp_chunk = -1;
        void RefreshDisplayChunk();

        GraphView view_fourier_curve;
        GraphView view_strong_freq;
        GraphView view_sphere;
        GraphView view_chunk_curve, view_chunk_reconstructed, view_chunk_spectrum;
        SpectrogramData spectrogram;
        SpectrogramBrush brush; int edit_t_lo = -1, edit_t_hi = -1;

        void Unload(Sound& s, bool& has);
        void PlayBuffer(Sound& s, bool& has, const std::vector<float>& buffer, int sample_rate);

        void DrawTopBar();
        void DrawManualSection();
        void DrawChunkSection();
    };

}  // namespace resynth