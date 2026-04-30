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
    Engine& m_engine;
    float m_level = 0.0f;
    float m_smooth_level = 0.0f;

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
