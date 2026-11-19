#pragma once

#include <libslic3r/Config.hpp>

#include <map>
#include <stdexcept>
#include <string>

namespace Slic3r {

// Reject a staged preset update when one of the settings it depends on no
// longer has the value observed before staging. Callers run this check in the
// same UI-thread operation that applies the update, so a concurrent user edit
// cannot be overwritten between validation and commit.
inline void verify_expected_current_values(
    const ConfigBase& config,
    const std::map<std::string, std::string>& expected_current)
{
    for (const auto& [key, expected] : expected_current) {
        if (!config.has(key))
            throw std::runtime_error("Expected current value for setting \"" + key
                                     + "\" is unavailable; nothing was applied");
        const std::string actual = config.opt_serialize(key);
        if (actual != expected)
            throw std::runtime_error("Setting \"" + key
                                     + "\" changed while the update was pending; nothing was applied");
    }
}

} // namespace Slic3r
