#ifndef slic3r_AIActionConfirmDialog_hpp_
#define slic3r_AIActionConfirmDialog_hpp_

#include <wx/dialog.h>
#include "../GUI_Utils.hpp"

namespace Slic3r { namespace GUI {

class AIActionConfirmDialog : public DPIDialog
{
public:
    struct Action {
        std::string type;
        std::string description;
        std::string setting_key;
        std::string current_value;
        std::string suggested_value;
    };

    AIActionConfirmDialog(wxWindow* parent, const Action& action);
    ~AIActionConfirmDialog() = default;

    bool user_confirmed() const { return m_confirmed; }

private:
    void create_controls();
    void on_apply(wxCommandEvent&);
    void on_dismiss(wxCommandEvent&);
    void on_dpi_changed(const wxRect& suggested_rect) override;

    Action m_action;
    bool m_confirmed{false};
    wxSizer* m_main_sizer;
};

}} // namespace Slic3r::GUI

#endif // slic3r_AIActionConfirmDialog_hpp_