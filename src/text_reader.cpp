#include "text_reader.h"

#include <algorithm>
#include <cmath>

namespace TextReader {

// ============================================================================
// OCR constants (local to this translation unit)
// ============================================================================

static constexpr int TEMPLATE_GRID_WIDTH = 7;
static constexpr int TEMPLATE_GRID_HEIGHT = 9;
static constexpr int TEMPLATE_MATCH_ON_SCORE = 3;
static constexpr int TEMPLATE_MATCH_OFF_SCORE = 1;
static constexpr int MIN_DIGIT_TEMPLATE_SCORE = 65;
static constexpr int GLYPH_ALPHA_THRESHOLD = 64;

// ============================================================================
// Otsu binarization
// ============================================================================

static int ComputeOtsuThreshold(const uint8_t* data, int count) {
    int histogram[256] = {};
    for (int i = 0; i < count; i++) histogram[data[i]]++;

    float sum = 0;
    for (int i = 0; i < 256; i++) sum += i * histogram[i];

    float sumB = 0, wB = 0;
    float maxVar = 0;
    int threshold = 128;

    for (int t = 0; t < 256; t++) {
        wB += histogram[t];
        if (wB == 0) continue;
        float wF = count - wB;
        if (wF == 0) break;

        sumB += t * histogram[t];
        float mB = sumB / wB;
        float mF = (sum - sumB) / wF;
        float var = wB * wF * (mB - mF) * (mB - mF);
        if (var > maxVar) {
            maxVar = var;
            threshold = t;
        }
    }
    return threshold;
}

std::vector<uint8_t> BinarizeRegion(const Bitmap& bmp, int x, int y, int w, int h, int threshold) {
    std::vector<uint8_t> gray(w * h);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            Pixel p = bmp.pixel(x + col, y + row);
            gray[row * w + col] = (uint8_t)p.brightness();
        }

    if (threshold < 0) {
        threshold = ComputeOtsuThreshold(gray.data(), w * h);
        threshold = (std::max)(threshold, 80);
    }

    std::vector<uint8_t> binary(w * h);
    for (int i = 0; i < w * h; i++) binary[i] = gray[i] > threshold ? 255 : 0;
    return binary;
}

// Binarize using red channel (for orange highlighted text)
static std::vector<uint8_t> BinarizeOrange(const Bitmap& bmp, int x, int y, int w, int h) {
    std::vector<uint8_t> redCh(w * h);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            Pixel p = bmp.pixel(x + col, y + row);
            redCh[row * w + col] = p.r;
        }

    int threshold = ComputeOtsuThreshold(redCh.data(), w * h);
    threshold = (std::max)(threshold, 50);  // orange bg red is ~40

    std::vector<uint8_t> binary(w * h);
    for (int i = 0; i < w * h; i++) binary[i] = redCh[i] > threshold ? 255 : 0;
    return binary;
}

// ============================================================================
// Structural feature extraction
// ============================================================================

// Count letter segments by vertical projection gaps
static int CountSegments(const uint8_t* bin, int w, int /*h*/, int left, int right, int top, int bot) {
    int segments = 0;
    bool inLetter = false;
    int gapLen = 0;

    for (int col = left; col <= right; col++) {
        int colSum = 0;
        for (int row = top; row <= bot; row++)
            if (bin[row * w + col] > 128) colSum++;

        if (colSum > 0) {
            if (!inLetter && (segments == 0 || gapLen >= 1)) segments++;
            inLetter = true;
            gapLen = 0;
        } else {
            if (inLetter)
                gapLen = 1;
            else
                gapLen++;
            inLetter = false;
        }
    }
    return segments;
}

// Check for descender (ink in bottom 30% of text)
static bool CheckDescender(const uint8_t* bin, int w, int left, int right, int top, int bot) {
    int textH = bot - top + 1;
    int baseline = top + (int)(textH * 0.7f);
    int below = 0, total = 0;

    for (int row = top; row <= bot; row++)
        for (int col = left; col <= right; col++)
            if (bin[row * w + col] > 128) {
                total++;
                if (row >= baseline) below++;
            }

    return total > 0 && (float)below / total > 0.12f;
}

// Extract features from a binary image
static TextFeatures FeaturesFromBinary(const uint8_t* bin, int w, int h) {
    TextFeatures f;

    int top = h, bot = 0, left = w, right = 0;
    int px = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (bin[y * w + x] > 128) {
                top = (std::min)(top, y);
                bot = (std::max)(bot, y);
                left = (std::min)(left, x);
                right = (std::max)(right, x);
                px++;
            }

    if (px < 5 || left >= right || top >= bot) return f;

    f.width = right - left + 1;
    f.height = bot - top + 1;
    f.pixelCount = px;
    f.segments = CountSegments(bin, w, h, left, right, top, bot);
    f.hasDescender = CheckDescender(bin, w, left, right, top, bot);
    f.valid = true;
    return f;
}

