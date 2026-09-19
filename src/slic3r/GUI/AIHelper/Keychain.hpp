#ifndef slic3r_Keychain_hpp_
#define slic3r_Keychain_hpp_

#include <string>
#include <optional>

namespace Slic3r { namespace GUI {

class Keychain
{
public:
    // Store a secret for the given service/account
    // Returns true on success
    static bool store(const std::string& service, const std::string& account, const std::string& secret);

    // Retrieve a secret for the given service/account
    // Returns nullopt if not found or on error
    static std::optional<std::string> retrieve(const std::string& service, const std::string& account);

    // Erase a secret for the given service/account
    // Returns true on success (or if not found)
    static bool erase(const std::string& service, const std::string& account);

private:
    class Impl {
    public:
        static bool store(const std::string& secret);
        static std::optional<std::string> retrieve();
        static bool erase();
    };
};

}} // namespace Slic3r::GUI

#endif // slic3r_Keychain_hpp_