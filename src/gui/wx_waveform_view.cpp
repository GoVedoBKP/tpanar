#include "wx_waveform_view.h"
#include "theme.h"
#include "../core/engine.h"

#include <wx/dcclient.h>

namespace tpanar_ns {

wxBEGIN_EVENT_TABLE(WaveformView, wxPanel)
    EVT_PAINT(WaveformView::OnPaint)
    EVT_LEFT_DOWN(WaveformView::OnMouseDown)
    EVT_MOTION(WaveformView::OnMouseDrag)
    EVT_LEFT_UP(WaveformView::OnMouseUp)
    EVT_MOUSEWHEEL(WaveformView::OnMouseWheel)
wxEND_EVENT_TABLE()

WaveformView::WaveformView(wxWindow* parent, wxWindowID id, Engine& engine)
    : wxPanel(parent, id), m_engine(engine)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

void WaveformView::set_sample(std::shared_ptr<SampleData> s) {
    m_sample = s;
    Refresh();
}

void WaveformView::get_view_range(size_t& start, size_t& end) {
    if (!m_sample || m_sample->left.empty()) { start = end = 0; return; }
    size_t total = m_sample->left.size();
    start = m_offset;
    size_t visible = (size_t)(total / m_zoom);
    if (visible < 10) visible = 10;
    end = start + visible;
    if (end > total) {
        end = total;
        start = (end > visible) ? end - visible : 0;
        m_offset = start;
    }
}

int WaveformView::sample_to_x(size_t pos, size_t view_start, size_t view_end, int width) const {
    if (view_end <= view_start) return -1;
    return (int)((double)(pos - view_start) / (view_end - view_start) * width);
}

void WaveformView::zoom_in()  { m_zoom *= 1.5; Refresh(); }
void WaveformView::zoom_out() {
    m_zoom /= 1.5;
    if (m_zoom < 1.0) { m_zoom = 1.0; m_offset = 0; }
    Refresh();
}
void WaveformView::view_all() { m_zoom = 1.0; m_offset = 0; Refresh(); }
void WaveformView::view_selection() {
    if (m_sample && m_sel_start != m_sel_end) {
        size_t s = std::min(m_sel_start, m_sel_end);
        size_t e = std::max(m_sel_start, m_sel_end);
        if (e > s) {
            m_offset = s;
            m_zoom = (double)m_sample->left.size() / (e - s);
            Refresh();
        }
    }
}

void WaveformView::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    wxSize size = GetClientSize();
    const int W = size.GetWidth(), H = size.GetHeight();

    const int TSH = 20; // time-scale strip height (top)
    const int WH  = H - TSH;

    // Background
    wxColour bg = ThemeManager::toWxColour(m_engine.m_tracker_bg);
    dc.SetBrush(wxBrush(bg));
    dc.SetPen(wxPen(bg));
    dc.DrawRectangle(0, 0, W, H);

    if (!m_sample || m_sample->left.empty()) return;

    size_t view_start, view_end;
    get_view_range(view_start, view_end);
    if (view_end <= view_start) return;

    double sample_rate = m_sample->sample_rate;
    double view_dur = (double)(view_end - view_start) / sample_rate;

    double interval = 10.0;
    if      (view_dur < 0.1)  interval = 0.01;
    else if (view_dur < 0.5)  interval = 0.05;
    else if (view_dur < 2.0)  interval = 0.2;
    else if (view_dur < 10.0) interval = 1.0;
    else if (view_dur < 30.0) interval = 5.0;