TextFeatures ExtractFeatures(const Bitmap& bmp, int x, int y, int w, int h, bool isOrange) {
    std::vector<uint8_t> bin;
    if (isOrange)
        bin = BinarizeOrange(bmp, x, y, w, h);
    else
        bin = BinarizeRegion(bmp, x, y, w, h);

    return FeaturesFromBinary(bin.data(), w, h);
}

TextFeatures RenderSkillFeatures(const FontAtlas& atlas, const std::string& name) {
    int rw, rh;
    auto rendered = FontEngine::RenderString(atlas, name, rw, rh);
    if (rw <= 0 || rh <= 0) return {};

    // Binarize rendered glyphs
    std::vector<uint8_t> bin(rw * rh);
    for (int i = 0; i < rw * rh; i++) bin[i] = rendered[i] > GLYPH_ALPHA_THRESHOLD ? 255 : 0;

    return FeaturesFromBinary(bin.data(), rw, rh);
}

// ============================================================================
// Feature scoring
// ============================================================================

float ScoreFeatures(const TextFeatures& obs, const TextFeatures& ref) {
    if (!obs.valid || !ref.valid) return 0;

    // Width similarity (most discriminating and reliable across renderers)
    float widthRatio = (float)obs.width / ref.width;
    if (widthRatio < 0.55f || widthRatio > 1.55f) return 0;
    float widthDiff = fabsf(widthRatio - 1.0f);
    float widthScore = 1.0f - widthDiff * widthDiff * 10.0f;
    if (widthScore < 0) widthScore = 0;

    // Segment count (unreliable - Scaleform merges letters differently than GDI)
    // Use soft penalty only, don't reward exact match heavily
    int segDiff = abs(obs.segments - ref.segments);
    float segScore = 1.0f;
    if (segDiff == 1)
        segScore = 0.85f;
    else if (segDiff == 2)
        segScore = 0.65f;
    else if (segDiff == 3)
        segScore = 0.45f;
    else if (segDiff >= 4)
        segScore = 0.30f;

    // Descender
    float descScore = (obs.hasDescender == ref.hasDescender) ? 1.0f : 0.5f;

    // Pixel density
    float densRatio = (float)obs.pixelCount / (ref.pixelCount > 0 ? ref.pixelCount : 1);
    float densDiff = fabsf(densRatio - 1.0f);
    float densScore = 1.0f - densDiff * densDiff * 5.0f;
    if (densScore < 0) densScore = 0;

    // Width is king (60%), segments are supporting (15%), density helps (15%), descender (10%)
    return widthScore * 0.60f + segScore * 0.15f + descScore * 0.10f + densScore * 0.15f;
}

// ============================================================================
// Skill name matching
// ============================================================================

SkillNameMatch MatchSkillName(const Bitmap& bmp, int x, int y, int w, int h, const FontAtlas& atlas,
                              const std::vector<std::string>& candidates, bool isOrange) {
    SkillNameMatch result;

    TextFeatures observed = ExtractFeatures(bmp, x, y, w, h, isOrange);
    if (!observed.valid) return result;

    float bestScore = 0;
    int bestIdx = -1;

    for (int i = 0; i < (int)candidates.size(); i++) {
        TextFeatures ref = RenderSkillFeatures(atlas, candidates[i]);
        float score = ScoreFeatures(observed, ref);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0 && bestScore > MIN_FEATURE_MATCH_CONFIDENCE) {
        result.name = candidates[bestIdx];
        result.confidence = bestScore;
        result.valid = true;
    }
    return result;
}

// ============================================================================
// Character-by-character line reading (for page indicators, categories)
// ============================================================================

static int FindTextTop(const uint8_t* binary, int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (binary[y * w + x] > 0) return y;
    return 0;
}

static float MatchGlyph(const uint8_t* bin, int imgW, int imgH, int posX, int posY, const GlyphInfo& glyph) {
    if (glyph.width == 0 || glyph.height == 0) return 0;
    if (posX + glyph.width > imgW || posY + glyph.height > imgH) return 0;
    if (posX < 0 || posY < 0) return 0;

    float sumAB = 0, sumAA = 0, sumBB = 0;
    for (int y = 0; y < glyph.height; y++)
        for (int x = 0; x < glyph.width; x++) {
            float a = bin[(posY + y) * imgW + (posX + x)] / 255.0f;
            float b = (glyph.alpha[y * glyph.width + x] > GLYPH_ALPHA_THRESHOLD) ? 1.0f : 0.0f;
            sumAB += a * b;
            sumAA += a * a;
            sumBB += b * b;
        }

    float denom = sqrtf(sumAA * sumBB);
    return denom > 0 ? sumAB / denom : 0;
}

