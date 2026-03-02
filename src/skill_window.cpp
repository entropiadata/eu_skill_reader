#include "skill_window.h"

#include "capture.h"
#include "skill_data.h"

namespace SkillWindow {

// ============================================================================
// Detection Strategy:
//
// The Skills window has a distinctive amber/warm-colored border frame.
// 1. Find horizontal amber lines to locate the top edge
// 2. Find the right amber border column
// 3. Validate by checking for dark panel interior and "SKILLS" title
// 4. Dynamically locate column headers and row positions within the window
//
// This works regardless of window position or game resolution.
// ============================================================================

// Check if a pixel has the warm amber/brown color of the window frame
static bool IsAmber(const Pixel& p) {
    int r = p.r, g = p.g, b = p.b;
    return r > 80 && g > 40 && r > g && g > b && (r - b) > 30;
}

// ============================================================================
// Public API
// ============================================================================

SkillWindowLayout Detect(const Bitmap& screenshot) {
    std::string diag;
    return Detect(screenshot, diag);
}

SkillWindowLayout Detect(const Bitmap& screenshot, std::string& diagOut) {
    SkillWindowLayout layout;
    layout.valid = false;
    diagOut = "";

    if (screenshot.width == 0 || screenshot.height == 0) {
        diagOut = "Empty screenshot";
        return layout;
    }

    // Hardcoded layout from known EU Skills window at 2554x1360
    // Window is always top-left at (0,0), size ~901x556
    layout.windowRect.left = 0;
    layout.windowRect.top = 0;
    layout.windowRect.right = 901;
    layout.windowRect.bottom = 556;

    // Columns (pixel offsets within window)
    layout.skillNameColX = 254;
    layout.skillNameColW = 296;  // 254 to 550
    layout.rankColX = 555;
    layout.rankColW = 210;  // 555 to 765
    layout.pointsColX = 770;
    layout.pointsColW = 126;  // 770 to 896

    // Rows
    layout.firstRowY = 87;
    layout.rowHeight = 25;
    layout.maxRows = 12;

    // Categories sidebar
    layout.categoryX = 5;
    layout.categoryW = 234;  // 254 - 20

    // Page indicator below last row
    layout.pageIndicatorY = 87 + 25 * 12 + 5;

    layout.valid = true;

    diagOut = "Hardcoded layout: (0,0)-(901,556) rows=12 firstRow=87 spacing=25";

    return layout;
}

ParseResult ParsePage(const Bitmap& screenshot, const SkillWindowLayout& layout, const FontAtlas& nameFont,
                      const FontAtlas& /*rankFont*/, const FontAtlas& numberFont, int listCursorIn) {
    ParseResult result;
    result.valid = false;
    result.currentPage = 0;
    result.totalPages = 0;
    result.totalPoints = 0;
    result.listCursorOut = listCursorIn;

    if (!layout.valid) return result;

    int wl = layout.windowRect.left;
    int wt = layout.windowRect.top;

    TextReader::ReadConfig numConfig;

    const auto& skillList = SkillData::GetSkillList();

    // Cursor into the skill list - continues from where previous page left off
    int listCursor = listCursorIn;

    for (int row = 0; row < layout.maxRows; row++) {
        int rowY = wt + layout.firstRowY + row * layout.rowHeight;

        // Check row has content
        int textPx = 0;
        bool isOrange = false;
        int orangePx = 0;
        int nameX = wl + layout.skillNameColX;
        for (int dy = 0; dy < layout.rowHeight; dy++)
            for (int x = nameX; x < nameX + layout.skillNameColW && x < screenshot.width; x += 3) {
                Pixel p = screenshot.pixel(x, rowY + dy);
                if (p.brightness() > 80 || p.isOrange()) textPx++;
                if (p.isOrange()) orangePx++;
            }
        if (textPx < 5) continue;
        isOrange = orangePx > 10;

        SkillEntry entry;
        entry.rank = "";

        // Get candidates: consecutive hidden skills + next non-hidden skill
        auto candidateIndices = SkillData::GetNextCandidates(listCursor);

        if (candidateIndices.empty()) break;  // past end of skill list

        if (candidateIndices.size() == 1) {
            // Only one candidate (non-hidden) - it MUST be this skill, no matching needed
            int idx = candidateIndices[0];
            entry.name = skillList[idx].name;
            listCursor = idx + 1;
        } else {
            // Multiple candidates - need to match
            std::vector<std::string> candidateNames;
            for (int idx : candidateIndices) candidateNames.push_back(skillList[idx].name);

            int readX = wl + layout.skillNameColX - 10;
            if (readX < wl) readX = wl;
            int readW = layout.skillNameColW + 10;

            auto match = TextReader::MatchSkillName(screenshot, readX, rowY, readW, layout.rowHeight, nameFont, candidateNames,
                                                    isOrange);

            if (match.valid) {
                entry.name = match.name;
                // Find which index matched and advance cursor past it
                for (int idx : candidateIndices) {
                    if (skillList[idx].name == match.name) {
                        listCursor = idx + 1;
                        break;
                    }
                }
            } else {
                // Match failed - assume it's the non-hidden skill (last candidate)
                int idx = candidateIndices.back();
                entry.name = skillList[idx].name;
                listCursor = idx + 1;
            }
        }

        // Read points - full column width (need up to 6 digits = 999999)
        // Only read top half of row: text is in top ~12px, progress bar is in bottom ~10px
        int ptsTextH = layout.rowHeight / 2 + 1;  // 13px of 25px row
        auto pts = TextReader::ReadNumber(screenshot, wl + layout.pointsColX, rowY, layout.pointsColW, ptsTextH, numberFont,
                                          numConfig);
        entry.points = pts.valid ? pts.value : 0;

        if (!entry.name.empty()) {
            result.skills.push_back(entry);
            if ((int)result.skills.size() >= 12) break;  // hard cap: max 12 per page
        }
    }

    result.listCursorOut = listCursor;

    auto pageInfo = ReadPageIndicator(screenshot, layout, numberFont);
    result.currentPage = pageInfo.current;
    result.totalPages = pageInfo.total;

    result.valid = !result.skills.empty();
    return result;
}

PageInfo ReadPageIndicator(const Bitmap& screenshot, const SkillWindowLayout& layout, const FontAtlas& font) {
    PageInfo info = {0, 0, false};

    int wl = layout.windowRect.left;
    int wt = layout.windowRect.top;
    int winW = layout.windowRect.right - layout.windowRect.left;

    int indicatorY = wt + layout.pageIndicatorY;
    int indicatorX = wl + winW / 3;
    int indicatorW = winW / 3;
    int indicatorH = 25;

    TextReader::ReadConfig config;
    config.minCharConfidence = TextReader::MIN_FEATURE_MATCH_CONFIDENCE;

    auto result = TextReader::ReadLine(screenshot, indicatorX, indicatorY, indicatorW, indicatorH, font, config);

    auto& text = result.text;
    size_t slash = text.find('/');
    if (slash != std::string::npos) {
        try {
            std::string before, after;
            for (size_t i = 0; i < slash; i++)
                if (text[i] >= '0' && text[i] <= '9') before += text[i];
            for (size_t i = slash + 1; i < text.size(); i++)
                if (text[i] >= '0' && text[i] <= '9') after += text[i];

            if (!before.empty() && !after.empty()) {
                info.current = std::stoi(before);
                info.total = std::stoi(after);
                info.valid = true;
            }
        } catch (...) {
        }
    }

    return info;
}

// ReadActiveCategory — currently unused
// std::string ReadActiveCategory(
//     const Bitmap& screenshot,
//     const SkillWindowLayout& layout,
//     const FontAtlas& font
// ) {
//     int wl = layout.windowRect.left;
//     int wt = layout.windowRect.top;
//
//     for (int y = wt + 30; y < layout.windowRect.bottom - 50; y++) {
//         for (int x = wl + 5; x < wl + layout.categoryW; x++) {
//             if (screenshot.pixel(x, y).isOrange()) {
//                 int readW = layout.categoryW - (x - wl);
//                 if (readW < 10) continue;
//                 TextReader::ReadConfig config;
//                 config.minCharConfidence = TextReader::MIN_FEATURE_MATCH_CONFIDENCE;
//
//                 auto result = TextReader::ReadLine(
//                     screenshot,
//                     x + 5, y - 5, readW, 20,
//                     font, config
//                 );
//                 return result.text;
//             }
//         }
//     }
//
//     return "ALL CATEGORIES";
// }

bool IsVisible(const Bitmap& screenshot, const SkillWindowLayout& layout) {
    if (!layout.valid) return false;

    int midX = (layout.windowRect.left + layout.windowRect.right) / 2;
    int midY = (layout.windowRect.top + layout.windowRect.bottom) / 2;

    int darkCount = 0;
    for (int dy = -10; dy <= 10; dy += 5) {
        for (int dx = -10; dx <= 10; dx += 5) {
            if (screenshot.pixel(midX + dx, midY + dy).brightness() < 60) darkCount++;
        }
    }

    bool hasAmberTop = false;
    for (int x = layout.windowRect.left; x < layout.windowRect.left + 50; x++) {
        if (IsAmber(screenshot.pixel(x, layout.windowRect.top))) {
            hasAmberTop = true;
            break;
        }
    }

    return darkCount > 10 && hasAmberTop;
}

}  // namespace SkillWindow
