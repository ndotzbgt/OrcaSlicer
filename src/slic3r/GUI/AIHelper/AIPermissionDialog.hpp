#ifndef slic3r_AIPermissionDialog_hpp_
#define slic3r_AIPermissionDialog_hpp_

#include <wx/dialog.h>
#include "../GUI_Utils.hpp"

namespace Slic3r { namespace GUI {

class AIPermissionDialog : public DPIDialog
{
public:
    enum class PermissionLevel {
        Deny,
        AllowCloud,
        AllowLocalOnly,
    };

    AIPermissionDialog(wxWindow* parent, PermissionLevel current_level = PermissionLevel::Deny);
    ~AIPermissionDialog() = default;

    PermissionLevel get_selected_level() const { return m_selected_level; }

private:
    void create_controls();
    void on_allow_cloud(wxCommandEvent&);
    void on_allow_local(wxCommandEvent&);
    void on_deny(wxCommandEvent&);
    void on_ok(wxCommandEvent&);
    void on_cancel(wxCommandEvent&);

    PermissionLevel m_selected_level;
    wxRadioButton* m_radio_cloud;
    wxRadioButton* m_radio_local;
    wxRadioButton* m_radio_deny;
};

}} // namespace Slic3r::GUI

#endif // slic3r_AIPermissionDialog_hpp_