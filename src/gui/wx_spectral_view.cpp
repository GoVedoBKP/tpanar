#include "wx_spectral_view.h"
#include "../core/engine.h"
#include <wx/dcclient.h>
#include <cmath>
#include <algorithm>

namespace tpanar_ns {

wxBEGIN_EVENT_TABLE(SpectralView, wxPanel)
    EVT_PAINT(SpectralView::OnPaint)
wxEND_EVENT_TABLE()

SpectralView::SpectralView(wxWindow* parent, wxWindowID id, Engine& engine)
    : wxPanel(parent, id), m_engine(engine)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_fft_size = 2048;
    m_analyzer = std::make_unique<FFTAnalyzer>(m_fft_size);
    m_fft_input.resize(m_fft_size, 0.0f);
    m_magnitudes.resize(m_fft_size / 2 + 1, 0.0f);
}

SpectralView::~SpectralView() {}

void SpectralView::update() {
    // Pull data from engine ringbuffer
    float val;
    bool has_data = false;
    while (m_engine.m_spectral_rb.pop(val)) {
        has_data = true;
        // Shift input and add new value
        for (size_t i = 0; i < m_fft_size - 1; ++i) {
            m_fft_input[i] = m_fft_input[i+1];
        }
        m_fft_input[m_fft_size-1] = val;
    }
    
    if (!has_data) return;

    // Apply windowing (Hanning) and process
    std::vector<float> windowed(m_fft_size);
    for (size_t i = 0; i < m_fft_size; ++i) {
        float multiplier = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (m_fft_size - 1)));
        windowed[i] = m_fft_input[i] * multiplier;
    }
    
    m_analyzer->process(windowed.data());
    const auto& new_mags = m_analyzer->magnitudes();

    // Normalize by N/2 (Hanning window coherent gain) so 0 dBFS signal → magnitude 1.0,
    // matching the calibration of the digital VU meters.
    const float norm_factor = 1.0f / (float)(m_fft_size / 2);

    // Dampen sensitivity with smoothing (Exponential moving average)
    float alpha = 0.25f; // Lower = slower, less sensitive
    for (size_t i = 0; i < m_magnitudes.size(); ++i) {
        m_magnitudes[i] = alpha * (new_mags[i] * norm_factor) + (1.0f - alpha) * m_magnitudes[i];
    }

    Refresh(false);
}

void SpectralView::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    wxSize size = GetClientSize();
    int w = size.GetWidth();
    int h = size.GetHeight();

    // Background: Black
    dc.SetBrush(*wxBLACK_BRUSH);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, w, h);

    if (m_magnitudes.empty()) return;

    // Logarithmic frequency scale mapping
    float sample_rate = (float)m_engine.sample_rate();
    float min_freq = 20.0f;
    float max_freq = std::min(20000.0f, sample_rate / 2.0f);
    
    float log_min = log10f(min_freq);
    float log_max = log10f(max_freq);

    int num_bars = 80; 
    int bar_w = std::max(1, w / num_bars);
    int gap = 1;

    for (int i = 0; i < num_bars; ++i) {
        float f_start = powf(10.0f, log_min + ((float)i / num_bars) * (log_max - log_min));
        float f_end = powf(10.0f, log_min + ((float)(i + 1) / num_bars) * (log_max - log_min));
        
        float bin_start = f_start * (float)m_fft_size / sample_rate;
        float bin_end = f_end * (float)m_fft_size / sample_rate;
        
        float max_mag = 0.0f;
        size_t start_idx = (size_t)bin_start;
        size_t end_idx = (size_t)ceilf(bin_end);
        
        for (size_t idx = start_idx; idx < end_idx && idx < m_magnitudes.size(); ++idx) {
            max_mag = std::max(max_mag, m_magnitudes[idx]);
        }

        // dB scaling — same reference as digital VU meters: -60 to 0 dBFS
        float db = 20.0f * log10f(max_mag + 1e-6f);
        float db_min = -60.0f;
        float db_max = 0.0f;
        float norm_y = (db - db_min) / (db_max - db_min);
        norm_y = std::max(0.0f, std::min(1.0f, norm_y));
        
        int bar_h = (int)(norm_y * h);
        int x = i * (bar_w + gap);

        if (bar_h > 0) {
            // Color zones match the digital VU meter: green 0–70%, yellow 70–90%, red 90–100%
            int green_h  = std::min(bar_h, (int)(0.7f * h));
            int yellow_h = std::min(bar_h, (int)(0.9f * h)) - green_h;
            int red_h    = bar_h - (green_h + yellow_h);

            if (green_h > 0) {
                dc.SetBrush(wxBrush(wxColour(0, 255, 0)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(x, h - green_h, bar_w, green_h);
            }
            if (yellow_h > 0) {
                dc.SetBrush(wxBrush(wxColour(255, 255, 0)));
                dc.DrawRectangle(x, h - (green_h + yellow_h), bar_w, yellow_h);
            }
            if (red_h > 0) {
                dc.SetBrush(wxBrush(wxColour(255, 0, 0)));
                dc.DrawRectangle(x, h - bar_h, bar_w, red_h);
            }
        }
    }
    
    // Grid lines
    dc.SetPen(wxPen(wxColour(60, 60, 60), 1, wxPENSTYLE_DOT));
    float freqs_to_mark[] = { 100, 1000, 10000 };
    for (float f : freqs_to_mark) {
        if (f > min_freq && f < max_freq) {
            float log_f = log10f(f);
            int gx = (int)(((log_f - log_min) / (log_max - log_min)) * w);
            dc.DrawLine(gx, 0, gx, h);
        }
    }
}

} // namespace tpanar_ns
