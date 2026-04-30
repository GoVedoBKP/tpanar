#pragma once

#include <wx/frame.h>
#include <wx/notebook.h>
#include <functional>

namespace tpanar_ns {

class DetachedFrame : public wxFrame {
public:
    using AttachCallback = std::function<void()>;
    
    DetachedFrame(wxWindow* panel, const wxString& title, wxWindow* parent, int tab_index = -1);
    void set_on_detach_callback(AttachCallback cb) { m_on_detach = cb; }
    void reattach();

private:
    wxWindow* m_panel;
    wxWindow* m_parent;
    wxNotebook* m_notebook;
    int m_tab_index;
    int m_original_tab_index;
    AttachCallback m_on_detach;

    void OnClose(wxCloseEvent& event);
    void OnSize(wxSizeEvent& event);
    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
