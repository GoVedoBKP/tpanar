#pragma once

#include <wx/panel.h>
#include <wx/html/htmlwin.h>

namespace tpanar_ns {

class Engine;

class HelpPanel : public wxPanel {
public:
    HelpPanel(wxWindow* parent, Engine& engine);

private:
    Engine& m_engine;
    wxHtmlWindow* m_html_win;

    void load_documentation();
};

} // namespace tpanar_ns
