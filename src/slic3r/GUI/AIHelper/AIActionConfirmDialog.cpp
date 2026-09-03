#include "AIActionConfirmDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/statline.h>

namespace Slic3r { namespace GUI {

AIActionConfirmDialog::AIActionConfirmDialog(wxWindow* parent, const Action& action)
    : DPIDialog(parent, wxID_ANY, _L("Confirm AI Suggestion"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_action(action),
      m_confirmed(false)
{
    create_controls();
    SetSizerAndFit(m_main_sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AIActionConfirmDialog::create_controls() {
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* label = new wxStaticText(this, wxID_ANY,
        _L("The AI Assistant suggests the following change:"));
    main_sizer->Add(label, 0, wxALL | wxEXPAND, FromDIP(15));

    wxStaticBoxSizer* detail_sizer = new wxStaticBoxSizer(wxVERTICAL, this, _L("Suggested Change"));

    wxString desc = from_u8(m_action.description);
    wxStaticText* desc_text = new wxStaticText(this, wxID_ANY, desc);
    desc_text->Wrap(FromDIP(450));
    detail_sizer->Add(desc_text, 0, wxALL | wxEXPAND, FromDIP(10));

    if (!m_action.setting_key.empty() && !m_action.current_value.empty()) {
        wxStaticText* current = new wxStaticText(this, wxID_ANY,
            _L("Current: ") + from_u8(m_action.current_value));
        current->SetForegroundColour(wxGetApp().get_label_clr_sys());
        detail_sizer->Add(current, 0, wxALL | wxEXPAND, FromDIP(10));
    }

    if (!m_action.suggested_value.empty()) {
        wxStaticText* suggested = new wxStaticText(this, wxID_ANY,
            _L("Suggested: ") + from_u8(m_action.suggested_value));
        suggested->SetForegroundColour(wxColour(0, 150, 0));
        detail_sizer->Add(suggested, 0, wxALL | wxEXPAND, FromDIP(10));
    }

    main_sizer->Add(detail_sizer, 0, wxALL | wxEXPAND, FromDIP(15));

    wxStaticText* warning = new wxStaticText(this, wxID_ANY,
        _L("This change will be applied to your current print settings. You can undo it afterwards."));
    warning->Wrap(FromDIP(450));
    main_sizer->Add(warning, 0, wxALL | wxEXPAND, FromDIP(15));

    wxStdDialogButtonSizer* btn_sizer = new wxStdDialogButtonSizer();
    wxButton* apply_btn = new wxButton(this, wxID_APPLY, _L("Apply"));
    wxButton* dismiss_btn = new wxButton(this, wxID_CANCEL, _L("Dismiss"));
    btn_sizer->AddButton(apply_btn);
    btn_sizer->AddButton(dismiss_btn);
    btn_sizer->Realize();

    main_sizer->Add(btn_sizer, 0, wxALL | wxALIGN_RIGHT, FromDIP(15));

    m_main_sizer = main_sizer;

    Bind(wxEVT_BUTTON, &AIActionConfirmDialog::on_apply, this, wxID_APPLY);
    Bind(wxEVT_BUTTON, &AIActionConfirmDialog::on_dismiss, this, wxID_CANCEL);
}

void AIActionConfirmDialog::on_apply(wxCommandEvent&) {
    m_confirmed = true;
    EndModal(wxID_APPLY);
}

void AIActionConfirmDialog::on_dismiss(wxCommandEvent&) {
    m_confirmed = false;
    EndModal(wxID_CANCEL);
}

}} // namespace Slic3r::GUI