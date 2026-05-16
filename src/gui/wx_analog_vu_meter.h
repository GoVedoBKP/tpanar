#pragma once

#include <wx/wxprec.h>
#include <wx/panel.h>

namespace tpanar_ns {

class Engine;

class AnalogVUMeter : public wxPanel {
public:
    AnalogVUMeter(wxWindow* parent, wxWindowID id, Engine& engine);

    void level(float l);
    float level() const { return m_level; }

    void OnPaint(wxPaintEvent& event);

private:
    void draw_static_bg(wxDC& dc);

    Engine& m_engine;
    float m_level = 0.0f;
    float m_smooth_level = 0.0f;
    wxBitmap m_bg_cache;
    long m_last_refresh_ms = 0;

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
