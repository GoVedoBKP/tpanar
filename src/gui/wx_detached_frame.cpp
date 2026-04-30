#include "wx_detached_frame.h"
#include <wx/notebook.h>

namespace tpanar_ns {

wxBEGIN_EVENT_TABLE(DetachedFrame, wxFrame)
    EVT_CLOSE(DetachedFrame::OnClose)
    EVT_SIZE(DetachedFrame::OnSize)
wxEND_EVENT_TABLE()

DetachedFrame::DetachedFrame(wxWindow* panel, const wxString& title, wxWindow* parent, int tab_index)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600)),
      m_panel(panel), m_parent(parent), m_tab_index(tab_index), m_notebook(nullptr)
{
    wxNotebook* nb = dynamic_cast<wxNotebook*>(parent);
    if (nb) {
        m_notebook = nb;
        for (size_t i = 0; i < nb->GetPageCount(); i++) {
            if (nb->GetPage(i) == panel) {
                m_original_tab_index = (int)i;
                nb->RemovePage(i);
                break;
            }
        }
    }
    
    SetSizeHints(100, 100);
    
    m_panel->Reparent(this);
    m_panel->SetSize(GetClientSize());
    m_panel->Show();
    
    Fit();
    Show();
}

void DetachedFrame::OnSize(wxSizeEvent& event) {
    if (m_panel) {
        m_panel->SetSize(GetClientSize());
    }
    event.Skip();
}

void DetachedFrame::OnClose(wxCloseEvent& event) {
    reattach();
    Destroy();
}

void DetachedFrame::reattach() {
    if (!m_panel || !m_notebook) return;
    
    m_panel->Reparent(m_notebook);
    m_panel->Show();
    
    int insert_idx = m_original_tab_index;
    if (insert_idx < 0 || insert_idx > (int)m_notebook->GetPageCount()) {
        insert_idx = m_notebook->GetPageCount();
    }
    m_notebook->InsertPage(insert_idx, m_panel, GetTitle());
    m_notebook->SetSelection(insert_idx);
    
    wxWindow* panel_to_clear = m_panel;
    m_panel = nullptr;
    
    if (m_on_detach) {
        m_on_detach();
    }
}

} // namespace tpanar_ns
