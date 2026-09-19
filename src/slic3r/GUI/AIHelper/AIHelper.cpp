#include "AIHelper.hpp"
#include "AIPermissionDialog.hpp"
#include "AIActionConfirmDialog.hpp"
#include "AIHelperIcons.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "I18N.hpp"
#include "GUI.hpp"
#include "Widgets/WebView.hpp"
#include "slic3r/Utils/Http.hpp"
#include "GLCanvas3D.hpp"
#include "WxFontUtils.hpp"

#include <nlohmann/json.hpp>

#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/clipbrd.h>
#include <wx/timer.h>
#include <wx/display.h>
#include <wx/msgdlg.h>
#include <wx/busyinfo.h>
#include <wx/image.h>
#include <wx/dcmemory.h>
#include <wx/webview.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

using namespace nlohmann;

namespace Slic3r { namespace GUI {

wxBEGIN_EVENT_TABLE(AIHelper, wxPanel)
    EVT_WEBVIEW_LOADED(wxID_ANY, AIHelper::on_webview_loaded)
    EVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED(wxID_ANY, AIHelper::on_webview_script_message)
    EVT_BUTTON(wxID_ANY, AIHelper::on_send)
    EVT_BUTTON(wxID_ANY, AIHelper::on_abort)
    EVT_BUTTON(wxID_ANY, AIHelper::on_attachment)
    EVT_BUTTON(wxID_ANY, AIHelper::on_clear)
    EVT_KEY_DOWN(AIHelper::on_key_down)
    EVT_TIMER(wxID_ANY, AIHelper::on_timer)
wxEND_EVENT_TABLE()

AIHelper::AIHelper(wxWindow* parent)
    : wxPanel(parent, wxID_ANY),
      m_streaming(false),
      m_stream_timer(new wxTimer(this)),
      m_dark_mode(false)
{
    create_controls();
    AIHelperIcons::init();
    load_chat_html();
    apply_dark_mode();

    Bind(wxEVT_TIMER, &AIHelper::on_timer, this, m_stream_timer->GetId());
}

AIHelper::~AIHelper() {
    delete m_stream_timer;
}

void AIHelper::create_controls() {
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    m_webview = WebView::CreateWebView(this, "");
    if (!m_webview) {
        // Fallback UI for when WebView is not available
        wxStaticText* notice = new wxStaticText(this, wxID_ANY,
            _L("WebView not available. Please install WebView2 (Windows) or WebKitGTK (Linux).\n\n"
               "Offline mode still works: Preferences \u2192 AI Assistant \u2192 Processing Mode \u2192 Local Only"));
        notice->Wrap(FromDIP(500));
        m_main_sizer->Add(notice, 1, wxALL | wxEXPAND | wxALIGN_CENTER, FromDIP(20));

        m_input_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_input = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
            wxTE_PROCESS_ENTER | wxTE_MULTILINE | wxTE_READONLY);
        m_input->SetMinSize(FromDIP(wxSize(-1, 44)));
        m_input->SetMaxSize(FromDIP(wxSize(-1, 150)));
        m_input->SetFont(wxGetApp().normal_font());
        m_input_sizer->Add(m_input, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);
        m_main_sizer->Add(m_input_sizer, 0, wxEXPAND | wxALL, FromDIP(8));

        SetSizer(m_main_sizer);
        Layout();
        return;
    }

    m_webview->Bind(wxEVT_WEBVIEW_LOADED, &AIHelper::on_webview_loaded, this);
    m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &AIHelper::on_webview_script_message, this);

    m_main_sizer->Add(m_webview, 1, wxEXPAND | wxALL, 0);

