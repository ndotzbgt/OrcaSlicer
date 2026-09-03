#ifndef slic3r_AIHelperIcons_hpp_
#define slic3r_AIHelperIcons_hpp_

#include <wx/bitmap.h>
#include <wx/image.h>

namespace Slic3r { namespace GUI {

class AIHelperIcons
{
public:
    static void init();
    static void shutdown();

    static const wxBitmap& get_ai_icon(bool dark = false);
    static const wxBitmap& get_send_icon(bool dark = false);
    static const wxBitmap& get_stop_icon(bool dark = false);
    static const wxBitmap& get_attachment_icon(bool dark = false);
    static const wxBitmap& get_settings_icon(bool dark = false);
    static const wxBitmap& get_copy_icon(bool dark = false);
    static const wxBitmap& get_regenerate_icon(bool dark = false);
    static const wxBitmap& get_clear_icon(bool dark = false);

private:
    static wxBitmap* m_ai_icon;
    static wxBitmap* m_ai_icon_dark;
    static wxBitmap* m_send_icon;
    static wxBitmap* m_send_icon_dark;
    static wxBitmap* m_stop_icon;
    static wxBitmap* m_stop_icon_dark;
    static wxBitmap* m_attachment_icon;
    static wxBitmap* m_attachment_icon_dark;
    static wxBitmap* m_settings_icon;
    static wxBitmap* m_settings_icon_dark;
    static wxBitmap* m_copy_icon;
    static wxBitmap* m_copy_icon_dark;
    static wxBitmap* m_regenerate_icon;
    static wxBitmap* m_regenerate_icon_dark;
    static wxBitmap* m_clear_icon;
    static wxBitmap* m_clear_icon_dark;
};

}} // namespace Slic3r::GUI

#endif // slic3r_AIHelperIcons_hpp_