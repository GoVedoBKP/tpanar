/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include <wx/wxprec.h>
#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/strconv.h>
#include <wx/wxcrtbase.h>
#include <locale.h>

#include "gui/wx_main_window.h"
#include "core/engine.h"

namespace tpanar_ns {
class TPanarApp : public wxApp {
public:
    virtual bool OnInit() override;

    virtual void SetCLocale() override {}

    virtual bool Initialize(int& argc, wxChar** argv) override {
        setenv("LANG", "C.UTF-8", 1);
        setenv("LC_ALL", "C.UTF-8", 1);
        setlocale(LC_ALL, "C.UTF-8");
        
        bool ok = wxApp::Initialize(argc, argv);
        
        wxUpdateLocaleIsUtf8();
        wxConvCurrent = &wxConvUTF8;
        return ok;
    }

private:
    Engine* m_engine = nullptr;
    WxMainWindow* m_window = nullptr;
};

wxIMPLEMENT_APP(TPanarApp);

bool TPanarApp::OnInit() {
    if (!wxApp::OnInit())
        return false;

    try {
        wxInitAllImageHandlers();
        m_engine = new Engine();

        if (!m_engine->initialize()) {
            delete m_engine;
            return false;
        }
        m_window = new WxMainWindow(1280, 800, "TPanar", *m_engine);
        m_window->Show(true);

        return true;
    }
    catch (const std::exception& e) {
        wxMessageBox(wxString::Format("Fatal error: %s", e.what()), "Error", wxOK | wxICON_ERROR);
        return false;
    }
}

} // namespace tpanar_ns

int main(int argc, char** argv) {
    setenv("LANG", "C.UTF-8", 1);
    setenv("LC_ALL", "C.UTF-8", 1);
    setlocale(LC_ALL, "C.UTF-8");
    
    return wxEntry(argc, argv);
}
