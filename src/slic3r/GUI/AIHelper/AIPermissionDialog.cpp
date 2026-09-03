#include "AIPermissionDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/radiobut.h>
#include <wx/button.h>

namespace Slic3r { namespace GUI {

AIPermissionDialog::AIPermissionDialog(wxWindow* parent, PermissionLevel current_level)
    : DPIDialog(parent, wxID_ANY, _L("AI Assistant Privacy"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_selected_level(current_level)
{
    create_controls();
    SetSizerAndFit(m_main_sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AIPermissionDialog::create_controls() {
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* label = new wxStaticText(this, wxID_ANY,
        _L("The AI Assistant can help with slicing questions, troubleshooting, and settings suggestions.\n"
           "Choose how your data is processed:"));
    label->Wrap(FromDIP(500));
    main_sizer->Add(label, 0, wxALL | wxEXPAND, FromDIP(15));

    wxStaticBoxSizer* options_sizer = new wxStaticBoxSizer(wxVERTICAL, this, _L("Processing Mode"));

    m_radio_cloud = new wxRadioButton(this, wxID_ANY, _L("Cloud AI (recommended) — Send queries to configured AI provider (Gemini, OpenAI, etc.) for best results"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    m_radio_local = new wxRadioButton(this, wxID_ANY, _L("Local only — Use offline rule-based responses only (no network requests)"), wxDefaultPosition, wxDefaultSize);
    m_radio_deny = new wxRadioButton(this, wxID_ANY, _L("Disable AI Assistant — Do not show the AI panel"), wxDefaultPosition, wxDefaultSize);

    options_sizer->Add(m_radio_cloud, 0, wxALL | wxEXPAND, FromDIP(8));
    options_sizer->Add(m_radio_local, 0, wxALL | wxEXPAND, FromDIP(8));
    options_sizer->Add(m_radio_deny, 0, wxALL | wxEXPAND, FromDIP(8));

    switch (m_selected_level) {
        case PermissionLevel::AllowCloud: m_radio_cloud->SetValue(true); break;
        case PermissionLevel::AllowLocalOnly: m_radio_local->SetValue(true); break;
        case PermissionLevel::Deny: m_radio_deny->SetValue(true); break;
    }

    main_sizer->Add(options_sizer, 0, wxALL | wxEXPAND, FromDIP(15));

    wxStaticText* note = new wxStaticText(this, wxID_ANY,
        _L("You can change this anytime in Preferences > AI Assistant.\n"
           "API keys are stored locally in your configuration file."));
    note->Wrap(FromDIP(500));
    main_sizer->Add(note, 0, wxALL | wxEXPAND, FromDIP(15));

    wxStdDialogButtonSizer* btn_sizer = new wxStdDialogButtonSizer();
    wxButton* ok_btn = new wxButton(this, wxID_OK, _L("OK"));
    wxButton* cancel_btn = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btn_sizer->AddButton(ok_btn);
    btn_sizer->AddButton(cancel_btn);
    btn_sizer->Realize();

    main_sizer->Add(btn_sizer, 0, wxALL | wxALIGN_RIGHT, FromDIP(15));

    m_main_sizer = main_sizer;

    Bind(wxEVT_RADIOBUTTON, &AIPermissionDialog::on_allow_cloud, this, m_radio_cloud->GetId());
    Bind(wxEVT_RADIOBUTTON, &AIPermissionDialog::on_allow_local, this, m_radio_local->GetId());
    Bind(wxEVT_RADIOBUTTON, &AIPermissionDialog::on_deny, this, m_radio_deny->GetId());
    Bind(wxEVT_BUTTON, &AIPermissionDialog::on_ok, this, wxID_OK);
    Bind(wxEVT_BUTTON, &AIPermissionDialog::on_cancel, this, wxID_CANCEL);
}

void AIPermissionDialog::on_allow_cloud(wxCommandEvent&) { m_selected_level = PermissionLevel::AllowCloud; }
void AIPermissionDialog::on_allow_local(wxCommandEvent&) { m_selected_level = PermissionLevel::AllowLocalOnly; }
void AIPermissionDialog::on_deny(wxCommandEvent&) { m_selected_level = PermissionLevel::Deny; }
void AIPermissionDialog::on_ok(wxCommandEvent&) { EndModal(wxID_OK); }
void AIPermissionDialog::on_cancel(wxCommandEvent&) { EndModal(wxID_CANCEL); }

}} // namespace Slic3r::GUI