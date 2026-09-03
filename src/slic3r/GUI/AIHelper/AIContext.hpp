#ifndef slic3r_AIContext_hpp_
#define slic3r_AIContext_hpp_

#include <string>
#include <map>

namespace Slic3r {

class DynamicPrintConfig;

struct AIContextData {
    std::string active_printer_name;
    std::string active_printer_vendor;
    std::string active_printer_model;
    std::string active_nozzle_diameter;
    std::string active_filament_type;
    std::string active_filament_vendor;
    std::string active_filament_name;
    std::string layer_height;
    std::string infill_density;
    std::string print_speed;
    std::string bed_temperature;
    std::string nozzle_temperature;
    std::string model_count;
    std::string total_volume;
    std::string estimated_time;
    std::map<std::string, std::string> custom_fields;
};

class AIContext
{
public:
    static AIContextData build(const DynamicPrintConfig* config);
    static AIContextData build_from_current_project();
    static std::string format_for_prompt(const AIContextData& ctx);
    static std::string format_compact(const AIContextData& ctx);
};

} // namespace Slic3r

#endif // slic3r_AIContext_hpp_