    // Separator between scale and wave
    wxColour grid_col = ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight);
    dc.SetPen(wxPen(grid_col));
    dc.DrawLine(0, TSH - 1, W, TSH - 1);

    // Time ticks — drawn only in scale strip (y=0..TSH) and grid lines only in wave area (y=TSH..H)
    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
    dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    double first_tick = floor((double)view_start / sample_rate / interval) * interval;
    for (double t = first_tick; t <= (double)view_end / sample_rate + interval; t += interval) {
        int x = (int)((t * sample_rate - view_start) / (view_end - view_start) * W);
        if (x < 0 || x >= W) continue;
        dc.SetPen(wxPen(grid_col));
        // grid line only in wave area — does not overlap the scale text
        dc.DrawLine(x, TSH, x, H);
        // tick mark and label in scale strip
        dc.DrawLine(x, TSH - 5, x, TSH - 1);
        dc.DrawText(wxString::Format("%.2fs", t), x + 2, 2);
    }

    // Waveform drawing helper — clips to [0, y_off, W, h]
    double spp = (double)(view_end - view_start) / W; // samples per pixel
    auto draw_channel = [&](const std::vector<float>& data, int y_off, int h, const wxString& label) {
        int mid_y = y_off + h / 2;
        // Zero line
        dc.SetPen(wxPen(grid_col));
        dc.DrawLine(0, mid_y, W, mid_y);
        // Channel label
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
        dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.DrawText(label, 2, y_off + 2);

        dc.SetPen(wxPen(ThemeManager::toWxColour(m_color)));
        const int y_top = y_off, y_bot = y_off + h;
        for (int i = 0; i < W; ++i) {
            size_t s = view_start + (size_t)(i * spp);
            size_t e = view_start + (size_t)((i + 1) * spp);
            if (e > data.size()) e = data.size();
            if (s >= data.size()) continue;

            float min_v = 1.0f, max_v = -1.0f;
            if (e > s) {
                for (size_t j = s; j < e; ++j) {
                    if (data[j] < min_v) min_v = data[j];
                    if (data[j] > max_v) max_v = data[j];
                }
            } else {
                min_v = max_v = data[s];
            }

            int y1 = mid_y + (int)(min_v * (h / 2 - 2));
            int y2 = mid_y + (int)(max_v * (h / 2 - 2));
            // Clamp to wave area — never draw into the scale strip
            y1 = std::max(y1, y_top);
            y2 = std::min(y2, y_bot - 1);
            if (y1 > y2) std::swap(y1, y2);
            if (y1 == y2) dc.DrawPoint(i, y1);
            else          dc.DrawLine(i, y1, i, y2);
        }
    };

    bool stereo   = !m_sample->right.empty();
    bool both     = (m_mode == ChannelMode::Both) && stereo;
    if (both) {
        draw_channel(m_sample->left,  TSH,          WH / 2, "L");
        draw_channel(m_sample->right, TSH + WH / 2, WH / 2, "R");
        dc.SetPen(wxPen(grid_col));
        dc.DrawLine(0, TSH + WH / 2, W, TSH + WH / 2);
    } else if (m_mode == ChannelMode::Right && stereo) {
        draw_channel(m_sample->right, TSH, WH, "R");
    } else {
        draw_channel(m_sample->left,  TSH, WH, stereo ? "L" : "M");
    }

    // Selection overlay — only in wave area
    if (m_sel_start != m_sel_end) {
        size_t s = std::min(m_sel_start, m_sel_end);
        size_t e = std::max(m_sel_start, m_sel_end);
        if (e > view_start && s < view_end) {
            int x1 = sample_to_x(std::max(s, view_start), view_start, view_end, W);
            int x2 = sample_to_x(std::min(e, view_end),   view_start, view_end, W);
            dc.SetBrush(wxBrush(wxColour(255, 255, 255, 60)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(x1, TSH, x2 - x1, WH);
            // Edge handles
            dc.SetPen(wxPen(wxColour(255, 255, 180), 2));
            dc.DrawLine(x1, TSH, x1, H);
            dc.DrawLine(x2, TSH, x2, H);
        }
    }

    // Playback position cursor line
    if (m_playback_pos >= 0) {
        size_t vstart, vend;
        get_view_range(vstart, vend);
        int px = sample_to_x((size_t)m_playback_pos, vstart, vend, W);
        if (px >= 0 && px < W) {
            dc.SetPen(wxPen(wxColour(255, 220, 0), 2)); // bright yellow
            dc.DrawLine(px, TSH, px, H);
        }
    }
}

void WaveformView::OnMouseDown(wxMouseEvent& event) {
    SetFocus();
    if (!m_sample || m_sample->left.empty()) return;

    int x = event.GetX();
    size_t view_start, view_end;
    get_view_range(view_start, view_end);
    if (view_end <= view_start) return;

    int W = GetClientSize().GetWidth();
    size_t pos = view_start + (size_t)((double)x / W * (view_end - view_start));

    // Check if we're near an existing selection edge
    if (m_sel_start != m_sel_end) {
        int xs = sample_to_x(std::min(m_sel_start, m_sel_end), view_start, view_end, W);
        int xe = sample_to_x(std::max(m_sel_start, m_sel_end), view_start, view_end, W);
        if (std::abs(x - xs) <= EDGE_THRESH) {
            m_drag_mode = DragMode::DragStart;
            // Ensure m_sel_start is the anchor-start so we drag the correct edge
            if (m_sel_start > m_sel_end) std::swap(m_sel_start, m_sel_end);
            CaptureMouse();
            return;
        }
        if (std::abs(x - xe) <= EDGE_THRESH) {
            m_drag_mode = DragMode::DragEnd;
            if (m_sel_start > m_sel_end) std::swap(m_sel_start, m_sel_end);
            CaptureMouse();
            return;
        }
    }

    // Start a new selection
    m_sel_start = m_sel_end = pos;
    m_drag_mode = DragMode::NewSel;
    CaptureMouse();
    Refresh();
}

void WaveformView::OnMouseDrag(wxMouseEvent& event) {
    // Cursor hint even when not dragging
    if (!event.LeftIsDown()) {
        if (m_sample && m_sel_start != m_sel_end) {
            size_t view_start, view_end;
            get_view_range(view_start, view_end);
            int W = GetClientSize().GetWidth();
            int x = event.GetX();
            int xs = sample_to_x(std::min(m_sel_start, m_sel_end), view_start, view_end, W);
            int xe = sample_to_x(std::max(m_sel_start, m_sel_end), view_start, view_end, W);
            if (std::abs(x - xs) <= EDGE_THRESH || std::abs(x - xe) <= EDGE_THRESH)
                SetCursor(wxCursor(wxCURSOR_SIZEWE));
            else
                SetCursor(wxCursor(wxCURSOR_IBEAM));
        } else {
            SetCursor(wxCursor(wxCURSOR_IBEAM));
        }
        return;
    }

    if (m_drag_mode == DragMode::None) return;

    size_t view_start, view_end;
    get_view_range(view_start, view_end);
    if (view_end <= view_start) return;
    int W = GetClientSize().GetWidth();
    int x = std::max(0, std::min(event.GetX(), W - 1));
    size_t pos = view_start + (size_t)((double)x / W * (view_end - view_start));

    if      (m_drag_mode == DragMode::DragStart) m_sel_start = pos;
    else if (m_drag_mode == DragMode::DragEnd)   m_sel_end   = pos;
    else /* NewSel */                             m_sel_end   = pos;

    Refresh();
}

void WaveformView::OnMouseUp(wxMouseEvent&) {
    if (m_drag_mode != DragMode::None) {
        if (HasCapture()) ReleaseMouse();
        m_drag_mode = DragMode::None;
        Refresh();
    }
}

void WaveformView::OnMouseWheel(wxMouseEvent& event) {
    if (event.ControlDown()) {
        if (event.GetWheelRotation() < 0) zoom_out();
        else zoom_in();
    }
}

} // namespace tpanar_ns

