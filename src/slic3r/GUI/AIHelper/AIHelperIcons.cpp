#include "AIHelperIcons.hpp"
#include "GUI_App.hpp"

namespace Slic3r { namespace GUI {

wxBitmap* AIHelperIcons::m_ai_icon = nullptr;
wxBitmap* AIHelperIcons::m_ai_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_send_icon = nullptr;
wxBitmap* AIHelperIcons::m_send_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_stop_icon = nullptr;
wxBitmap* AIHelperIcons::m_stop_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_attachment_icon = nullptr;
wxBitmap* AIHelperIcons::m_attachment_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_settings_icon = nullptr;
wxBitmap* AIHelperIcons::m_settings_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_copy_icon = nullptr;
wxBitmap* AIHelperIcons::m_copy_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_regenerate_icon = nullptr;
wxBitmap* AIHelperIcons::m_regenerate_icon_dark = nullptr;
wxBitmap* AIHelperIcons::m_clear_icon = nullptr;
wxBitmap* AIHelperIcons::m_clear_icon_dark = nullptr;

void AIHelperIcons::init() {
    // Use wxArtProvider for built-in icons (no external assets needed)
    wxSize icon_size(FromDIP(24), FromDIP(24));
    m_ai_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_OTHER, icon_size));
    m_ai_icon_dark = new wxBitmap(*m_ai_icon);
    m_send_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_OTHER, icon_size));
    m_send_icon_dark = new wxBitmap(*m_send_icon);
    m_stop_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_OTHER, icon_size));
    m_stop_icon_dark = new wxBitmap(*m_stop_icon);
    m_attachment_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, icon_size));
    m_attachment_icon_dark = new wxBitmap(*m_attachment_icon);
    m_settings_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE, wxART_OTHER, icon_size));
    m_settings_icon_dark = new wxBitmap(*m_settings_icon);
    m_copy_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_COPY, wxART_OTHER, icon_size));
    m_copy_icon_dark = new wxBitmap(*m_copy_icon);
    m_regenerate_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_UNDO, wxART_OTHER, icon_size));
    m_regenerate_icon_dark = new wxBitmap(*m_regenerate_icon);
    m_clear_icon = new wxBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_OTHER, icon_size));
    m_clear_icon_dark = new wxBitmap(*m_clear_icon);
}

void AIHelperIcons::shutdown() {
    delete m_ai_icon; m_ai_icon = nullptr;
    delete m_ai_icon_dark; m_ai_icon_dark = nullptr;
    delete m_send_icon; m_send_icon = nullptr;
    delete m_send_icon_dark; m_send_icon_dark = nullptr;
    delete m_stop_icon; m_stop_icon = nullptr;
    delete m_stop_icon_dark; m_stop_icon_dark = nullptr;
    delete m_attachment_icon; m_attachment_icon = nullptr;
    delete m_attachment_icon_dark; m_attachment_icon_dark = nullptr;
    delete m_settings_icon; m_settings_icon = nullptr;
    delete m_settings_icon_dark; m_settings_icon_dark = nullptr;
    delete m_copy_icon; m_copy_icon = nullptr;
    delete m_copy_icon_dark; m_copy_icon_dark = nullptr;
    delete m_regenerate_icon; m_regenerate_icon = nullptr;
    delete m_regenerate_icon_dark; m_regenerate_icon_dark = nullptr;
    delete m_clear_icon; m_clear_icon = nullptr;
    delete m_clear_icon_dark; m_clear_icon_dark = nullptr;
}

const wxBitmap& AIHelperIcons::get_ai_icon(bool dark) { return dark && m_ai_icon_dark ? *m_ai_icon_dark : *m_ai_icon; }
const wxBitmap& AIHelperIcons::get_send_icon(bool dark) { return dark && m_send_icon_dark ? *m_send_icon_dark : *m_send_icon; }
const wxBitmap& AIHelperIcons::get_stop_icon(bool dark) { return dark && m_stop_icon_dark ? *m_stop_icon_dark : *m_stop_icon; }
const wxBitmap& AIHelperIcons::get_attachment_icon(bool dark) { return dark && m_attachment_icon_dark ? *m_attachment_icon_dark : *m_attachment_icon; }
const wxBitmap& AIHelperIcons::get_settings_icon(bool dark) { return dark && m_settings_icon_dark ? *m_settings_icon_dark : *m_settings_icon; }
const wxBitmap& AIHelperIcons::get_copy_icon(bool dark) { return dark && m_copy_icon_dark ? *m_copy_icon_dark : *m_copy_icon; }
const wxBitmap& AIHelperIcons::get_regenerate_icon(bool dark) { return dark && m_regenerate_icon_dark ? *m_regenerate_icon_dark : *m_regenerate_icon; }
const wxBitmap& AIHelperIcons::get_clear_icon(bool dark) { return dark && m_clear_icon_dark ? *m_clear_icon_dark : *m_clear_icon; }

}} // namespace Slic3r::GUI