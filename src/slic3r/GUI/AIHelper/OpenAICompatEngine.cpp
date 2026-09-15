#include "OpenAICompatEngine.hpp"
#include "SSEParser.hpp"
#include "slic3r/Utils/Http.hpp"
#include "nlohmann/json.hpp"

#include <wx/thread.h>
#include <functional>

namespace Slic3r {

OpenAICompatEngine::OpenAICompatEngine(const std::string& api_key,
                                        const std::string& model,
                                        const std::string& endpoint)
    : is_available_(false)
{
    set_api_key(api_key);
    set_model(model);
    set_endpoint(endpoint.empty() ? "https://api.openai.com/v1" : endpoint);
    is_available_ = !get_api_key().empty();
}

bool OpenAICompatEngine::is_available() const {
    return is_available_ && !get_api_key().empty();
}

std::string OpenAICompatEngine::test_connection() const {
    if (!is_available()) {
        return "API key not configured";
    }
    return "OK";
}

void OpenAICompatEngine::chat(
    const std::vector<AIRequest>& history,
    const std::vector<AIRequest>& messages,
    AIStreamCallback on_chunk,
    std::function<void(const std::string& error)> on_error,
    std::function<void()> on_done)
{
    if (!is_available()) {
        on_error("OpenAI-compatible: API key not configured");
        on_done();
        return;
    }

    do_chat_stream(messages, on_chunk, on_error, on_done, 0);
}

void OpenAICompatEngine::abort() {
}

void OpenAICompatEngine::do_chat_stream(
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
        if (!msg.image_base64.empty()) {
            json content = json::array();
            if (!msg.content.empty()) {
                content.push_back({{"type", "text"}, {"text", msg.content}});
            }
            content.push_back({
                {"type", "image_url"},
                {"image_url", {{"url", "data:image/png;base64," + msg.image_base64}}}
            });
            m["content"] = content;
        } else {
            m["content"] = msg.content;
        }
        msgs.push_back(m);
    }

    payload["model"] = get_model();
    payload["messages"] = msgs;
    payload["stream"] = true;
    payload["temperature"] = 0.7;
    payload["max_tokens"] = 8192;

    std::string url = get_endpoint() + "/chat/completions";

    auto sse_parser = std::make_shared<SSEParser>(on_chunk);

    // Use std::function for recursive calls to avoid lambda capture issues
    std::function<void(int)> perform_request = [&](int current_attempt) {
        auto sse_parser_local = std::make_shared<SSEParser>(on_chunk);

        auto http = Http::post(url)
            .header("Content-Type", "application/json")
            .header("Authorization", "Bearer " + get_api_key())
            .set_post_body(payload.dump())
            .timeout_connect(10)
            .timeout_max(120)
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
            });

        http.perform();
    };

    perform_request(attempt);
}

} // namespace Slic3r