LineResult ReadLine(const Bitmap& bmp, int x, int y, int w, int h, const FontAtlas& atlas, const ReadConfig& config) {
    LineResult result;

    auto binary = BinarizeRegion(bmp, x, y, w, h, config.useAdaptiveThreshold ? -1 : config.brightnessThreshold);

    int textTop = FindTextTop(binary.data(), w, h);

    // Skip leading blank columns
    int curX = 0;
    while (curX < w) {
        bool has = false;
        for (int row = 0; row < h; row++)
            if (binary[row * w + curX] > 0) {
                has = true;
                break;
            }
        if (has) break;
        curX++;
    }

    float totalConf = 0;
    int charCount = 0;
    int emptyRun = 0;

    while (curX < w - 2) {
        bool hasContent = false;
        for (int row = 0; row < h; row++)
            if (binary[row * w + curX] > 0) {
                hasContent = true;
                break;
            }

        if (!hasContent) {
            emptyRun++;
            if (emptyRun > atlas.lineHeight) break;
            int spaceW = atlas.lineHeight / 3;
            auto spIt = atlas.glyphs.find(' ');
            if (spIt != atlas.glyphs.end()) spaceW = spIt->second.advanceX;
            if (emptyRun > config.maxCharGap + spaceW) {
                if (!result.text.empty() && result.text.back() != ' ') {
                    result.text += ' ';
                    result.chars.push_back({' ', curX, 1.0f});
                }
            }
            curX++;
            continue;
        }
        emptyRun = 0;

        float bestScore = config.minCharConfidence;
        char bestChar = 0;
        int bestAdv = 1;

        for (auto& [ch, glyph] : atlas.glyphs) {
            if (ch == ' ' || glyph.width == 0) continue;
            if (glyph.width < 3 || glyph.height < 4) continue;

            for (int yo = -2; yo <= 2; yo++) {
                int py = textTop + yo;
                if (py < 0 || py + glyph.height > h) continue;
                float score = MatchGlyph(binary.data(), w, h, curX, py, glyph);
                if (score > bestScore) {
                    bestScore = score;
                    bestChar = ch;
                    bestAdv = glyph.advanceX > 0 ? glyph.advanceX : glyph.width;
                }
            }
        }

        if (bestChar) {
            result.text += bestChar;
            result.chars.push_back({bestChar, curX, bestScore});
            totalConf += bestScore;
            charCount++;
            curX += bestAdv;
        } else {
            curX++;
        }
    }

    result.avgConfidence = charCount > 0 ? totalConf / charCount : 0;
    return result;
}

// ============================================================================
// Number reading - normalized grid comparison
//
// Templates extracted from actual Scaleform-rendered digits at 2554x1360.
// Each digit is 7 columns x 9 rows, stored as bit patterns.
//
// Approach:
//   1. Binarize the points region (white/gray + orange text, reject teal bars)
//   2. Segment into blobs by column gaps
//   3. Split wide blobs (merged digits) recursively
//   4. For each blob:
//      a. Extract content columns (trim empty left/right)
//      b. Right-align into a standard 7×9 grid
//      c. Score against all 20 right-aligned templates (10 white + 10 orange)
//      d. Pick highest score
//
// Right-alignment eliminates positional jitter and width variance.
// Every comparison is two 7×9 binary grids, apples-to-apples.
// ============================================================================

struct DigitTemplate {
    uint8_t rows[TEMPLATE_GRID_HEIGHT];  // bit 6=leftmost, bit 0=rightmost, 7 cols per row
};

// clang-format off

// Templates extracted from game screenshot (white text, 9pt)
static const DigitTemplate DIGIT_TEMPLATES[10] = {
    // 0: .###... / .####.. / ##..##. / ##..##. / ##..##. / ##..##. / ##..##. / .####.. / .####..
    {{0x38, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x3C}},
    // 1: ..##... / .###... / ####... / #.##... / ..##... / ..##... / ..##... / ..##... / ..##...
    {{0x18, 0x38, 0x78, 0x58, 0x18, 0x18, 0x18, 0x18, 0x18}},
    // 2: .####.. / .#####. / .#..##. / ....##. / ...##.. / ..###.. / .###... / .#####. / ######.
    {{0x3C, 0x3E, 0x26, 0x06, 0x0C, 0x1C, 0x38, 0x3E, 0x7E}},
    // 3: ####... / ##.##.. / ...##.. / ..##... / ..###.. / ...##.. / #..##.. / #####.. / ####...
    {{0x78, 0x6C, 0x0C, 0x18, 0x1C, 0x0C, 0x4C, 0x7C, 0x78}},
    // 4: ...##.. / ..###.. / ..###.. / .####.. / ##.##.. / ######. / ######. / ...##.. / ...##..
    {{0x0C, 0x1C, 0x1C, 0x3C, 0x6C, 0x7E, 0x7E, 0x0C, 0x0C}},
    // 5: #####.. / #####.. / ##..... / ####... / ##.##.. / ...##.. / #..##.. / #####.. / ####...
    {{0x7C, 0x7C, 0x60, 0x78, 0x6C, 0x0C, 0x4C, 0x7C, 0x78}},
    // 6: ..###.. / .##.##. / .#..... / #####.. / ###.##. / ##..##. / .#..##. / .#####. / ..###..
    {{0x1C, 0x36, 0x20, 0x7C, 0x76, 0x66, 0x26, 0x3E, 0x1C}},
    // 7: ######. / ######. / ...##.. / ...#... / ..##... / ..##... / ..#.... / .##.... / .##....
    {{0x7E, 0x7E, 0x0C, 0x08, 0x18, 0x18, 0x10, 0x30, 0x30}},
    // 8: .###... / ##.##.. / ##.##.. / .###... / #####.. / ##..#.. / ##..#.. / #####.. / .###...
    {{0x38, 0x6C, 0x6C, 0x38, 0x7C, 0x64, 0x64, 0x7C, 0x38}},
    // 9: .####.. / ###.##. / ##..##. / .#..##. / .#####. / ....##. / .#..##. / .####.. / .####..
    {{0x3C, 0x76, 0x66, 0x26, 0x3E, 0x06, 0x26, 0x3C, 0x3C}},
};