    m_input_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_attachment_btn = new wxButton(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(36, 36)), wxBU_EXACTFIT);
    m_attachment_btn->SetBitmap(AIHelperIcons::get_attachment_icon(m_dark_mode));
    m_attachment_btn->SetToolTip(_L("Attach image (screenshot, file, or URL)"));
    m_input_sizer->Add(m_attachment_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

    m_input = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
        wxTE_PROCESS_ENTER | wxTE_MULTILINE | wxTE_DONTWRAP);
    m_input->SetMinSize(FromDIP(wxSize(-1, 44)));
    m_input->SetMaxSize(FromDIP(wxSize(-1, 150)));
    m_input->SetFont(wxGetApp().normal_font());
    m_input_sizer->Add(m_input, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, FromDIP(8));

    m_send_btn = new wxButton(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(36, 36)), wxBU_EXACTFIT);
    m_send_btn->SetBitmap(AIHelperIcons::get_send_icon(m_dark_mode));
    m_send_btn->SetToolTip(_L("Send (Enter)"));
    m_input_sizer->Add(m_send_btn, 0, wxALIGN_CENTER_VERTICAL);

    m_abort_btn = new wxButton(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(36, 36)), wxBU_EXACTFIT);
    m_abort_btn->SetBitmap(AIHelperIcons::get_stop_icon(m_dark_mode));
    m_abort_btn->SetToolTip(_L("Stop generation"));
    m_abort_btn->Hide();
    m_input_sizer->Add(m_abort_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

    m_clear_btn = new wxButton(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(36, 36)), wxBU_EXACTFIT);
    m_clear_btn->SetBitmap(AIHelperIcons::get_clear_icon(m_dark_mode));
    m_clear_btn->SetToolTip(_L("Clear conversation"));
    m_input_sizer->Add(m_clear_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

    m_main_sizer->Add(m_input_sizer, 0, wxEXPAND | wxALL, FromDIP(8));

    SetSizer(m_main_sizer);
    Layout();
}

