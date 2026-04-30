#pragma once

#include <wx/wxprec.h>
#include <wx/panel.h>
#include <memory>
#include <vector>
#include <memory>
#include "../analysis/fft_analyzer.h"

namespace tpanar_ns {

class Engine;

class SpectralView : public wxPanel {
public:
    SpectralView(wxWindow* parent, wxWindowID id, Engine& engine);
    ~SpectralView();

    void update();
    void OnPaint(wxPaintEvent& event);

private:
    Engine& m_engine;
    std::unique_ptr<FFTAnalyzer> m_analyzer;
    std::vector<float> m_fft_input;
    std::vector<float> m_magnitudes;
    size_t m_fft_size = 2048;

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