// Orange (selected row) templates - different glyph rendering from Scaleform
static const DigitTemplate ORANGE_TEMPLATES[10] = {
    // O0: .####.. / .####.. / ##.###. / ##..##. / ##..##. / ##..##. / ##.###. / #####.. / .####..
    {{0x3C, 0x3C, 0x6E, 0x66, 0x66, 0x66, 0x6E, 0x7C, 0x3C}},
    // O1: ..##... / .###... / ####... / #.##... / ..##... / ..##... / ..##... / ..##... / ..##...
    {{0x18, 0x38, 0x78, 0x58, 0x18, 0x18, 0x18, 0x18, 0x18}},
    // O2: .####.. / .#####. / .#..##. / ....##. / ...###. / ..###.. / .###... / ######. / ######.
    {{0x3C, 0x3E, 0x26, 0x06, 0x0E, 0x1C, 0x38, 0x7E, 0x7E}},
    // O3: .####.. / #####.. / ...##.. / ..###.. / ...##.. / ....##. / ##..##. / ######. / .####..
    {{0x3C, 0x7C, 0x0C, 0x1C, 0x0C, 0x06, 0x66, 0x7E, 0x3C}},
    // O4: ...##.. / ..###.. / .####.. / .####.. / ##.##.. / ######. / ######. / ...##.. / ...##..
    {{0x0C, 0x1C, 0x3C, 0x3C, 0x6C, 0x7E, 0x7E, 0x0C, 0x0C}},
    // O5: #####.. / #####.. / ##..... / ####... / #####.. / ...##.. / #..##.. / #####.. / ...#...
    {{0x7C, 0x7C, 0x60, 0x78, 0x7C, 0x0C, 0x4C, 0x7C, 0x08}},
    // O6: ..###.. / .#####. / ###.#.. / #####.. / ######. / ###.##. / ###.##. / .#####. / ..###..
    {{0x1C, 0x3E, 0x74, 0x7C, 0x7E, 0x76, 0x76, 0x3E, 0x1C}},
    // O7: ######. / ######. / ...##.. / ...##.. / ..##... / ..##... / ..#.... / .#..... / .##....
    {{0x7E, 0x7E, 0x0C, 0x0C, 0x18, 0x18, 0x10, 0x20, 0x30}},
    // O8: ..###.. / .#####. / .#####. / ..####. / .#####. / ###.##. / ###.##. / .#####. / ..####.
    {{0x1C, 0x3E, 0x3E, 0x1E, 0x3E, 0x76, 0x76, 0x3E, 0x1E}},
    // O9: .####.. / ######. / ##..##. / ##..##. / .#####. / ..####. / .#..##. / .#####. / .####..
    {{0x3C, 0x7E, 0x66, 0x66, 0x3E, 0x1E, 0x26, 0x3E, 0x3C}},
};

// Alternate white templates — subpixel rendering variants at different x positions
struct AltTemplate {
    int digit;          // which digit (0-9) this is an alternate for
    DigitTemplate tmpl;
};

