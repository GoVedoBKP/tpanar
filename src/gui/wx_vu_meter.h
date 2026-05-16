#pragma once

#include <wx/wxprec.h>
#include <wx/panel.h>

namespace tpanar_ns {

class Engine;

class VUMeter : public wxPanel {
public:
    VUMeter(wxWindow* parent, wxWindowID id, Engine& engine, bool horizontal = false);

    void level(float l);
    float level() const { return m_level; }

    void OnPaint(wxPaintEvent& event);

private:
    Engine& m_engine;
    float m_level = 0.0f;
    float m_peak_hold = 0.0f;
    int m_peak_timer = 0;
    bool m_horizontal = false;
    long m_last_refresh_ms = 0;

    DECLARE_EVENT_TABLE()
};

} // namespace tpanar_ns
