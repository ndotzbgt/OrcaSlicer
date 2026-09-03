#ifndef slic3r_AIEngine_hpp_
#define slic3r_AIEngine_hpp_

#include <string>
#include <functional>
#include <memory>
#include <vector>

#include "libslic3r/Config.hpp"

namespace Slic3r {

enum class AIStreamMode {
    Token,
    Batch,
};

struct AIRequest {
    std::string role;
    std::string content;
    std::string image_base64;
};

struct AIResponse {
    std::string content;
    std::string finish_reason;
    bool done{false};
};

struct AIChunk {
    std::string text;
    bool        done{false};
};

using AIStreamCallback = std::function<void(const AIChunk&)>;

class AIEngineBase
{
public:
    virtual ~AIEngineBase() = default;

    virtual std::string get_backend_name() const = 0;
    virtual bool supports_streaming() const = 0;
    virtual bool needs_api_key() const = 0;
    virtual bool is_available() const = 0;

    virtual std::string test_connection() const = 0;

    virtual void chat(
        const std::vector<AIRequest>& history,
        const std::vector<AIRequest>& messages,
        AIStreamCallback on_chunk,
        std::function<void(const std::string& error)> on_error,
        std::function<void()> on_done) = 0;

    virtual void abort() = 0;

    void set_model(const std::string& model) { m_model = model; }
    const std::string& get_model() const { return m_model; }

    void set_api_key(const std::string& key) { m_api_key = key; }
    const std::string& get_api_key() const { return m_api_key; }

    void set_endpoint(const std::string& endpoint) { m_endpoint = endpoint; }
    const std::string& get_endpoint() const { return m_endpoint; }

protected:
    std::string m_model;
    std::string m_api_key;
    std::string m_endpoint;
};

using AIEnginePtr = std::shared_ptr<AIEngineBase>;

} // namespace Slic3r

#endif // slic3r_AIEngine_hpp_