static const AltTemplate DIGIT_TEMPLATES_ALT[] = {
    // 0 at x19-23: fixes 0→8 confusion
    {0, {{0x0E, 0x1F, 0x1B, 0x19, 0x19, 0x19, 0x1B, 0x1F, 0x0E}}},
    // 3 at x12-17: fixes 3→8 confusion (dominant error, ~19 occurrences)
    {3, {{0x1E, 0x36, 0x06, 0x0E, 0x06, 0x03, 0x33, 0x3F, 0x1E}}},
    // 3 at x19-24: fixes 3→8 confusion
    {3, {{0x1C, 0x36, 0x06, 0x0C, 0x0E, 0x03, 0x33, 0x3E, 0x1C}}},
    // 5 at x19-24: fixes 5→6 confusion (~10 occurrences)
    {5, {{0x1E, 0x1E, 0x30, 0x3E, 0x3E, 0x03, 0x23, 0x3E, 0x1C}}},
    // 5 variant 2: row 8 subpixel shift (1C→1E), fixes 5→6 at tight margin
    {5, {{0x1E, 0x1E, 0x30, 0x3E, 0x3E, 0x03, 0x23, 0x3E, 0x1E}}},
    // 5 variant 3: rows 1,8 subpixel shift, fixes 5→6 confusion
    {5, {{0x1E, 0x3E, 0x30, 0x3E, 0x3E, 0x03, 0x23, 0x3E, 0x1E}}},
    // 6 at x19-24: fixes 6→0 confusion
    {6, {{0x1E, 0x3E, 0x30, 0x3E, 0x3E, 0x33, 0x33, 0x3E, 0x1E}}},
    // 8 variant 1 at x12-17: fixes 8→6 confusion
    {8, {{0x1E, 0x37, 0x32, 0x1E, 0x3E, 0x33, 0x33, 0x3F, 0x1E}}},
    // 8 variant 2 at x12-17: fixes 8→5 confusion
    {8, {{0x1E, 0x37, 0x32, 0x1E, 0x1E, 0x33, 0x33, 0x3F, 0x1E}}},
    // 8 variant 3 at x25-30: fixes 8→6 confusion
    {8, {{0x1E, 0x1B, 0x13, 0x1E, 0x1F, 0x33, 0x33, 0x1F, 0x1E}}},
    // 8 variant 4 at x25-30: fixes 8→5 confusion
    {8, {{0x1E, 0x1B, 0x1B, 0x1E, 0x1F, 0x33, 0x33, 0x1F, 0x1E}}},
    // 9 at x19-23: fixes 9→8 confusion
    {9, {{0x0E, 0x1B, 0x13, 0x1B, 0x1F, 0x01, 0x1B, 0x1F, 0x0E}}},
};
static const int NUM_WHITE_ALTS = sizeof(DIGIT_TEMPLATES_ALT) / sizeof(DIGIT_TEMPLATES_ALT[0]);

// Alternate orange templates — subpixel rendering variants
static const AltTemplate ORANGE_TEMPLATES_ALT[] = {
    // 3 variant 1: fixes 3→9 confusion
    {3, {{0x1E, 0x1B, 0x03, 0x06, 0x07, 0x03, 0x13, 0x1F, 0x0E}}},
    // 3 variant 2: fixes 3→8 confusion
    {3, {{0x0E, 0x1B, 0x03, 0x06, 0x03, 0x01, 0x11, 0x1F, 0x0E}}},
    // 7: fixes 7→2 confusion
    {7, {{0x1F, 0x1F, 0x03, 0x06, 0x06, 0x04, 0x0C, 0x0C, 0x0C}}},
    // 8: fixes 8→6 confusion
    {8, {{0x1E, 0x1B, 0x13, 0x1E, 0x1F, 0x33, 0x33, 0x1F, 0x0E}}},
    // 9 at x19-23: fixes 9→8 confusion (~3 occurrences)
    {9, {{0x0E, 0x1B, 0x13, 0x1B, 0x1F, 0x01, 0x13, 0x1F, 0x0E}}},
};
static const int NUM_ORANGE_ALTS = sizeof(ORANGE_TEMPLATES_ALT) / sizeof(ORANGE_TEMPLATES_ALT[0]);

// clang-format on

// ============================================================================
// Normalized 7×9 grid: right-aligned pixel data for consistent comparison
// ============================================================================

struct NormGrid {
    uint8_t px[TEMPLATE_GRID_HEIGHT][TEMPLATE_GRID_WIDTH];  // [row][col], 0 or 1. col 6 = rightmost
    int contentW;  // original content width before alignment
};

// Right-align a template into a NormGrid
static NormGrid NormalizeTemplate(const DigitTemplate& tmpl) {
    NormGrid g = {};

    // Find rightmost column with any content
    int rightmost = -1;
    for (int row = 0; row < TEMPLATE_GRID_HEIGHT; row++)
        for (int col = 0; col < TEMPLATE_GRID_WIDTH; col++)
            if ((tmpl.rows[row] >> (TEMPLATE_GRID_WIDTH - 1 - col)) & 1)
                if (col > rightmost) rightmost = col;

    if (rightmost < 0) return g;

    // Find leftmost
    int leftmost = TEMPLATE_GRID_WIDTH;
    for (int row = 0; row < TEMPLATE_GRID_HEIGHT; row++)
        for (int col = 0; col < TEMPLATE_GRID_WIDTH; col++)
            if ((tmpl.rows[row] >> (TEMPLATE_GRID_WIDTH - 1 - col)) & 1)
                if (col < leftmost) leftmost = col;

    g.contentW = rightmost - leftmost + 1;
    int shift = (TEMPLATE_GRID_WIDTH - 1) - rightmost;  // push rightmost content to last col

    for (int row = 0; row < TEMPLATE_GRID_HEIGHT; row++)
        for (int col = 0; col < TEMPLATE_GRID_WIDTH; col++)
            if ((tmpl.rows[row] >> (TEMPLATE_GRID_WIDTH - 1 - col)) & 1) {
                int nc = col + shift;
                if (nc >= 0 && nc < TEMPLATE_GRID_WIDTH) g.px[row][nc] = 1;
            }

    return g;
}

