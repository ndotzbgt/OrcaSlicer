#include "AIContext.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/DynamicConfig.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"

#include <sstream>
#include <iomanip>

namespace Slic3r {

AIContextData AIContext::build(const DynamicPrintConfig* config) {
    AIContextData ctx;

    if (!config) return ctx;

    auto get_opt = [config](const char* key) -> std::string {
        const auto* opt = config->get(key);
        return opt ? opt->serialize() : "";
    };

    ctx.layer_height = get_opt("layer_height");
    ctx.infill_density = get_opt("fill_density");
    ctx.print_speed = get_opt("perimeter_speed");
    ctx.bed_temperature = get_opt("bed_temperature");
    ctx.nozzle_temperature = get_opt("temperature");

    if (auto* plater = wxGetApp().plater()) {
        auto& model = plater->model();
        ctx.model_count = std::to_string(model.objects.size());
        double total_vol = 0;
        for (auto* obj : model.objects) {
            total_vol += obj->volume();
        }
        ctx.total_volume = std::to_string(total_vol / 1000.0) + " cm³";

        if (model.print_time > 0) {
            int hours = model.print_time / 3600;
            int mins = (model.print_time % 3600) / 60;
            ctx.estimated_time = (hours > 0) ? (std::to_string(hours) + "h " + std::to_string(mins) + "m") : (std::to_string(mins) + "m");
        }
    }

    return ctx;
}

AIContextData AIContext::build_from_current_project() {
    if (auto* plater = wxGetApp().plater()) {
        return build(plater->get_edited_preset());
    }
    return {};
}

std::string AIContext::format_for_prompt(const AIContextData& ctx) {
    std::ostringstream oss;
    oss << "Current print context:\n";
    if (!ctx.active_printer_name.empty()) oss << "- Printer: " << ctx.active_printer_name << "\n";
    if (!ctx.active_filament_name.empty()) oss << "- Filament: " << ctx.active_filament_name << "\n";
    if (!ctx.layer_height.empty()) oss << "- Layer height: " << ctx.layer_height << "mm\n";
    if (!ctx.infill_density.empty()) oss << "- Infill: " << ctx.infill_density << "%\n";
    if (!ctx.print_speed.empty()) oss << "- Print speed: " << ctx.print_speed << "mm/s\n";
    if (!ctx.nozzle_temperature.empty()) oss << "- Nozzle temp: " << ctx.nozzle_temperature << "°C\n";
    if (!ctx.bed_temperature.empty()) oss << "- Bed temp: " << ctx.bed_temperature << "°C\n";
    if (!ctx.model_count.empty()) oss << "- Objects: " << ctx.model_count << "\n";
    if (!ctx.total_volume.empty()) oss << "- Volume: " << ctx.total_volume << "\n";
    if (!ctx.estimated_time.empty()) oss << "- Est. time: " << ctx.estimated_time << "\n";
    return oss.str();
}

std::string AIContext::format_compact(const AIContextData& ctx) {
    std::ostringstream oss;
    bool first = true;
    auto add = [&](const std::string& label, const std::string& value) {
        if (!value.empty()) {
            if (!first) oss << " | ";
            oss << label << ": " << value;
            first = false;
        }
    };
    add("Printer", ctx.active_printer_name);
    add("Filament", ctx.active_filament_name);
    add("Layer", ctx.layer_height + "mm");
    add("Infill", ctx.infill_density + "%");
    add("Speed", ctx.print_speed + "mm/s");
    add("Nozzle", ctx.nozzle_temperature + "°C");
    add("Bed", ctx.bed_temperature + "°C");
    add("Objects", ctx.model_count);
    add("Volume", ctx.total_volume);
    add("Time", ctx.estimated_time);
    return oss.str();
}

} // namespace Slic3r