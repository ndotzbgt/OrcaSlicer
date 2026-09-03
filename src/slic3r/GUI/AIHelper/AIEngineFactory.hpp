#ifndef slic3r_AIEngineFactory_hpp_
#define slic3r_AIEngineFactory_hpp_

#include "AIEngine.hpp"

namespace Slic3r {

class AIEngineFactory
{
public:
    enum class Backend {
        Gemini,
        Ollama,
        OpenAICompat,
    };

    static AIEnginePtr create_engine(Backend backend,
                                     const std::string& api_key,
                                     const std::string& model,
                                     const std::string& endpoint);

    static Backend parse_backend(const std::string& backend_str);
    static std::string backend_to_string(Backend backend);
    static std::vector<std::string> available_backends();

    static AIEnginePtr create_from_config(const std::string& backend_str,
                                          const std::string& api_key,
                                          const std::string& model,
                                          const std::string& endpoint);
};

} // namespace Slic3r

#endif // slic3r_AIEngineFactory_hpp_