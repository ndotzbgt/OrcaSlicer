#ifndef slic3r_AIHelper_hpp_
#define slic3r_AIHelper_hpp_

#include <wx/panel.h>
#include <wx/webview.h>
#include <wx/timer.h>
#include <memory>
#include <string>
#include <vector>

#include "AIEngine.hpp"
#include "AISessionManager.hpp"
#include "AIContext.hpp"

namespace Slic3r { namespace GUI {

class AIHelper : public wxPanel
{
public:
    AIHelper(wxWindow* parent);
    ~AIHelper() override;

    void set_visible(bool visible);
    bool is_visible() const;

    void toggle_visibility();

    void set_engine(std::shared_ptr<AIEngineBase> engine);
    std::shared_ptr<AIEngineBase> get_engine() const { return m_engine; }

    void send_message(const std::string& text, const std::string& image_base64 = "");
    void abort_generation();

    void on_project_changed(const std::string& project_path);
    void on_settings_changed();

    void show_permission_dialog();
    void show_action_confirm(const AIActionConfirmDialog::Action& action);

private:
    void create_controls();
    void load_chat_html();
    void append_message(const std::string& role, const std::string& content, bool is_streaming = false);
    void update_streaming_message(const std::string& token);
    void finalize_streaming_message();
    void on_webview_loaded(wxWebViewEvent& event);
    void on_webview_script_message(wxWebViewEvent& event);
    void on_send(wxCommandEvent& event);
    void on_abort(wxCommandEvent& event);
    void on_attachment(wxCommandEvent& event);
    void on_clear(wxCommandEvent& event);
    void on_key_down(wxKeyEvent& event);
    void on_timer(wxTimerEvent& event);
    void apply_dark_mode();

    void inject_context();

    wxWebView* m_webview;
    wxTextCtrl* m_input;
    wxButton* m_send_btn;
    wxButton* m_abort_btn;
    wxButton* m_attachment_btn;
    wxButton* m_clear_btn;
    wxBoxSizer* m_main_sizer;
    wxBoxSizer* m_input_sizer;

    std::shared_ptr<AIEngineBase> m_engine;
    AISessionManager m_session_mgr;
    AIContextData m_context;

    bool m_streaming{false};
    std::string m_pending_tokens;
    std::string m_stream_buffer;
    wxTimer* m_stream_timer;
    bool m_dark_mode{false};

    wxDECLARE_EVENT_TABLE();
};

wxDECLARE_EVENT(EVT_AI_HELPER_TOGGLE, wxCommandEvent);
wxDECLARE_EVENT(EVT_AI_CHAT_START, wxCommandEvent);
wxDECLARE_EVENT(EVT_AI_CHAT_END, wxCommandEvent);
wxDECLARE_EVENT(EVT_AI_CHAT_MESSAGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_AI_ACTION_CONFIRM, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif // slic3r_AIHelper_hpp_