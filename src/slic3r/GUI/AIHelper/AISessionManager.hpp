#ifndef slic3r_AISessionManager_hpp_
#define slic3r_AISessionManager_hpp_

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "AIEngine.hpp"

namespace Slic3r {

class AISessionManager
{
public:
    struct Message {
        std::string role;
        std::string content;
        std::string image_base64;
        std::string timestamp;
    };

    struct Session {
        int version = 1;  // For migration
        std::string project_path;
        std::vector<Message> messages;
        std::string created_at;
        std::string updated_at;
    };

    // Trim limits (configurable via preferences)
    static constexpr size_t DEFAULT_MAX_MESSAGES = 100;
    static constexpr size_t DEFAULT_MAX_BYTES = 500 * 1024;

    explicit AISessionManager(const std::string& project_path = "");

    void set_project_path(const std::string& path);

    bool load();
    bool save();

    const std::vector<Message>& get_messages() const { return m_session.messages; }
    void add_message(const Message& msg);
    void clear();

    // Trim to configurable limits
    void trim_to_limit(size_t max_messages = DEFAULT_MAX_MESSAGES, size_t max_bytes = DEFAULT_MAX_BYTES);

    static std::string get_session_file_path(const std::string& project_path);

private:
    Session m_session;
    std::string m_project_path;
    mutable std::mutex m_mutex;
    void init_session();
};

} // namespace Slic3r

#endif // slic3r_AISessionManager_hpp_