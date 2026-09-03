#include "AISessionManager.hpp"
#include "libslic3r/Utils.hpp"
#include "nlohmann/json.hpp"

#include <wx/datetime.h>
#include <wx/filename.h>
#include <fstream>
#include <filesystem>

namespace Slic3r {

AISessionManager::AISessionManager(const std::string& project_path)
    : m_project_path(project_path)
{
    init_session();
    if (!m_project_path.empty()) {
        load();
    }
}

void AISessionManager::set_project_path(const std::string& path) {
    if (m_project_path != path) {
        save();
        m_project_path = path;
        init_session();
        load();
    }
}

void AISessionManager::init_session() {
    m_session.project_path = m_project_path;
    m_session.version = 1;
    wxDateTime now = wxDateTime::Now();
    m_session.created_at = now.FormatISOCombined(' ').ToStdString();
    m_session.updated_at = m_session.created_at;
}

bool AISessionManager::load() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = get_session_file_path(m_project_path);
    if (path.empty()) {
        return false;
    }

    // Try companion file first
    if (!wxFile::Exists(path)) {
        // Try .3mf companion file
        std::string companion = m_project_path + ".ai.json";
        if (wxFile::Exists(companion)) {
            path = companion;
        } else {
            return false;
        }
    }

    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;

        // Handle version migration
        int version = j.value("version", 1);
        m_session.version = version;
        m_session.project_path = j.value("project_path", "");
        m_session.created_at = j.value("created_at", "");
        m_session.updated_at = j.value("updated_at", "");

        m_session.messages.clear();
        for (const auto& msg : j["messages"]) {
            Message m;
            m.role = msg.value("role", "");
            m.content = msg.value("content", "");
            m.image_base64 = msg.value("image_base64", "");
            m.timestamp = msg.value("timestamp", "");
            m_session.messages.push_back(std::move(m));
        }
        return true;
    } catch (...) {
        // Corruption recovery: start fresh session
        init_session();
        return false;
    }
}

bool AISessionManager::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_project_path.empty()) return false;

    std::string path = get_session_file_path(m_project_path);
    if (path.empty()) return false;

    wxFileName fn(path);
    fn.Mkdir(wxS_DIR_DEFAULT, true);

    nlohmann::json j;
    j["version"] = m_session.version;
    j["project_path"] = m_session.project_path;
    j["created_at"] = m_session.created_at;
    j["updated_at"] = m_session.updated_at;

    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : m_session.messages) {
        nlohmann::json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        m["image_base64"] = msg.image_base64;
        m["timestamp"] = msg.timestamp;
        msgs.push_back(m);
    }
    j["messages"] = msgs;

    // Atomic write: temp file + rename
    std::string tmp_path = path + ".tmp";
    std::ofstream file(tmp_path);
    if (!file.is_open()) return false;

    file << j.dump(2);
    file.close();

    if (!file) return false;

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    return true;
}

void AISessionManager::add_message(const Message& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_session.messages.push_back(msg);
    m_session.updated_at = wxDateTime::Now().FormatISOCombined(' ').ToStdString();
    if (!m_project_path.empty()) {
        save();
        // Read trim limits from config
        size_t max_messages = 100;
        size_t max_kb = 500;
        if (wxTheApp && wxGetApp().app_config) {
            std::string max_msg_str = wxGetApp().app_config->get("ai", "max_messages");
            std::string max_kb_str = wxGetApp().app_config->get("ai", "max_kb");
            if (!max_msg_str.empty()) {
                try { max_messages = std::stoul(max_msg_str); } catch (...) {}
            }
            if (!max_kb_str.empty()) {
                try { max_kb = std::stoul(max_kb_str); } catch (...) {}
            }
        }
        trim_to_limit(max_messages, max_kb * 1024);
    }
}

void AISessionManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_session.messages.clear();
    m_session.updated_at = wxDateTime::Now().FormatISOCombined(' ').ToStdString();
    if (!m_project_path.empty()) {
        save();
    }
}

void AISessionManager::trim_to_limit(size_t max_messages, size_t max_bytes) {
    // Trim by message count
    if (m_session.messages.size() > max_messages) {
        size_t excess = m_session.messages.size() - max_messages;
        m_session.messages.erase(m_session.messages.begin(), m_session.messages.begin() + excess);
    }

    // Trim by byte size (approximate)
    size_t total_bytes = 0;
    for (const auto& msg : m_session.messages) {
        total_bytes += msg.content.size() + msg.image_base64.size() + msg.role.size() + msg.timestamp.size();
    }

    if (total_bytes > max_bytes) {
        // Remove oldest messages until under limit
        while (!m_session.messages.empty() && total_bytes > max_bytes) {
            const auto& msg = m_session.messages.front();
            total_bytes -= msg.content.size() + msg.image_base64.size() + msg.role.size() + msg.timestamp.size();
            m_session.messages.erase(m_session.messages.begin());
        }
    }
}

std::string AISessionManager::get_session_file_path(const std::string& project_path) {
    if (project_path.empty()) return "";

    wxFileName fn(project_path);
    std::string dir = fn.GetPath().ToStdString();
    std::string base = fn.GetName().ToStdString();

    return dir + "/" + base + "_ai_session.json";
}

} // namespace Slic3r