// Pre-computed right-aligned templates (initialized on first use)
// Each digit can have multiple templates (primary + alternates for subpixel variants)
static std::vector<NormGrid> ALL_WHITE[10];
static std::vector<NormGrid> ALL_ORANGE[10];
static bool g_normsInitialized = false;

static void InitNormalizedTemplates() {
    if (g_normsInitialized) return;
    for (int d = 0; d < 10; d++) {
        ALL_WHITE[d].push_back(NormalizeTemplate(DIGIT_TEMPLATES[d]));
        ALL_ORANGE[d].push_back(NormalizeTemplate(ORANGE_TEMPLATES[d]));
    }
    for (int i = 0; i < NUM_WHITE_ALTS; i++)
        ALL_WHITE[DIGIT_TEMPLATES_ALT[i].digit].push_back(NormalizeTemplate(DIGIT_TEMPLATES_ALT[i].tmpl));
    for (int i = 0; i < NUM_ORANGE_ALTS; i++)
        ALL_ORANGE[ORANGE_TEMPLATES_ALT[i].digit].push_back(NormalizeTemplate(ORANGE_TEMPLATES_ALT[i].tmpl));
    g_normsInitialized = true;
}

// Helper: read a pixel from the binarized image
static int ReadPx(const uint8_t* binary, int imgW, int imgH, int x, int y) {
    if (x >= 0 && x < imgW && y >= 0 && y < imgH) return binary[y * imgW + x] > 0 ? 1 : 0;
    return 0;
}

// Extract a blob from the binary image and right-align into a NormGrid
static NormGrid NormalizeBlob(const uint8_t* binary, int imgW, int imgH, int blobX0, int blobX1, int textTop, int textH) {
    NormGrid g = {};
    int rows = (textH < TEMPLATE_GRID_HEIGHT) ? textH : TEMPLATE_GRID_HEIGHT;

    // Find actual content bounds within the blob
    int contentLeft = blobX1 + 1, contentRight = blobX0 - 1;
    for (int row = 0; row < rows; row++)
        for (int x = blobX0; x <= blobX1; x++)
            if (ReadPx(binary, imgW, imgH, x, textTop + row)) {
                if (x < contentLeft) contentLeft = x;
                if (x > contentRight) contentRight = x;
            }

    if (contentLeft > contentRight) return g;

    g.contentW = contentRight - contentLeft + 1;
    if (g.contentW > TEMPLATE_GRID_WIDTH) g.contentW = TEMPLATE_GRID_WIDTH;  // cap

    // Right-align: content right edge → last grid col
    int gridOffset = (TEMPLATE_GRID_WIDTH - 1) - (contentRight - contentLeft);
    if (gridOffset < 0) gridOffset = 0;

    for (int row = 0; row < rows; row++)
        for (int x = contentLeft; x <= contentRight; x++) {
            int gc = gridOffset + (x - contentLeft);
            if (gc >= 0 && gc < TEMPLATE_GRID_WIDTH && ReadPx(binary, imgW, imgH, x, textTop + row))
                g.px[row][gc] = 1;
        }

    return g;
}

// Score: weighted matching between two NormGrids
// ON-ON match = 3 points (distinctive feature), OFF-OFF match = 1 point (background)
// This prevents sparse templates (e.g. '3') from getting free points via empty space.
// Max score varies per template (~85-133 depending on ON pixel count).
static int ScoreNormalized(const NormGrid& observed, const NormGrid& tmpl) {
    int score = 0;
    for (int r = 0; r < TEMPLATE_GRID_HEIGHT; r++)
        for (int c = 0; c < TEMPLATE_GRID_WIDTH; c++)
            if (observed.px[r][c] == tmpl.px[r][c])
                score += observed.px[r][c] ? TEMPLATE_MATCH_ON_SCORE : TEMPLATE_MATCH_OFF_SCORE;
    return score;
}

// Score a blob against templates, return best score
// When isOrange is specified, only scores against the matching template set.
static int BestNormScore(const uint8_t* binary, int imgW, int imgH, int blobX0, int blobX1, int textTop, int textH,
                         int isOrange = -1  // -1=both, 0=white only, 1=orange only
) {
    InitNormalizedTemplates();
    NormGrid obs = NormalizeBlob(binary, imgW, imgH, blobX0, blobX1, textTop, textH);
    int best = 0;
    for (int d = 0; d < 10; d++) {
        if (isOrange <= 0) {  // white
            for (const auto& tmpl : ALL_WHITE[d]) {
                int s = ScoreNormalized(obs, tmpl);
                if (s > best) best = s;
            }
        }
        if (isOrange != 0) {  // orange
            for (const auto& tmpl : ALL_ORANGE[d]) {
                int s = ScoreNormalized(obs, tmpl);
                if (s > best) best = s;
            }
        }
    }
    return best;
}

