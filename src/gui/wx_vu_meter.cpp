#include "wx_vu_meter.h"
#include "theme.h"
#include "../core/engine.h"

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <algorithm>
#include <cmath>

namespace tpanar_ns {

wxBEGIN_EVENT_TABLE(VUMeter, wxPanel)
    EVT_PAINT(VUMeter::OnPaint)
wxEND_EVENT_TABLE()

VUMeter::VUMeter(wxWindow* parent, wxWindowID id, Engine& engine, bool horizontal)
    : wxPanel(parent, id), m_engine(engine), m_horizontal(horizontal)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
}

void VUMeter::level(float l) {
    l = std::clamp(l, 0.0f, 2.0f);

    const float previous_level = m_level;
    const float previous_peak = m_peak_hold;
    const float attack = 0.55f;
    const float release = 0.18f;

    if (l > m_level) {
        m_level += (l - m_level) * attack;
    } else {
        m_level += (l - m_level) * release;
    }

    if (m_level > m_peak_hold) {
        m_peak_hold = m_level;
        m_peak_timer = 12;
    } else if (m_peak_timer > 0) {
        --m_peak_timer;
    } else {
        m_peak_hold += (m_level - m_peak_hold) * 0.2f;
        if (m_peak_hold < m_level) {
            m_peak_hold = m_level;
        }
    }

    if (std::fabs(previous_level - m_level) < 0.002f &&
        std::fabs(previous_peak - m_peak_hold) < 0.002f) {
        return;
    }

    Refresh(false);
}

void VUMeter::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();

    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_bg)));
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_bg)));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    float db = 20.0f * std::log10(m_level + 0.00001f);
    float db_max = 0.0f;
    float db_min = -60.0f;
    float level_norm = (db - db_min) / (db_max - db_min);
    level_norm = std::max(0.0f, std::min(1.0f, level_norm));

    float peak_norm = (20.0f * std::log10(m_peak_hold + 0.00001f) - db_min) / (db_max - db_min);
    peak_norm = std::max(0.0f, std::min(1.0f, peak_norm));

    wxColour green(0, 255, 0);
    wxColour yellow(255, 255, 0);
    wxColour red(255, 0, 0);

    int w = size.GetWidth();
    int h = size.GetHeight();
    int level_pixels = (int)(level_norm * (m_horizontal ? w : h));

    if (m_horizontal) {
        int green_end = (int)(0.7f * w);
        int yellow_end = (int)(0.9f * w);

        if (level_pixels > 0) {
            int g_pix = std::min(level_pixels, green_end);
            dc.SetBrush(wxBrush(green));
            dc.SetPen(wxPen(green));
            dc.DrawRectangle(0, 0, g_pix, h);

            if (level_pixels > green_end) {
                int y_pix = std::min(level_pixels, yellow_end) - green_end;
                dc.SetBrush(wxBrush(yellow));
                dc.SetPen(wxPen(yellow));
                dc.DrawRectangle(green_end, 0, y_pix, h);
            }

            if (level_pixels > yellow_end) {
                int r_pix = level_pixels - yellow_end;
                dc.SetBrush(wxBrush(red));
                dc.SetPen(wxPen(red));
                dc.DrawRectangle(yellow_end, 0, r_pix, h);
            }
        }

        int peak_x = (int)(peak_norm * w);
        dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
        dc.SetPen(wxPen(wxColour(255, 255, 255)));
        dc.DrawLine(peak_x, 0, peak_x, h);
    } else {
        int green_end = (int)(0.7f * h);
        int yellow_end = (int)(0.9f * h);

        if (level_pixels > 0) {
            int g_pix = std::min(level_pixels, green_end);
            dc.SetBrush(wxBrush(green));
            dc.SetPen(wxPen(green));
            dc.DrawRectangle(0, h - g_pix, w, g_pix);

            if (level_pixels > green_end) {
                int y_pix = std::min(level_pixels, yellow_end) - green_end;
                dc.SetBrush(wxBrush(yellow));
                dc.SetPen(wxPen(yellow));
                dc.DrawRectangle(0, h - (green_end + y_pix), w, y_pix);
            }

            if (level_pixels > yellow_end) {
                int r_pix = level_pixels - yellow_end;
                dc.SetBrush(wxBrush(red));
                dc.SetPen(wxPen(red));
                dc.DrawRectangle(0, h - (yellow_end + r_pix), w, r_pix);
            }
        }

        int peak_y = h - (int)(peak_norm * h);
        dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
        dc.SetPen(wxPen(wxColour(255, 255, 255)));
        dc.DrawLine(0, peak_y, w, peak_y);
    }
}

} // namespace tpanar_ns
