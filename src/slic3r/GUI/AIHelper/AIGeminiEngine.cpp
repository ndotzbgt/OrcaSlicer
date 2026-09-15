#include "AIGeminiEngine.hpp"
#include "SSEParser.hpp"
#include "slic3r/Utils/Http.hpp"
#include "nlohmann/json.hpp"

#include <wx/thread.h>
#include <wx/string.h>
#include <boost/algorithm/string.hpp>
#include <functional>

namespace Slic3r {

AIGeminiEngine::AIGeminiEngine(const std::string& api_key,
                                const std::string& model,
                                const std::string& endpoint)
    : is_available_(false)
{
    set_api_key(api_key);
    set_model(model);
    set_endpoint(endpoint.empty() ? "https://generativelanguage.googleapis.com/v1beta" : endpoint);
    is_available_ = !get_api_key().empty();
}

bool AIGeminiEngine::is_available() const {
    return is_available_ && !get_api_key().empty();
}

std::string AIGeminiEngine::test_connection() const {
    if (!is_available()) {
        return "API key not configured";
    }
    return "OK";
}

void AIGeminiEngine::chat(
    const std::vector<AIRequest>& history,
    const std::vector<AIRequest>& messages,
    AIStreamCallback on_chunk,
    std::function<void(const std::string& error)> on_error,
    std::function<void()> on_done)
{
    if (!is_available()) {
        on_error("Gemini: API key not configured");
        on_done();
        return;
    }

    do_chat_stream(messages, on_chunk, on_error, on_done, 0);
}

void AIGeminiEngine::abort() {
}

void AIGeminiEngine::do_chat_stream(
    const std::vector<AIRequest>& messages,
    AIStreamCallback on_chunk,
    std::function<void(const std::string& error)> on_error,
    std::function<void()> on_done,
    int attempt)
{
    using json = nlohmann::json;

    json payload;
    json contents = json::array();

    for (const auto& msg : messages) {
        json part;
        part["role"] = msg.role;
        json parts = json::array();

        if (!msg.content.empty()) {
            parts.push_back({{"text", msg.content}});
        }
        if (!msg.image_base64.empty()) {
            parts.push_back({
                {"inline_data", {
                    {"mime_type", "image/png"},
                    {"data", msg.image_base64}
                }}
            });
        }
        part["parts"] = parts;
        contents.push_back(part);
    }

    payload["contents"] = contents;
    payload["generationConfig"] = {
        {"temperature", 0.7},
        {"topK", 40},
        {"topP", 0.95},
        {"maxOutputTokens", 8192},
    };
    payload["stream"] = true;

    std::string url = get_endpoint() + "/models/" + get_model() + ":streamGenerateContent";

    auto sse_parser = std::make_shared<SSEParser>(on_chunk);

    // Use std::function for recursive calls to avoid lambda capture issues
    std::function<void(int)> perform_request = [&](int current_attempt) {
        auto sse_parser_local = std::make_shared<SSEParser>(on_chunk);

        Http::post(url)
            .header("Content-Type", "application/json")
            .header("x-goog-api-key", get_api_key())
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
                // Retry on transient errors (429, 5xx, network errors)
                bool retryable = (status == 429 || (status >= 500 && status < 600) || status == 0);
                if (retryable && current_attempt < 5) {
                    // Exponential backoff: 1s, 2s, 4s, 8s, 16s
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