// Classify a blob: right-align and compare against templates
// Returns '0'-'9' or 0 if no match, fills outScores[20] if non-null
// isOrange: -1=both sets, 0=white only, 1=orange only
static char ClassifyNormalized(const uint8_t* binary, int imgW, int imgH, int blobX0, int blobX1, int textTop, int textH,
                               bool isOrange = false,
                               int* outScores = nullptr  // [20]: white0-9, orange0-9
) {
    InitNormalizedTemplates();
    NormGrid obs = NormalizeBlob(binary, imgW, imgH, blobX0, blobX1, textTop, textH);

    int bestScore = 0;
    int bestIdx = -1;

    for (int d = 0; d < 10; d++) {
        // Score against all white templates for this digit, take max
        int sw = 0;
        for (const auto& tmpl : ALL_WHITE[d]) {
            int s = ScoreNormalized(obs, tmpl);
            if (s > sw) sw = s;
        }
        // Score against all orange templates for this digit, take max
        int so = 0;
        for (const auto& tmpl : ALL_ORANGE[d]) {
            int s = ScoreNormalized(obs, tmpl);
            if (s > so) so = s;
        }
        if (outScores) {
            outScores[d] = sw;
            outScores[10 + d] = so;
        }

        // Only consider templates matching the detected text color
        if (!isOrange) {
            if (sw > bestScore) {
                bestScore = sw;
                bestIdx = d;
            }
        } else {
            if (so > bestScore) {
                bestScore = so;
                bestIdx = d;
            }
        }
    }

    // Require minimum match quality. With ON/OFF weighting,
    // perfect self-scores range from ~107 (digit 1) to ~133 (digit 0).
    // MIN_DIGIT_TEMPLATE_SCORE (~50-60% of typical score) rejects noise.
    if (bestScore < MIN_DIGIT_TEMPLATE_SCORE || bestIdx < 0) return 0;
    return (char)('0' + bestIdx);
}

// Binarize keeping only text pixels (white/gray or orange), not teal bars.
// Also detects whether the region contains orange text.
static std::vector<uint8_t> BinarizeNumberRegion(const Bitmap& bmp, int x, int y, int w, int h, bool* outIsOrange = nullptr) {
    std::vector<uint8_t> binary(w * h, 0);
    int orangeCount = 0, whiteCount = 0;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            Pixel p = bmp.pixel(x + col, y + row);
            int br = p.brightness();
            // Orange text - relaxed threshold to capture anti-aliased pixels.
            if (p.r > 120 && p.r > p.g && p.g > p.b && (p.r - p.b) > 40 && br > 60) {
                binary[row * w + col] = 255;
                orangeCount++;
                continue;
            }
            // White/gray text (neutral color)
            if (br > 100) {
                int maxC = p.r;
                if (p.g > maxC) maxC = p.g;
                if (p.b > maxC) maxC = p.b;
                int minC = p.r;
                if (p.g < minC) minC = p.g;
                if (p.b < minC) minC = p.b;
                if (maxC - minC < WHITE_TEXT_MAX_SATURATION) {
                    binary[row * w + col] = 255;
                    whiteCount++;
                }
            }
        }
    if (outIsOrange) *outIsOrange = (orangeCount > whiteCount);
    return binary;
}

