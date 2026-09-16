#include "AIOllamaEngine.hpp"
#include "SSEParser.hpp"
#include "slic3r/Utils/Http.hpp"
#include "nlohmann/json.hpp"

#include <wx/thread.h>
#include <functional>

namespace Slic3r {

AIOllamaEngine::AIOllamaEngine(const std::string& api_key,
                                const std::string& model,
                                const std::string& endpoint)
    : is_available_(false)
{
    set_api_key(api_key);
    set_model(model);
    set_endpoint(endpoint.empty() ? "http://localhost:11434" : endpoint);
    is_available_ = true;
}

bool AIOllamaEngine::is_available() const {
    return is_available_;
}

std::string AIOllamaEngine::test_connection() const {
    return "OK (local)";
}

void AIOllamaEngine::chat(
    const std::vector<AIRequest>& history,
    const std::vector<AIRequest>& messages,
    AIStreamCallback on_chunk,
    std::function<void(const std::string& error)> on_error,
    std::function<void()> on_done)
{
    if (!is_available()) {
        on_error("Ollama: Not available");
        on_done();
        return;
    }

    do_chat_stream(messages, on_chunk, on_error, on_done, 0);
}

void AIOllamaEngine::abort() {
}

void AIOllamaEngine::do_chat_stream(
    const std::vector<AIRequest>& messages,
    AIStreamCallback on_chunk,
    std::function<void(const std::string& error)> on_error,
    std::function<void()> on_done,
    int attempt)
{
    using json = nlohmann::json;

    json payload;
    json msgs = json::array();

    for (const auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (!msg.image_base64.empty()) {
            m["images"] = json::array({msg.image_base64});
        }
        msgs.push_back(m);
    }

    payload["model"] = get_model();
    payload["messages"] = msgs;
    payload["stream"] = true;
    payload["options"] = {
        {"temperature", 0.7},
        {"top_k", 40},
        {"top_p", 0.9},
    };

    std::string url = get_endpoint() + "/api/chat";

    auto sse_parser = std::make_shared<SSEParser>(on_chunk);

    // Use std::function for recursive calls to avoid lambda capture issues
    std::function<void(int)> perform_request = [&](int current_attempt) {
        auto sse_parser_local = std::make_shared<SSEParser>(on_chunk);

        Http::post(url)
            .header("Content-Type", "application/json")
            .set_post_body(payload.dump())
            .timeout_connect(5)
            .timeout_max(300)
            .on_progress([sse_parser_local](Http::Progress prog, bool& cancel) {
                if (!prog.buffer.empty()) {
                    sse_parser_local->feed(prog.buffer);
                }
            })
            .on_complete([sse_parser_local, on_chunk, on_done](std::string body, unsigned status) {
                sse_parser_local->finish();
                on_chunk(AIChunk{"", true});
                on_done();
            })
            .on_error([this, &perform_request, sse_parser_local, on_chunk, on_error, on_done, current_attempt](std::string body, std::string error, unsigned status) {
                bool retryable = (status == 429 || (status >= 500 && status < 600) || status == 0);
                if (retryable && current_attempt < 5) {
                    int delay_ms = 1000 * (1 << current_attempt);
                    wxMilliSleep(delay_ms);
                    sse_parser_local->reset();
                    perform_request(current_attempt + 1);
                } else {
                    sse_parser_local->reset();
                    if (status >= 400) {
                        on_error("HTTP " + std::to_string(status) + ": " + body);
                    } else {
                        on_error(error.empty() ? "Connection failed" : error);
                    }
                    on_done();
                }
            })
            .perform();
    };

    perform_request(attempt);
}

} // namespace Slic3r