#pragma once
#include "font_atlas.h"
#include "text_reader.h"
#include "types.h"

// ============================================================================
// Skill Window: Detect and parse the EU Skills window from a screenshot
// ============================================================================

namespace SkillWindow {

// Detect the Skills window within a game screenshot
// Returns layout information if found
SkillWindowLayout Detect(const Bitmap& screenshot);

// Detect with diagnostic info (diagOut filled with failure reason)
SkillWindowLayout Detect(const Bitmap& screenshot, std::string& diagOut);

// Parse all visible skill rows from the current page
// Requires a calibrated layout and appropriate font atlas
struct ParseResult {
    std::vector<SkillEntry> skills;
    int currentPage;
    int totalPages;
    int totalPoints;
    std::string activeCategory;
    bool valid;
    int listCursorOut = 0;
    std::string diag;  // per-row number diagnostics
};

ParseResult ParsePage(const Bitmap& screenshot, const SkillWindowLayout& layout, const FontAtlas& nameFont,
                      const FontAtlas& rankFont, const FontAtlas& numberFont,
                      int listCursorIn = 0  // starting position in skill list
);

// Read just the page indicator (e.g., "1/12")
struct PageInfo {
    int current;
    int total;
    bool valid;
};

PageInfo ReadPageIndicator(const Bitmap& screenshot, const SkillWindowLayout& layout, const FontAtlas& font);

// Detect which category is currently selected (currently unused)
// std::string ReadActiveCategory(
//     const Bitmap& screenshot,
//     const SkillWindowLayout& layout,
//     const FontAtlas& font
// );

// Check if the skills window is currently visible
bool IsVisible(const Bitmap& screenshot, const SkillWindowLayout& layout);

}  // namespace SkillWindow
