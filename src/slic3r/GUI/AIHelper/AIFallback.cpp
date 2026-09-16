#include "AIFallback.hpp"

#include <map>
#include <string>
#include <algorithm>

namespace Slic3r {

struct FallbackEntry {
    std::vector<std::string> keywords;
    std::string response;
};

static const std::vector<FallbackEntry> FALLBACK_KNOWLEDGE_BASE = {
    {
        {"layer", "adhesion", "first layer", "bed adhesion", "warping", "corners lifting"},
        "First layer adhesion issues? Try:\n"
        "- Increase bed temperature by 5-10°C\n"
        "- Clean bed with isopropyl alcohol\n"
        "- Use brim or raft\n"
        "- Adjust Z-offset (move nozzle closer to bed)\n"
        "- Slow down first layer speed to 20-30mm/s\n"
        "- Check bed leveling"
    },
    {
        {"stringing", "oozing", "retraction", "wisps", "hairs"},
        "Stringing/oozing solutions:\n"
        "- Increase retraction distance (0.5-1mm increments)\n"
        "- Increase retraction speed (30-50mm/s)\n"
        "- Lower nozzle temperature by 5-10°C\n"
        "- Enable coasting/wipe\n"
        "- Dry filament (moisture causes stringing)\n"
        "- Check for partial clogs"
    },
    {
        {"underextrusion", "under extrusion", "gaps", "holes", "thin walls", "weak"},
        "Underextrusion fixes:\n"
        "- Check extruder gear tension\n"
        "- Increase flow rate by 5-10%\n"
        "- Check for partial nozzle clog\n"
        "- Verify filament diameter setting matches actual\n"
        "- Increase nozzle temperature\n"
        "- Check Bowden tube for friction/damage"
    },
    {
        {"overextrusion", "over extrusion", "blobs", "zits", "elephant foot", "bulging"},
        "Overextrusion solutions:\n"
        "- Decrease flow rate by 5-10%\n"
        "- Lower nozzle temperature\n"
        "- Calibrate e-steps\n"
        "- Check filament diameter accuracy\n"
        "- Enable pressure advance/linear advance"
    },
    {
        {"layer shift", "shifting", "misaligned", "offset layers"},
        "Layer shift causes:\n"
        "- Loose belts (tension them)\n"
        "- Loose pulleys/set screws\n"
        "- Print speed too high\n"
        "- Stepper drivers overheating\n"
        "- Collision with print/curling\n"
        "- Check X/Y axis smoothness"
    },
    {
        {"support", "supports", "overhang", "bridging", "drooping", "sagging"},
        "Support/overhang improvements:\n"
        "- Increase support density (15-25%)\n"
        "- Reduce support Z-gap (0.1-0.15mm)\n"
        "- Use tree/organic supports\n"
        "- Enable support interface layers\n"
        "- Decrease overhang angle threshold\n"
        "- Cool better with 100% fan on overhangs"
    },
    {
        {"clog", "clogged", "blocked", "nozzle", "jam", "not extruding"},
        "Nozzle clog solutions:\n"
        "- Cold pull (atomic pull) with nylon/clean filament\n"
        "- Heat to 250°C+ and push cleaning filament\n"
        "- Use acupuncture needle (0.35-0.4mm)\n"
        "- Replace nozzle if persistent\n"
        "- Check PTFE tube condition (Bowden)"
    },
    {
        {"bed level", "leveling", "tramming", "uneven", "first layer"},
        "Bed leveling tips:\n"
        "- Use paper method: slight resistance\n"
        "- Check all 4 corners + center\n"
        "- Re-level after any printer move\n"
        "- Use mesh bed leveling if available\n"
        "- Check for warped bed surface"
    },
    {
        {"temperature", "temp", "hotend", "bed", "nozzle", "heat"},
        "Temperature guidelines:\n"
        "- PLA: 190-220°C nozzle, 50-60°C bed\n"
        "- PETG: 230-250°C nozzle, 70-85°C bed\n"
        "- ABS/ASA: 240-270°C nozzle, 90-110°C bed (enclosure)\n"
        "- TPU: 210-230°C nozzle, 40-60°C bed\n"
        "- Always check filament manufacturer specs"
    },
    {
        {"speed", "fast", "slow", "print speed", "travel speed"},
        "Speed recommendations:\n"
        "- PLA: 50-80mm/s\n"
        "- PETG: 40-60mm/s\n"
        "- ABS/ASA: 40-60mm/s\n"
        "- TPU: 20-35mm/s\n"
        "- First layer: 20-30mm/s\n"
        "- Travel: 150-200mm/s\n"
        "- Outer walls: 30-40mm/s for quality"
    },
    {
        {"infill", "infill density", "strength", "weight", "top layers"},
        "Inf ill settings:\n"
        "- Structural: 30-50% (gyroid/cubic)\n"
        "- Standard: 15-25% (gyroid/grid)\n"
        "- Lightweight: 5-15% (lightning/grid)\n"
        "- Top layers: 5-7 for solid surface\n"
        "- Use gyroid for isotropic strength"
    },
    {
        {"warping", "curling", "lifting", "corners", "ABS", "ASA"},
        "Warping prevention:\n"
        "- Enclosure for ABS/ASA\n"
        "- Brim (5-10mm) or raft\n"
        "- Bed adhesive (glue stick, hairspray, Magigoo)\n"
        "- Higher bed temp\n"
        "- Rounded corners on model\n"
        "- Reduce cooling on first layers"
    },
    {
        {"ringing", "ghosting", "echoing", "vibration", "resonance"},
        "Ringing/ghosting fixes:\n"
        "- Enable input shaping\n"
        "- Reduce print speed/acceleration\n"
        "- Tighten belts\n"
        "- Check frame rigidity\n"
        "- Lower jerk/junction deviation"
    },
    {
        {"z seam", "seam", "blob", "zipper", "vertical line"},
        "Z-seam control:\n"
        "- Set \"Z seam alignment\" to \"Random\" or \"Rear\"\n"
        "- Enable coasting\n"
        "- Adjust retraction at layer change\n"
        "- Outer wall wipe distance\n"
        "- Print outer wall first"
    }
};

void AIFallback::chat(
    const std::vector<AIRequest>& history,
    const std::vector<AIRequest>& messages,
    AIStreamCallback on_chunk,
    std::function<void(const std::string& error)> on_error,
    std::function<void()> on_done)
{
    std::string query;
    for (const auto& msg : messages) {
        if (msg.role == "user") {
            query = msg.content;
            break;
        }
    }
    for (const auto& msg : history) {
        if (msg.role == "user") {
            query += " " + msg.content;
        }
    }

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    std::string best_response;
    int best_score = 0;

    for (const auto& entry : FALLBACK_KNOWLEDGE_BASE) {
        int score = 0;
        for (const auto& kw : entry.keywords) {
            if (lower_query.find(kw) != std::string::npos) {
                score++;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_response = entry.response;
        }
    }

    if (best_response.empty()) {
        best_response = "I'm running in offline mode. For detailed troubleshooting, please configure an AI backend in Preferences > AI Assistant.\n\nCommon topics I can help with offline:\n"
                        "- First layer adhesion / bed leveling\n"
                        "- Stringing / retraction tuning\n"
                        "- Under/over extrusion\n"
                        "- Layer shifts\n"
                        "- Support settings\n"
                        "- Nozzle clogs\n"
                        "- Temperature guidelines\n"
                        "- Print speed recommendations\n"
                        "- Infill patterns/density\n"
                        "- Warping prevention\n"
                        "- Ringing/ghosting\n"
                        "- Z-seam placement";
    }

    on_chunk(AIChunk{best_response, true});
    on_done();
}

} // namespace Slic3r