#pragma once
#include "raylib.h"
#include <vector>

namespace resynth {
    struct GraphView {
        float target_x_min = 0, target_x_max = 0, target_y_min = 0, target_y_max = 0;
        float x_min = 0, x_max = 0, y_min = 0, y_max = 0;
        bool initialized = false;
        double last_click_time = -1.0;
    };

    struct GraphSeries {
        const float* xs;
        const float* ys;
        int count;
        Color color;
        const char* name = nullptr; // shown in legend + hover box if set
		bool draw_points = false; // if true, draw points at each data point else, draw lines between points
    };

    // Multi-curve version — draws every series in `series` on the same axes.
    void DrawGraph(const GraphSeries* series, int series_count, Rectangle bounds, GraphView& view, const char* label = nullptr);

    // Convenience single-curve overload — unchanged call sites keep working.
    inline void DrawGraph(const float* xs, const float* ys, int count, Rectangle bounds, Color color, GraphView& view, const char* label = nullptr) {
        GraphSeries s{ xs, ys, count, color, nullptr };
        DrawGraph(&s, 1, bounds, view, label);
    }

    struct SpectrogramData {
        Texture2D texture{};
        std::vector<float> magnitudes; // raw magnitude values, row-major: [time_index * freq_count + freq_index]
        int time_count = 0;
        int freq_count = 0;
        float time_step = 0;  // seconds per chunk (x-axis spacing)
        float freq_step = 0;  // Hz per bin (y-axis spacing)
        bool built = false;
        Image image{};               // kept alive so we can repaint pixels
        float max_mag = 0.0f;        // colour normalisation reference
        bool hann_smoothing = true;  // which chunk array this view reflects
        Texture2D original_texture{};
        bool show_original = false;
    };

    enum class SpectrogramTool { Brush, Select };

    struct SpectrogramBrush {
        bool enabled = false;
        float radius_px = 20.0f;
        float strength = 0.5f;   // fraction of max_mag added at brush centre
        bool soft = true;
        Vector2 last_pos{};
        bool stroking = false;
        SpectrogramTool tool = SpectrogramTool::Brush;
        bool dragging = false;
        bool drag_add = false;
        Vector2 drag_start{};
    };

    // Builds the texture + raw magnitude grid from your chunk data. Call once whenever chunks change
    // (e.g. right after "Construct Chunks"), not every frame — rebuilding is relatively expensive.
    void BuildSpectrogram(SpectrogramData& spec, const struct Chunk* chunks, int chunk_count, float max_freq_bins = -1, bool hann_smooting = true);

    // Draws the built spectrogram + hover readout. Cheap — call this every frame.
    void DrawSpectrogram(SpectrogramData& spec, Rectangle bounds, const char* label = nullptr);

    bool EditSpectrogram(SpectrogramData& spec, Rectangle bounds, Chunk* chunks, int chunk_count, SpectrogramBrush& brush, int& out_t_lo, int& out_t_hi);
    bool PaintSpectrogram(SpectrogramData& spec, Rectangle bounds, Chunk* chunks, int chunk_count, SpectrogramBrush& brush, int& out_t_lo, int& out_t_hi);

    //tracking mouse wheel consumption
    void ResetGraphWheelConsumption();
    bool WasGraphWheelConsumed();

}  // namespace resynth