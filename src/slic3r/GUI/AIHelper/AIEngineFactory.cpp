#include "AIEngineFactory.hpp"
#include "AIGeminiEngine.hpp"
#include "AIOllamaEngine.hpp"
#include "OpenAICompatEngine.hpp"
#include "AIFallback.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r {

AIEnginePtr AIEngineFactory::create_engine(Backend backend,
                                            const std::string& api_key,
                                            const std::string& model,
                                            const std::string& endpoint)
{
    switch (backend) {
        case Backend::Gemini:
            return std::make_shared<AIGeminiEngine>(api_key, model, endpoint);
        case Backend::Ollama:
            return std::make_shared<AIOllamaEngine>(api_key, model, endpoint);
        case Backend::OpenAICompat:
            return std::make_shared<OpenAICompatEngine>(api_key, model, endpoint);
    }
    return std::make_shared<AIFallback>();
}

AIEngineFactory::Backend AIEngineFactory::parse_backend(const std::string& backend_str) {
    std::string s = backend_str;
    boost::algorithm::to_lower(s);
    if (s == "gemini") return Backend::Gemini;
    if (s == "ollama") return Backend::Ollama;
    if (s == "openai_compat" || s == "openai" || s == "openai-compat") return Backend::OpenAICompat;
    return Backend::Gemini;
}

std::string AIEngineFactory::backend_to_string(Backend backend) {
    switch (backend) {
        case Backend::Gemini: return "gemini";
        case Backend::Ollama: return "ollama";
        case Backend::OpenAICompat: return "openai_compat";
    }
    return "gemini";
}

std::vector<std::string> AIEngineFactory::available_backends() {
    return {"gemini", "ollama", "openai_compat"};
}

AIEnginePtr AIEngineFactory::create_from_config(const std::string& backend_str,
                                                 const std::string& api_key,
                                                 const std::string& model,
                                                 const std::string& endpoint)
{
    Backend backend = parse_backend(backend_str);
    auto engine = create_engine(backend, api_key, model, endpoint);
    if (!engine->is_available()) {
        return std::make_shared<AIFallback>();
    }
    return engine;
}

} // namespace Slic3r