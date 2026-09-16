#ifndef slic3r_AIFallback_hpp_
#define slic3r_AIFallback_hpp_

#include "AIEngine.hpp"

namespace Slic3r {

class AIFallback : public AIEngineBase
{
public:
    AIFallback() = default;

    std::string get_backend_name() const override { return "fallback"; }
    bool supports_streaming() const override { return false; }
    bool needs_api_key() const override { return false; }
    bool is_available() const override { return true; }

    std::string test_connection() const override { return "OK (offline)"; }

    void abort() override {}

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