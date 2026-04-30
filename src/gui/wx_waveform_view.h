#pragma once

#include <wx/wxprec.h>
#include <wx/panel.h>
#include <memory>

#include "../audio/sample_data.h"

namespace tpanar_ns {

class Engine;

enum class ChannelMode { Both, Left, Right };

class WaveformView : public wxPanel {
public:
    WaveformView(wxWindow* parent, wxWindowID id, Engine& engine);

    void set_sample(std::shared_ptr<SampleData> s);
    void set_color(unsigned int c) { m_color = c; Refresh(); }

    void zoom_in();
    void zoom_out();
    void view_all();
    void view_selection();

    void OnPaint(wxPaintEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseDrag(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);

    size_t selection_start() const { return m_sel_start < m_sel_end ? m_sel_start : m_sel_end; }
    size_t selection_end()   const { return m_sel_start < m_sel_end ? m_sel_end : m_sel_start; }
    void set_channel_mode(ChannelMode mode) { m_mode = mode; Refresh(); }
    void set_playback_pos(int64_t pos) { m_playback_pos = pos; Refresh(false); }

private:
    void get_view_range(size_t& start, size_t& end);
    // Returns pixel x for a sample position (or -1 if outside view).
    int sample_to_x(size_t pos, size_t view_start, size_t view_end, int width) const;

    Engine& m_engine;
    std::shared_ptr<SampleData> m_sample;
    unsigned int m_color = 0x40FF4000;

    size_t m_sel_start = 0;
    size_t m_sel_end   = 0;

    double m_zoom   = 1.0;
    size_t m_offset = 0;

    ChannelMode m_mode = ChannelMode::Both;

    int64_t m_playback_pos = -1;

    enum class DragMode { None, NewSel, DragStart, DragEnd };
    DragMode m_drag_mode = DragMode::None;

    static constexpr int EDGE_THRESH = 10; // pixels

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
