#ifndef slic3r_AIOllamaEngine_hpp_
#define slic3r_AIOllamaEngine_hpp_

#include "AIEngine.hpp"

namespace Slic3r {

class AIOllamaEngine : public AIEngineBase
{
public:
    explicit AIOllamaEngine(const std::string& api_key = "",
                            const std::string& model = "",
                            const std::string& endpoint = "http://localhost:11434");

    std::string get_backend_name() const override { return "ollama"; }
    bool supports_streaming() const override { return true; }
    bool needs_api_key() const override { return false; }
    bool is_available() const override;

    std::string test_connection() const override;

    void chat(
        const std::vector<AIRequest>& history,
        const std::vector<AIRequest>& messages,
        AIStreamCallback on_chunk,
        std::function<void(const std::string& error)> on_error,
        std::function<void()> on_done) override;

    void abort() override;

private:
    bool is_available_;
    void do_chat_stream(
        const std::vector<AIRequest>& messages,
        AIStreamCallback on_chunk,
        std::function<void(const std::string& error)> on_error,
        std::function<void()> on_done);
};

} // namespace Slic3r

#endif // slic3r_AIOllamaEngine_hpp_