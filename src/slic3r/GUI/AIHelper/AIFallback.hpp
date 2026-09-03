#ifndef slic3r_AIFallback_hpp_
#define slic3r_AIFallback_hpp_

#include "AIEngine.hpp"

namespace Slic3r {

class AIFallback
{
public:
    AIFallback() = default;

    std::string get_backend_name() const { return "fallback"; }
    bool supports_streaming() const { return false; }
    bool needs_api_key() const { return false; }
    bool is_available() const { return true; }

    std::string test_connection() const { return "OK (offline)"; }

    void chat(
        const std::vector<AIRequest>& history,
        const std::vector<AIRequest>& messages,
        AIStreamCallback on_chunk,
        std::function<void(const std::string& error)> on_error,
        std::function<void()> on_done);

private:
    std::string find_best_match(const std::string& query) const;
};

} // namespace Slic3r

#endif // slic3r_AIFallback_hpp_