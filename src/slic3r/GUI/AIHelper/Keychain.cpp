#include "AIHelper/Keychain.hpp"

#include <wx/string.h>
#include <wx/filename.h>

#ifdef _WIN32
    #include <windows.h>
    #include <wincrypt.h>
    #pragma comment(lib, "crypt32.lib")
#elif defined(__APPLE__)
    #include <Security/Security.h>
    #include <CoreFoundation/CoreFoundation.h>
#else
    #include <secret/secret.h>
#endif

namespace Slic3r { namespace GUI {

namespace {

std::string service_name = "OrcaSlicer";
std::string account_name = "ai_api_key";

#ifdef _WIN32
bool keychain_store_win(const std::string& secret) {
    DATA_BLOB data_in = { static_cast<DWORD>(secret.size()), const_cast<BYTE*>(reinterpret_cast<const BYTE*>(secret.data())) };
    DATA_BLOB data_out = { 0, nullptr };

    // Use CRYPTPROTECT_LOCAL_MACHINE for machine-level or omit for user-level
    // We use user-level (default) so only this user can decrypt
    if (!CryptProtectData(&data_in, L"OrcaSlicer AI API Key", nullptr, nullptr, nullptr, 0, &data_out)) {
        return false;
    }

    // Store in registry
    HKEY hkey;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\OrcaSlicer", 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, nullptr);
    if (result != ERROR_SUCCESS) {
        LocalFree(data_out.pbData);
        return false;
    }

    result = RegSetValueExW(hkey, L"ai_api_key", 0, REG_BINARY, data_out.pbData, data_out.cbData);
    RegCloseKey(hkey);
    LocalFree(data_out.pbData);

    return result == ERROR_SUCCESS;
}

std::optional<std::string> keychain_retrieve_win() {
    HKEY hkey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OrcaSlicer", 0, KEY_READ, &hkey);
    if (result != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    result = RegQueryValueExW(hkey, L"ai_api_key", nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || type != REG_BINARY) {
        RegCloseKey(hkey);
        return std::nullopt;
    }

    std::vector<BYTE> encrypted(size);
    result = RegQueryValueExW(hkey, L"ai_api_key", nullptr, &type, encrypted.data(), &size);
    RegCloseKey(hkey);
    if (result != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DATA_BLOB data_in = { size, encrypted.data() };
    DATA_BLOB data_out = { 0, nullptr };

    if (!CryptUnprotectData(&data_in, nullptr, nullptr, nullptr, nullptr, 0, &data_out)) {
        return std::nullopt;
    }

    std::string secret(reinterpret_cast<char*>(data_out.pbData), data_out.cbData);
    LocalFree(data_out.pbData);
    return secret;
}

bool keychain_erase_win() {
    HKEY hkey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OrcaSlicer", 0, KEY_WRITE, &hkey);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    result = RegDeleteValueW(hkey, L"ai_api_key");
    RegCloseKey(hkey);
    return result == ERROR_SUCCESS;
}

#elif defined(__APPLE__)
bool keychain_store_mac(const std::string& secret) {
    // Delete existing item first
    CFMutableDictionaryRef delete_query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(delete_query, kSecClass, kSecClassGenericPassword);
    CFStringRef service_cf = CFStringCreateWithCString(kCFAllocatorDefault, service_name.c_str(), kCFStringEncodingUTF8);
    CFStringRef account_cf = CFStringCreateWithCString(kCFAllocatorDefault, account_name.c_str(), kCFStringEncodingUTF8);
    CFDictionaryAddValue(delete_query, kSecAttrService, service_cf);
    CFDictionaryAddValue(delete_query, kSecAttrAccount, account_cf);
    SecItemDelete(delete_query);
    CFRelease(delete_query);
    CFRelease(service_cf);
    CFRelease(account_cf);

    // Add new item
    CFMutableDictionaryRef add_query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(add_query, kSecClass, kSecClassGenericPassword);
    CFDictionaryAddValue(add_query, kSecAttrService, CFStringCreateWithCString(kCFAllocatorDefault, service_name.c_str(), kCFStringEncodingUTF8));
    CFDictionaryAddValue(add_query, kSecAttrAccount, CFStringCreateWithCString(kCFAllocatorDefault, account_name.c_str(), kCFStringEncodingUTF8));
    CFDictionaryAddValue(add_query, kSecValueData, CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(secret.data()), secret.size()));
    CFDictionaryAddValue(add_query, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
    CFDictionaryAddValue(add_query, kSecAttrSynchronizable, kCFBooleanFalse);

    OSStatus status = SecItemAdd(add_query, nullptr);
    CFRelease(add_query);
    return status == errSecSuccess;
}

std::optional<std::string> keychain_retrieve_mac() {
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionaryAddValue(query, kSecAttrService, CFStringCreateWithCString(kCFAllocatorDefault, service_name.c_str(), kCFStringEncodingUTF8));
    CFDictionaryAddValue(query, kSecAttrAccount, CFStringCreateWithCString(kCFAllocatorDefault, account_name.c_str(), kCFStringEncodingUTF8));
    CFDictionaryAddValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionaryAddValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);

    if (status != errSecSuccess || !result) {
        return std::nullopt;
    }

    CFDataRef data = static_cast<CFDataRef>(result);
    std::string secret(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
    CFRelease(data);
    return secret;
}

bool keychain_erase_mac() {
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionaryAddValue(query, kSecAttrService, CFStringCreateWithCString(kCFAllocatorDefault, service_name.c_str(), kCFStringEncodingUTF8));
    CFDictionaryAddValue(query, kSecAttrAccount, CFStringCreateWithCString(kCFAllocatorDefault, account_name.c_str(), kCFStringEncodingUTF8));
    OSStatus status = SecItemDelete(query);
    CFRelease(query);
    return status == errSecSuccess || status == errSecItemNotFound;
}

#else
bool keychain_store_linux(const std::string& secret) {
    SecretSchema schema = {
        "org.orcaslicer.ai.apikey",
        SECRET_SCHEMA_NONE,
        {
            { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
        }
    };

    GError* error = nullptr;
    bool success = secret_password_store_sync(
        &schema,
        nullptr, // default collection
        nullptr, // cancellable
        &error,
        "service", service_name.c_str(),
        "account", account_name.c_str(),
        secret.c_str(),
        nullptr
    );

    if (error) {
        g_error_free(error);
    }
    return success;
}

std::optional<std::string> keychain_retrieve_linux() {
    SecretSchema schema = {
        "org.orcaslicer.ai.apikey",
        SECRET_SCHEMA_NONE,
        {
            { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
        }
    };

    GError* error = nullptr;
    gchar* secret = secret_password_lookup_sync(
        &schema,
        nullptr, // default collection
        nullptr, // cancellable
        &error,
        "service", service_name.c_str(),
        "account", account_name.c_str(),
        nullptr
    );

    if (error) {
        g_error_free(error);
        return std::nullopt;
    }

    if (!secret) {
        return std::nullopt;
    }

    std::string result(secret);
    g_free(secret);
    return result;
}

bool keychain_erase_linux() {
    SecretSchema schema = {
        "org.orcaslicer.ai.apikey",
        SECRET_SCHEMA_NONE,
        {
            { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
        }
    };

    GError* error = nullptr;
    bool success = secret_password_clear_sync(
        &schema,
        nullptr, // default collection
        nullptr, // cancellable
        &error,
        "service", service_name.c_str(),
        "account", account_name.c_str(),
        nullptr
    );

    if (error) {
        g_error_free(error);
    }
    return success;
}

#endif

} // anonymous namespace

class Keychain::Impl {
public:
    static bool store(const std::string& secret) {
#ifdef _WIN32
        return keychain_store_win(secret);
#elif defined(__APPLE__)
        return keychain_store_mac(secret);
#else
        return keychain_store_linux(secret);
#endif
    }

    static std::optional<std::string> retrieve() {
#ifdef _WIN32
        return keychain_retrieve_win();
#elif defined(__APPLE__)
        return keychain_retrieve_mac();
#else
        return keychain_retrieve_linux();
#endif
    }

    static bool erase() {
#ifdef _WIN32
        return keychain_erase_win();
#elif defined(__APPLE__)
        return keychain_erase_mac();
#else
        return keychain_erase_linux();
#endif
    }
};

bool Keychain::store(const std::string& service, const std::string& account, const std::string& secret) {
    (void)service; (void)account; // Use hardcoded names for now
    return Impl::store(secret);
}

std::optional<std::string> Keychain::retrieve(const std::string& service, const std::string& account) {
    (void)service; (void)account;
    return Impl::retrieve();
}

bool Keychain::erase(const std::string& service, const std::string& account) {
    (void)service; (void)account;
    return Impl::erase();
}

}} // namespace Slic3r::GUI