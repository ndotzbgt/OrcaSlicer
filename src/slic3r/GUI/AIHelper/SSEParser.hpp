#ifndef slic3r_SSEParser_hpp_
#define slic3r_SSEParser_hpp_

#include "AIEngine.hpp"
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r {

class SSEParser
{
public:
    using ChunkCallback = std::function<void(const AIChunk&)>;

    explicit SSEParser(ChunkCallback cb);
    ~SSEParser() = default;

    // Feed incremental data from HTTP on_progress callback
    void feed(const std::string& data);

    // Call when HTTP request completes (on_complete)
    void finish();

    // Call when HTTP request errors (on_error) - resets state
    void reset();

    bool is_done() const { return m_done; }

private:
    void parse_buffer();
    void parse_line(const std::string& line);
    void emit_chunk(const std::string& text, bool done);
    void emit_chunk(const std::string& text, bool done, const nlohmann::json& j);
    std::string extract_text(const nlohmann::json& j);

    ChunkCallback m_callback;
    std::string m_buffer;
    bool m_done{false};
    bool m_saw_done_marker{false};
};

} // namespace Slic3r

#endif // slic3r_SSEParser_hpp_