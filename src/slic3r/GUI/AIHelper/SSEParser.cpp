#include "SSEParser.hpp"
#include "nlohmann/json.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r {

using json = nlohmann::json;

SSEParser::SSEParser(ChunkCallback cb)
    : m_callback(std::move(cb))
{
}

void SSEParser::feed(const std::string& data)
{
    if (m_done || data.empty()) return;
    m_buffer += data;
    parse_buffer();
}

void SSEParser::finish()
{
    if (!m_done) {
        // Flush any remaining buffer
        parse_buffer();
        if (!m_saw_done_marker) {
            emit_chunk("", true);
        }
        m_done = true;
    }
}

void SSEParser::reset()
{
    m_buffer.clear();
    m_done = false;
    m_saw_done_marker = false;
}

void SSEParser::parse_buffer()
{
    size_t pos = 0;
    while (true) {
        // Find next line ending
        size_t nl = m_buffer.find('\n', pos);
        if (nl == std::string::npos) {
            // Incomplete line, keep in buffer
            if (pos > 0) {
                m_buffer.erase(0, pos);
            }
            break;
        }

        // Extract line (including \n)
        std::string line = m_buffer.substr(pos, nl - pos + 1);
        pos = nl + 1;

        // Handle empty lines (SSE field separators)
        if (line == "\n" || line == "\r\n") {
            continue;
        }

        parse_line(line);
    }

    // Remove processed portion
    if (pos > 0) {
        m_buffer.erase(0, pos);
    }
}

void SSEParser::parse_line(const std::string& line)
{
    // SSE format: "data: <json>\n\n" or "data: [DONE]\n\n"
    // Also handle comments ": ping\n" and other fields
    if (line.size() < 6 || line.substr(0, 6) != "data: ") {
        return; // Ignore non-data fields (id:, event:, retry:, comments)
    }

    std::string payload = line.substr(6);
    boost::trim(payload);

    if (payload == "[DONE]") {
        m_saw_done_marker = true;
        emit_chunk("", true);
        return;
    }

    if (payload.empty()) {
        return;
    }

    try {
        json j = json::parse(payload);
        emit_chunk("", false, j);
    } catch (const json::parse_error&) {
        // Malformed JSON - log and skip, don't crash
        // In production, could use BOOST_LOG_TRIVIAL(warning)
    } catch (...) {
        // Any other parse error
    }
}

void SSEParser::emit_chunk(const std::string& text, bool done, const nlohmann::json& j)
{
    if (!m_callback) return;

    if (!j.is_null()) {
        // Extract text from various provider formats
        std::string extracted = extract_text(j);
        if (!extracted.empty() || done) {
            m_callback(AIChunk{extracted, done});
        }
    } else if (!text.empty() || done) {
        m_callback(AIChunk{text, done});
    }
}

std::string SSEParser::extract_text(const nlohmann::json& j)
{
    // Try multiple provider formats
    // Gemini: { "candidates": [{ "content": { "parts": [{ "text": "..." }] } }] }
    if (j.contains("candidates") && j["candidates"].is_array() && !j["candidates"].empty()) {
        const auto& cand = j["candidates"][0];
        if (cand.contains("content") && cand["content"].contains("parts")) {
            std::string result;
            for (const auto& part : cand["content"]["parts"]) {
                if (part.contains("text")) {
                    result += part["text"].get<std::string>();
                }
            }
            if (!result.empty()) return result;
        }
    }

    // OpenAI/Ollama: { "choices": [{ "delta": { "content": "..." } }] } or { "message": { "content": "..." } }
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
        const auto& choice = j["choices"][0];
        if (choice.contains("delta") && choice["delta"].contains("content")) {
            return choice["delta"]["content"].get<std::string>();
        }
        if (choice.contains("message") && choice["message"].contains("content")) {
            return choice["message"]["content"].get<std::string>();
        }
    }

    // Ollama streaming: { "message": { "content": "..." }, "done": true }
    if (j.contains("message") && j["message"].contains("content")) {
        return j["message"]["content"].get<std::string>();
    }

    return "";
}

} // namespace Slic3r