void AIHelper::load_chat_html() {
    if (!m_webview) return;

    std::string html = R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 14px; line-height: 1.6; padding: 16px; overflow-y: auto; }
        .message { margin-bottom: 16px; display: flex; gap: 8px; animation: fadeIn 0.2s ease; }
        @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }
        .avatar { width: 32px; height: 32px; border-radius: 50%; flex-shrink: 0; display: flex; align-items: center; justify-content: center; font-size: 14px; }
        .user .avatar { background: #009688; color: white; }
        .assistant .avatar { background: #424242; color: white; }
        .fallback .avatar { background: #FF9800; color: white; }
        .content { flex: 1; min-width: 0; }
        .role { font-weight: 600; font-size: 12px; margin-bottom: 4px; }
        .user .role { color: #009688; }
        .assistant .role { color: #757575; }
        .fallback .role { color: #FF9800; }
        .text { white-space: pre-wrap; word-wrap: break-word; }
        .user .text { color: #212121; }
        .assistant .text, .fallback .text { color: #424242; }
        .streaming .text::after { content: "▋"; animation: blink 1s infinite; color: #999; margin-left: 2px; }
        @keyframes blink { 0%, 50% { opacity: 1; } 51%, 100% { opacity: 0; } }
        .image-preview { max-width: 100%; max-height: 200px; border-radius: 8px; margin-top: 8px; }
        .actions { display: flex; gap: 8px; margin-top: 8px; opacity: 0; transition: opacity 0.2s; }
        .message:hover .actions { opacity: 1; }
        .action-btn { background: none; border: none; color: #999; cursor: pointer; font-size: 12px; padding: 4px 8px; border-radius: 4px; }
        .action-btn:hover { background: #f0f0f0; color: #333; }
        .dark .action-btn:hover { background: #424242; color: #fff; }
        .dark body { background: #1e1e1e; }
        .dark .user .text { color: #e0e0e0; }
        .dark .assistant .text, .dark .fallback .text { color: #b0b0b0; }
        .dark .assistant .avatar { background: #616161; }
        .dark .action-btn { color: #888; }
        .action-row { display: flex; gap: 8px; margin-top: 8px; }
        .suggestion-chip { background: #e3f2fd; color: #1565c0; padding: 4px 12px; border-radius: 16px; font-size: 12px; cursor: pointer; border: none; }
        .dark .suggestion-chip { background: #1a237e; color: #90caf9; }
        .typing-indicator { display: flex; gap: 4px; padding: 8px 0; }
        .typing-indicator span { width: 8px; height: 8px; background: #999; border-radius: 50%; animation: typing 1.4s infinite ease-in-out; }
        .typing-indicator span:nth-child(2) { animation-delay: 0.2s; }
        .typing-indicator span:nth-child(3) { animation-delay: 0.4s; }
        @keyframes typing { 0%, 60%, 100% { transform: translateY(0); } 30% { transform: translateY(-6px); } }
        pre { background: #f5f5f5; padding: 12px; border-radius: 8px; overflow-x: auto; font-size: 13px; }
        .dark pre { background: #2d2d2d; }
        code { background: #f5f5f5; padding: 2px 6px; border-radius: 4px; font-family: monospace; font-size: 13px; }
        .dark code { background: #2d2d2d; }
        pre code { background: none; padding: 0; }
        blockquote { border-left: 3px solid #009688; padding-left: 12px; color: #666; margin: 8px 0; }
        .dark blockquote { color: #aaa; }
        ul, ol { margin: 8px 0 8px 24px; }
        a { color: #009688; text-decoration: none; }
        a:hover { text-decoration: underline; }
        img { max-width: 100%; height: auto; border-radius: 8px; }
        hr { border: none; border-top: 1px solid #eee; margin: 16px 0; }
        .dark hr { border-top: 1px solid #333; }
        table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 13px; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        .dark th, .dark td { border: 1px solid #444; }
        th { background: #f5f5f5; }
        .dark th { background: #2d2d2d; }
    </style>
</head>
<body>
    <div id="messages"></div>
    <div id="typing" class="typing-indicator" style="display:none;"><span></span><span></span><span></span></div>
    <script>
        let currentStreamingId = null;

        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }

        function postMessage(data) {
            const msg = JSON.stringify(data);
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(msg);
            } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.wx) {
                window.webkit.messageHandlers.wx.postMessage(msg);
            }
        }

        function renderMarkdown(text) {
            return text
                .replace(/```(\w+)?\n([\s\S]*?)```/g, '<pre><code class="lang-$1">$2</code></pre>')
                .replace(/`([^`]+)`/g, '<code>$1</code>')
                .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
                .replace(/\*(.+?)\*/g, '<em>$1</em>')
                .replace(/^### (.+)$/gm, '<h3>$1</h3>')
                .replace(/^## (.+)$/gm, '<h2>$1</h2>')
                .replace(/^# (.+)$/gm, '<h1>$1</h1>')
                .replace(/^> (.+)$/gm, '<blockquote>$1</blockquote>')
                .replace(/^\- (.+)$/gm, '<li>$1</li>')
                .replace(/^\d+\. (.+)$/gm, '<li>$1</li>')
                .replace(/(<li>.*<\/li>)/s, '<ul>$1</ul>')
                .replace(/\n/g, '<br>');
        }

        function addMessage(role, content, streaming = false) {
            const container = document.getElementById('messages');
            const id = 'msg-' + Date.now() + '-' + Math.random().toString(36).substr(2, 9);
            if (streaming) currentStreamingId = id;

            const cls = role === 'user' ? 'user' : (role === 'fallback' ? 'fallback' : 'assistant');
            const roleName = role === 'user' ? 'You' : (role === 'fallback' ? 'Assistant (Offline)' : 'Assistant');

            const div = document.createElement('div');
            div.id = id;
            div.className = 'message ' + cls + (streaming ? ' streaming' : '');
            div.innerHTML = `
                <div class="avatar">${role === 'user' ? 'U' : 'AI'}</div>
                <div class="content">
                    <div class="role">${escapeHtml(roleName)}</div>
                    <div class="text">${streaming ? escapeHtml(content) : renderMarkdown(content)}</div>
                    <div class="actions">
                        <button class="action-btn" onclick="copyText('${id}')">Copy</button>
                        <button class="action-btn" onclick="regenerate('${id}')">Regenerate</button>
                    </div>
                </div>
            `;
            container.appendChild(div);
            container.scrollTop = container.scrollHeight;
            return id;
        }

        function updateStreamingMessage(token) {
            if (!currentStreamingId) return;
            const el = document.getElementById(currentStreamingId);
            if (!el) return;
            const textEl = el.querySelector('.text');
            if (textEl) {
                textEl.textContent += token;
            }
            const container = document.getElementById('messages');
            container.scrollTop = container.scrollHeight;
        }

        function finalizeStreamingMessage() {
            if (!currentStreamingId) return;
            const el = document.getElementById(currentStreamingId);
            if (!el) return;
            el.classList.remove('streaming');
            const textEl = el.querySelector('.text');
            if (textEl) {
                textEl.innerHTML = renderMarkdown(textEl.textContent);
            }
            currentStreamingId = null;
        }

        function showTyping(show) {
            document.getElementById('typing').style.display = show ? 'flex' : 'none';
        }

        function copyText(id) {
            const el = document.getElementById(id);
            if (!el) return;
            const text = el.querySelector('.text').innerText;
            navigator.clipboard.writeText(text);
        }

        function regenerate(id) {
            postMessage({ type: 'regenerate', messageId: id });
        }

        function clearAll() {
            document.getElementById('messages').innerHTML = '';
            postMessage({ type: 'clear' });
        }

        window.addEventListener('message', event => {
            if (event.data.type === 'setTheme') {
                document.body.classList.toggle('dark', event.data.dark);
            }
        });

        document.addEventListener('keydown', e => {
            if (e.key === 'Enter' && !e.shiftKey && e.target.tagName !== 'TEXTAREA') {
                e.preventDefault();
            }
        });

        // Expose sendMessage for input handling
        window.sendUserMessage = function(text) {
            postMessage({ type: 'user', text: text });
        };
    </script>
</body>
</html>
)html";

    m_webview->SetPage(from_u8(html), "");
}

void AIHelper::set_visible(bool visible) {
    Show(visible);
    Layout();
    if (GetParent()) GetParent()->Layout();
}

bool AIHelper::is_visible() const {
    return IsShown();
}

void AIHelper::toggle_visibility() {
    set_visible(!is_visible());
}

void AIHelper::set_engine(std::shared_ptr<AIEngineBase> engine) {
    m_engine = engine;
    if (m_engine) {
        inject_context();
    }
}

void AIHelper::send_message(const std::string& text, const std::string& image_base64) {
    if (text.empty() && image_base64.empty()) return;
    if (!m_engine) {
        wxMessageBox(_L("No AI backend configured. Please configure in Preferences > AI Assistant."), _L("AI Assistant"));
        return;
    }

    AISessionManager::Message user_msg;
    user_msg.role = "user";
    user_msg.content = text;
    user_msg.image_base64 = image_base64;
    user_msg.timestamp = wxDateTime::Now().FormatISOCombined(' ').ToStdString();
    m_session_mgr.add_message(user_msg);

    append_message("user", text, false);
    if (!image_base64.empty()) {
        std::string script = "addImagePreview('" + currentStreamingId + "', '" + image_base64 + "');";
        WebView::RunScript(m_webview, from_u8(script));
    }

    showTyping(true);
    m_streaming = true;
    m_send_btn->Hide();
    m_abort_btn->Show();
    m_input_sizer->Layout();

    std::vector<AIRequest> history;
    std::vector<AIRequest> messages;
    for (const auto& msg : m_session_mgr.get_messages()) {
        AIRequest req;
        req.role = msg.role;
        req.content = msg.content;
        req.image_url = msg.image_base64;
        if (msg.role == "user") {
            messages.push_back(req);
        } else {
            history.push_back(req);
        }
    }

    m_engine->chat(history, messages,
        [this](const AIChunk& chunk) {
            wxGetApp().CallAfter([this, chunk]() {
                if (chunk.done) {
                    finalize_streaming_message();
                    showTyping(false);
                    m_streaming = false;
                    m_send_btn->Show();
                    m_abort_btn->Hide();
                    m_input_sizer->Layout();
                } else {
                    update_streaming_message(chunk.text);
                }
            });
        },
        [this](const std::string& error) {
            wxGetApp().CallAfter([this, error]() {
                showTyping(false);
                m_streaming = false;
                m_send_btn->Show();
                m_abort_btn->Hide();
                m_input_sizer->Layout();
                append_message("assistant", _L("Error: ") + error, false);
                wxMessageBox(from_u8(error), _L("AI Assistant Error"));
            });
        },
        [this]() {
            wxGetApp().CallAfter([this]() {
                showTyping(false);
                m_streaming = false;
                m_send_btn->Show();
                m_abort_btn->Hide();
                m_input_sizer->Layout();
            });
        });
}

void AIHelper::abort_generation() {
    if (m_engine) {
        m_engine->abort();
    }
    showTyping(false);
    m_streaming = false;
    m_send_btn->Show();
    m_abort_btn->Hide();
    m_input_sizer->Layout();
}

void AIHelper::on_project_changed(const std::string& project_path) {
    m_session_mgr.set_project_path(project_path);
    load_chat_html();
    for (const auto& msg : m_session_mgr.get_messages()) {
        append_message(msg.role, msg.content, false);
    }
    inject_context();
}

void AIHelper::on_settings_changed() {
    if (m_engine) {
        inject_context();
    }
}

void AIHelper::show_permission_dialog() {
    AIPermissionDialog dlg(this);
    if (dlg.ShowModal() == wxID_OK) {
        auto level = dlg.get_selected_level();
        wxGetApp().app_config->set("ai", "permission_level",
            level == AIPermissionDialog::PermissionLevel::AllowCloud ? "cloud" :
            level == AIPermissionDialog::PermissionLevel::AllowLocalOnly ? "local" : "deny");
        wxGetApp().app_config->save();
    }
}

void AIHelper::show_action_confirm(const AIActionConfirmDialog::Action& action) {
    AIActionConfirmDialog dlg(this, action);
    if (dlg.ShowModal() == wxID_APPLY && dlg.user_confirmed()) {
    }
}

void AIHelper::append_message(const std::string& role, const std::string& content, bool is_streaming) {
    if (!m_webview) return;
    std::string escaped = content;
    boost::replace_all(escaped, "\\", "\\\\");
    boost::replace_all(escaped, "'", "\\'");
    boost::replace_all(escaped, "\n", "\\n");
    boost::replace_all(escaped, "\r", "");
    boost::replace_all(escaped, "\"", "\\\"");
    std::string script = "addMessage('" + role + "', '" + escaped + "', " + (is_streaming ? "true" : "false") + ");";
    WebView::RunScript(m_webview, from_u8(script));
}

void AIHelper::update_streaming_message(const std::string& token) {
    if (!m_webview) return;
    // Buffer tokens for batching (flushed in on_timer)
    m_stream_buffer += token;
    if (m_stream_buffer.size() > 100) {
        // Flush immediately if buffer exceeds threshold
        on_timer(wxTimerEvent(*m_stream_timer));
    }
}

void AIHelper::finalize_streaming_message() {
    if (!m_webview) return;
    WebView::RunScript(m_webview, "finalizeStreamingMessage();");
}

void AIHelper::showTyping(bool show) {
    if (!m_webview) return;
    WebView::RunScript(m_webview, std::string("showTyping(") + (show ? "true" : "false") + ");");
}

void AIHelper::inject_context() {
    if (!m_webview || !m_engine) return;
    m_context = AIContext::build_from_current_project();
    std::string context = AIContext::format_for_prompt(m_context);
    if (!context.empty()) {
        std::string script = "setContext('" + context + "');";
        WebView::RunScript(m_webview, from_u8(script));
    }
}

void AIHelper::on_webview_loaded(wxWebViewEvent& event) {
    apply_dark_mode();
}

void AIHelper::on_webview_script_message(wxWebViewEvent& event) {
    wxString msg = event.GetString();
    if (msg.empty()) return;

    // Parse JSON message from webview
    using json = nlohmann::json;
    try {
        json j = json::parse(msg.ToStdString());
        std::string type = j.value("type", "");

        if (type == "user") {
            std::string text = j.value("text", "");
            send_message(text);
            m_input->Clear();
        } else if (type == "clear") {
            m_session_mgr.clear();
        } else if (type == "regenerate") {
            // TODO: implement regenerate
        } else if (type == "copy") {
            std::string text = j.value("text", "");
            if (wxTheClipboard->Open()) {
                wxTheClipboard->SetData(new wxTextDataObject(from_u8(text)));
                wxTheClipboard->Close();
            }
        }
    } catch (...) {
        // Fallback for old format
        if (msg.StartsWith("user:")) {
            std::string text = msg.AfterFirst(':').ToStdString();
            send_message(text);
            m_input->Clear();
        } else if (msg == "clear") {
            m_session_mgr.clear();
        } else if (msg.StartsWith("copy:")) {
            if (wxTheClipboard->Open()) {
                wxTheClipboard->SetData(new wxTextDataObject(msg.AfterFirst(':')));
                wxTheClipboard->Close();
            }
        }
    }
}

void AIHelper::on_send(wxCommandEvent& event) {
    std::string text = m_input->GetValue().ToStdString();
    if (!text.empty()) {
        send_message(text);
        m_input->Clear();
    }
}

void AIHelper::on_abort(wxCommandEvent& event) {
    abort_generation();
}

void AIHelper::on_attachment(wxCommandEvent& event) {
    wxMenu menu;
    menu.Append(wxID_ANY, _L("Screenshot"));
    menu.Append(wxID_ANY, _L("From File..."));
    menu.Append(wxID_ANY, _L("From URL..."));
    PopupMenu(&menu, m_attachment_btn->GetPosition() + wxPoint(0, m_attachment_btn->GetSize().GetHeight()));
    menu.Bind(wxEVT_MENU, [this, &menu](wxCommandEvent& evt) {
        int id = evt.GetId();
        if (id == menu.FindItem(_L("Screenshot"))) {
            take_screenshot();
        } else if (id == menu.FindItem(_L("From File..."))) {
            wxFileDialog dlg(this, _L("Select Image"), "", "", "Image files (*.png;*.jpg;*.jpeg;*.gif)|*.png;*.jpg;*.jpeg;*.gif", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() == wxID_OK) {
                load_image_file(dlg.GetPath().ToStdString());
            }
        } else if (id == menu.FindItem(_L("From URL..."))) {
            wxTextEntryDialog dlg(this, _L("Enter image URL:"), _L("Attach Image from URL"));
            if (dlg.ShowModal() == wxID_OK) {
                load_image_url(dlg.GetValue().ToStdString());
            }
        }
    });
}

void AIHelper::on_clear(wxCommandEvent& event) {
    if (wxMessageBox(_L("Clear conversation history?"), _L("Clear"), wxYES_NO | wxICON_QUESTION) == wxYES) {
        m_session_mgr.clear();
        if (m_webview) {
            WebView::RunScript(m_webview, "clearAll();");
        }
    }
}

void AIHelper::on_key_down(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_RETURN && !event.ShiftDown() && event.GetEventObject() == m_input) {
        on_send(event);
    } else {
        event.Skip();
    }
}

void AIHelper::on_timer(wxTimerEvent& event) {
    // Token batching: flush buffered tokens every 50ms or when buffer exceeds 100 chars
    if (!m_stream_buffer.empty()) {
        std::string script = "updateStreamingMessage('" + escape_js(m_stream_buffer) + "');";
        WebView::RunScript(m_webview, from_u8(script));
        m_stream_buffer.clear();
    }
}

static std::string escape_js(const std::string& s) {
    std::string escaped = s;
    boost::replace_all(escaped, "\\", "\\\\");
    boost::replace_all(escaped, "'", "\\'");
    boost::replace_all(escaped, "\n", "\\n");
    boost::replace_all(escaped, "\r", "");
    boost::replace_all(escaped, "\"", "\\\"");
    return escaped;
}

void AIHelper::apply_dark_mode() {
    m_dark_mode = wxGetApp().dark_mode();
    if (m_webview) {
        WebView::RunScript(m_webview, std::string("document.body.classList.toggle('dark', ") + (m_dark_mode ? "true" : "false") + ");");
    }
    if (m_attachment_btn) m_attachment_btn->SetBitmap(AIHelperIcons::get_attachment_icon(m_dark_mode));
    if (m_send_btn) m_send_btn->SetBitmap(AIHelperIcons::get_send_icon(m_dark_mode));
    if (m_abort_btn) m_abort_btn->SetBitmap(AIHelperIcons::get_stop_icon(m_dark_mode));
    if (m_clear_btn) m_clear_btn->SetBitmap(AIHelperIcons::get_clear_icon(m_dark_mode));
    Refresh();
}

void AIHelper::take_screenshot() {
    if (auto* frame = wxGetApp().mainframe) {
        if (auto* plater = frame->plater()) {
            if (auto* canvas = plater->get_3dcanvas()) {
                wxSize sz = canvas->GetSize();
                wxBitmap bmp(sz);
                
                wxGLContext* ctx = canvas->GetRenderContext();
                if (!ctx || !ctx->SetCurrent(*canvas)) {
                    wxMessageBox(_L("Failed to acquire GL context for screenshot"), _L("Error"));
                    return;
                }

                std::vector<unsigned char> pixels(sz.GetWidth() * sz.GetHeight() * 4);
                glReadPixels(0, 0, sz.GetWidth(), sz.GetHeight(), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

                // Flip vertically since OpenGL origin is bottom-left
                for (int y = 0; y < sz.GetHeight() / 2; ++y) {
                    size_t row1 = y * sz.GetWidth() * 4;
                    size_t row2 = (sz.GetHeight() - 1 - y) * sz.GetWidth() * 4;
                    std::swap_ranges(pixels.begin() + row1, pixels.begin() + row1 + sz.GetWidth() * 4, pixels.begin() + row2);
                }

                wxImage img(sz.GetWidth(), sz.GetHeight(), pixels.data(), true);
                wxMemoryBuffer buf;
                wxPNGHandler handler;
                if (!handler.SaveFile(&img, buf, wxBITMAP_TYPE_PNG)) {
                    wxMessageBox(_L("Failed to encode screenshot"), _L("Error"));
                    return;
                }

                std::string base64;
                base64.resize(4 * ((buf.GetDataLen() + 2) / 3));
                size_t out_len = 0;
                wxBase64Encode((char*)base64.data(), buf.GetDataLen(), (const char*)buf.GetData(), buf.GetDataLen(), &out_len);
                base64.resize(out_len);

                send_message("", base64);
            }
        }
    }
}

void AIHelper::load_image_file(const std::string& path) {
    wxImage img(path);
    if (!img.IsOk()) {
        wxMessageBox(_L("Failed to load image"), _L("Error"));
        return;
    }
    if (img.GetWidth() > 1024 || img.GetHeight() > 1024) {
        img.Rescale(std::min(1024, img.GetWidth()), std::min(1024, img.GetHeight()));
    }
    wxMemoryBuffer buf;
    wxPNGHandler handler;
    if (!handler.SaveFile(&img, buf, wxBITMAP_TYPE_PNG)) {
        wxMessageBox(_L("Failed to encode image"), _L("Error"));
        return;
    }

    std::string base64;
    base64.resize(4 * ((buf.GetDataLen() + 2) / 3));
    size_t out_len = 0;
    wxBase64Encode((char*)base64.data(), buf.GetDataLen(), (const char*)buf.GetData(), buf.GetDataLen(), &out_len);
    base64.resize(out_len);

    send_message("", base64);
}

void AIHelper::load_image_url(const std::string& url) {
    if (!m_engine) return;
    wxBusyInfo wait(_L("Downloading image..."));
    auto http = Http::get(url)
        .timeout_connect(10)
        .timeout_max(30)
        .on_complete([this, url](std::string body, unsigned status) {
            if (status == 200 && !body.empty()) {
                wxMemoryBuffer buf(body.size());
                memcpy(buf.GetWriteBuf(buf.GetSize()), body.data(), body.size());
                buf.UngetWrite(buf.GetSize());

                wxImage img;
                wxPNGHandler pngHandler;
                wxJPEGHandler jpgHandler;
                wxGIFHandler gifHandler;

                if (pngHandler.LoadFile(&img, buf)) {}
                else if (jpgHandler.LoadFile(&img, buf)) {}
                else if (gifHandler.LoadFile(&img, buf)) {}
                else {
                    wxGetApp().CallAfter([this]() {
                        wxMessageBox(_L("Unsupported image format"), _L("Error"));
                    });
                    return;
                }

                if (img.GetWidth() > 1024 || img.GetHeight() > 1024) {
                    img.Rescale(std::min(1024, img.GetWidth()), std::min(1024, img.GetHeight()));
                }
                wxMemoryBuffer outBuf;
                if (!pngHandler.SaveFile(&img, outBuf, wxBITMAP_TYPE_PNG)) {
                    wxGetApp().CallAfter([this]() {
                        wxMessageBox(_L("Failed to encode image"), _L("Error"));
                    });
                    return;
                }

                std::string base64;
                base64.resize(4 * ((outBuf.GetDataLen() + 2) / 3));
                size_t out_len = 0;
                wxBase64Encode((char*)base64.data(), outBuf.GetDataLen(), (const char*)outBuf.GetData(), outBuf.GetDataLen(), &out_len);
                base64.resize(out_len);

                wxGetApp().CallAfter([this, base64]() {
                    send_message("", base64);
                });
            } else {
                wxGetApp().CallAfter([this]() {
                    wxMessageBox(_L("Failed to download image"), _L("Error"));
                });
            }
        })
        .on_error([this](std::string body, std::string error, unsigned status) {
            wxGetApp().CallAfter([this, error]() {
                wxMessageBox(from_u8("Download failed: " + error), _L("Error"));
            });
        });
    http.perform();
}

}} // namespace Slic3r::GUI