NumberResult ReadNumber(const Bitmap& bmp, int x, int y, int w, int h, const FontAtlas& /*atlas*/, const ReadConfig& /*config*/
) {
    NumberResult result;
    InitNormalizedTemplates();

    bool isOrange = false;
    auto binary = BinarizeNumberRegion(bmp, x, y, w, h, &isOrange);

    // Find text vertical bounds
    int textTop = h, textBot = 0;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            if (binary[row * w + col] > 0) {
                textTop = (std::min)(textTop, row);
                textBot = (std::max)(textBot, row);
            }

    if (textTop > textBot) return result;
    int textH = textBot - textTop + 1;

    // Segment into blobs by finding column gaps
    struct Blob {
        int x0, x1;
    };
    std::vector<Blob> blobs;
    int blobStart = -1;

    for (int col = 0; col <= w; col++) {
        bool hasContent = false;
        if (col < w) {
            for (int row = textTop; row <= textBot; row++)
                if (binary[row * w + col] > 0) {
                    hasContent = true;
                    break;
                }
        }

        if (hasContent && blobStart < 0) {
            blobStart = col;
        } else if (!hasContent && blobStart >= 0) {
            blobs.push_back({blobStart, col - 1});
            blobStart = -1;
        }
    }

    if (blobs.empty()) return result;

    // Diagnostic: initial blob info
    std::string initBlobInfo = "initBlobs=" + std::to_string(blobs.size()) + " [";
    for (const auto& b : blobs) {
        initBlobInfo += "x" + std::to_string(b.x0) + "-" + std::to_string(b.x1) + "w" + std::to_string(b.x1 - b.x0 + 1) + " ";
    }
    initBlobInfo += "] textH=" + std::to_string(textH) + " top=" + std::to_string(textTop) + (isOrange ? " ORANGE" : " WHITE");

    // Split blobs that are too wide (merged digits)
    // Uses recursive splitting with normalized scoring.

    auto findFirstContent = [&](int from, int to) -> int {
        for (int col = from; col <= to; col++) {
            for (int row = textTop; row <= textBot; row++)
                if (binary[row * w + col] > 0) return col;
        }
        return to + 1;
    };

    std::function<void(int, int, std::vector<Blob>&, int)> splitBlob;
    splitBlob = [&](int x0, int x1, std::vector<Blob>& out, int depth) {
        int bw = x1 - x0 + 1;
        if (bw <= 8 || depth >= 3) {
            out.push_back({x0, x1});
            return;
        }

        int bestScore = 0, bestCol = -1;
        for (int sc = x0 + 3; sc <= x1 - 2; sc++) {
            int rs = findFirstContent(sc, x1);
            if (rs > x1) continue;
            int ls = BestNormScore(binary.data(), w, h, x0, sc - 1, textTop, textH, isOrange ? 1 : 0);
            int rrs = BestNormScore(binary.data(), w, h, rs, x1, textTop, textH, isOrange ? 1 : 0);
            int combined = (std::min)(ls, rrs);
            if (combined > bestScore) {
                bestScore = combined;
                bestCol = sc;
            }
        }

        if (bestCol >= 0 && bestScore >= MIN_DIGIT_TEMPLATE_SCORE) {
            splitBlob(x0, bestCol - 1, out, depth + 1);
            int rs = findFirstContent(bestCol, x1);
            if (rs <= x1) splitBlob(rs, x1, out, depth + 1);
        } else {
            out.push_back({x0, x1});
        }
    };

    std::vector<Blob> splitBlobs;
    for (const auto& b : blobs) {
        splitBlob(b.x0, b.x1, splitBlobs, 0);
    }

    // Classify each blob using normalized grid comparison
    std::string numStr;
    std::string diagStr = "blobs=" + std::to_string(splitBlobs.size()) + " [";
    for (const auto& b : splitBlobs) {
        int bw = b.x1 - b.x0 + 1;
        int scores[20] = {};
        char digit = ClassifyNormalized(binary.data(), w, h, b.x0, b.x1, textTop, textH, isOrange, scores);

        // Get the actual observed NormGrid for diagnostics
        NormGrid obsGrid = NormalizeBlob(binary.data(), w, h, b.x0, b.x1, textTop, textH);

        // Best template info for diagnostics
        int bestScore = 0, bestD = -1;
        for (int i = 0; i < (isOrange ? 20 : 10); i++) {
            if (scores[i] > bestScore) {
                bestScore = scores[i];
                bestD = i;
            }
        }

        diagStr += "x" + std::to_string(b.x0) + "-" + std::to_string(b.x1) + "w" + std::to_string(bw) + "→";
        if (digit) {
            diagStr += digit;
            diagStr += "(n" + std::to_string(bestD) + "@" + std::to_string(bestScore);
            // Show all template scores for diagnostics
            diagStr += " w[";
            for (int i = 0; i < 10; i++) diagStr += std::to_string(scores[i]) + (i < 9 ? "," : "");
            diagStr += "] o[";
            for (int i = 0; i < 10; i++) diagStr += std::to_string(scores[10 + i]) + (i < 9 ? "," : "");
            diagStr += "]";
            // Dump observed NormGrid as hex (same format as templates)
            // so we can compare against template definitions
            diagStr += " grid[";
            for (int r = 0; r < TEMPLATE_GRID_HEIGHT; r++) {
                uint8_t rowBits = 0;
                for (int c = 0; c < TEMPLATE_GRID_WIDTH; c++)
                    if (obsGrid.px[r][c]) rowBits |= (1 << (TEMPLATE_GRID_WIDTH - 1 - c));
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X", rowBits);
                diagStr += hex;
                if (r < TEMPLATE_GRID_HEIGHT - 1) diagStr += ",";
            }
            diagStr += "]";
            diagStr += ") ";
            numStr += digit;
        } else {
            diagStr += "?(n" + std::to_string(bestD) + "@" + std::to_string(bestScore) + ") ";
        }
    }
    diagStr += "] " + initBlobInfo;

    if (numStr.empty() || numStr.size() > 7) {
        // Empty or unreasonably long (max game value is 6 digits)
        result.diag = diagStr;
        return result;
    }

    try {
        result.value = std::stoi(numStr);
    } catch (...) {
        result.diag = diagStr;
        return result;
    }
    result.confidence = 1.0f;
    result.valid = true;
    result.diag = diagStr;
    return result;
}

}  // namespace TextReader
