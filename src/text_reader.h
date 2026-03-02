#pragma once
#include "font_atlas.h"
#include "types.h"

// ============================================================================
// Text Reader: Structural feature matching for skill name OCR
// ============================================================================

namespace TextReader {

// Shared OCR constants
static constexpr int DEFAULT_BRIGHTNESS_THRESHOLD = 80;
static constexpr float MIN_FEATURE_MATCH_CONFIDENCE = 0.3f;
static constexpr int WHITE_TEXT_MAX_SATURATION = 60;

struct ReadConfig {
    int brightnessThreshold = DEFAULT_BRIGHTNESS_THRESHOLD;
    float minCharConfidence = 0.5f;
    int maxCharGap = 3;
    bool useAdaptiveThreshold = true;
};

// Binarize a bitmap region - returns binary mask (0 or 255)
std::vector<uint8_t> BinarizeRegion(const Bitmap& bmp, int x, int y, int w, int h,
                                    int threshold = -1  // -1 for auto (Otsu)
);

// Character-by-character line reading (used for page indicators, categories)
struct CharResult {
    char ch;
    int x;
    float confidence;
};

struct LineResult {
    std::string text;
    float avgConfidence = 0;
    std::vector<CharResult> chars;
};

LineResult ReadLine(const Bitmap& bmp, int x, int y, int w, int h, const FontAtlas& atlas, const ReadConfig& config = {});

// Structural features extracted from a binarized text region
struct TextFeatures {
    int width = 0;       // bounding box width of text pixels
    int height = 0;      // bounding box height
    int pixelCount = 0;  // total foreground pixels
    int segments = 0;    // letter-group count (vertical projection gaps)
    bool hasDescender = false;
    bool valid = false;
};

// Extract structural features from a bitmap region
TextFeatures ExtractFeatures(const Bitmap& bmp, int x, int y, int w, int h, bool isOrange = false);

// Score how well two feature sets match (0.0 = no match, 1.0 = perfect)
float ScoreFeatures(const TextFeatures& observed, const TextFeatures& reference);

// Pre-render a skill name and extract its structural features
TextFeatures RenderSkillFeatures(const FontAtlas& atlas, const std::string& name);

// Skill name match result
struct SkillNameMatch {
    std::string name;
    float confidence = 0;
    bool valid = false;
};

// Match a screen region against candidate skill names
// candidates: list of skill names to try (pre-filtered by caller)
SkillNameMatch MatchSkillName(const Bitmap& bmp, int x, int y, int w, int h, const FontAtlas& atlas,
                              const std::vector<std::string>& candidates, bool isOrange = false);

// Read a number from a bitmap region
struct NumberResult {
    int value = 0;
    bool valid = false;
    float confidence = 0;
    std::string diag;  // diagnostic info for debugging
};

NumberResult ReadNumber(const Bitmap& bmp, int x, int y, int w, int h, const FontAtlas& atlas, const ReadConfig& config = {});

}  // namespace TextReader
