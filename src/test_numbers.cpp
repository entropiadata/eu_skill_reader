// ============================================================================
// Regression tests for number recognition (normalized grid matching)
//
// Build: cl /EHsc /O2 /std:c++17 /DNOMINMAX test_numbers.cpp text_reader.cpp
//        font_atlas.cpp /Fe:test_numbers.exe /link user32.lib gdi32.lib
//
// Run:   test_numbers.exe
// ============================================================================

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "text_reader.h"
#include "types.h"

// ============================================================================
// Template data (duplicated from text_reader.cpp for test access)
// ============================================================================

struct DigitTemplate {
    uint8_t rows[9];
};

static const DigitTemplate DIGIT_TEMPLATES[10] = {
    {{0x38, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x3C}},  // 0
    {{0x18, 0x38, 0x78, 0x58, 0x18, 0x18, 0x18, 0x18, 0x18}},  // 1
    {{0x3C, 0x3E, 0x26, 0x06, 0x0C, 0x1C, 0x38, 0x3E, 0x7E}},  // 2
    {{0x78, 0x6C, 0x0C, 0x18, 0x1C, 0x0C, 0x4C, 0x7C, 0x78}},  // 3
    {{0x0C, 0x1C, 0x1C, 0x3C, 0x6C, 0x7E, 0x7E, 0x0C, 0x0C}},  // 4
    {{0x7C, 0x7C, 0x60, 0x78, 0x6C, 0x0C, 0x4C, 0x7C, 0x78}},  // 5
    {{0x1C, 0x36, 0x20, 0x7C, 0x76, 0x66, 0x26, 0x3E, 0x1C}},  // 6
    {{0x7E, 0x7E, 0x0C, 0x08, 0x18, 0x18, 0x10, 0x30, 0x30}},  // 7
    {{0x38, 0x6C, 0x6C, 0x38, 0x7C, 0x64, 0x64, 0x7C, 0x38}},  // 8
    {{0x3C, 0x76, 0x66, 0x26, 0x3E, 0x06, 0x26, 0x3C, 0x3C}},  // 9
};

static const DigitTemplate ORANGE_TEMPLATES[10] = {
    {{0x3C, 0x3C, 0x6E, 0x66, 0x66, 0x66, 0x6E, 0x7C, 0x3C}},  // O0
    {{0x18, 0x38, 0x78, 0x58, 0x18, 0x18, 0x18, 0x18, 0x18}},  // O1
    {{0x3C, 0x3E, 0x26, 0x06, 0x0E, 0x1C, 0x38, 0x7E, 0x7E}},  // O2
    {{0x3C, 0x7C, 0x0C, 0x1C, 0x0C, 0x06, 0x66, 0x7E, 0x3C}},  // O3
    {{0x0C, 0x1C, 0x3C, 0x3C, 0x6C, 0x7E, 0x7E, 0x0C, 0x0C}},  // O4
    {{0x7C, 0x7C, 0x60, 0x78, 0x7C, 0x0C, 0x4C, 0x7C, 0x08}},  // O5
    {{0x1C, 0x3E, 0x74, 0x7C, 0x7E, 0x76, 0x76, 0x3E, 0x1C}},  // O6
    {{0x7E, 0x7E, 0x0C, 0x0C, 0x18, 0x18, 0x10, 0x20, 0x30}},  // O7
    {{0x1C, 0x3E, 0x3E, 0x1E, 0x3E, 0x76, 0x76, 0x3E, 0x1E}},  // O8
    {{0x3C, 0x7E, 0x66, 0x66, 0x3E, 0x1E, 0x26, 0x3E, 0x3C}},  // O9
};

// ============================================================================
// Helper: create a Bitmap with digits painted as white text on dark background
// ============================================================================

// Paint a single template digit at position (x0, y0) in the bitmap
static void PaintDigit(Bitmap& bmp, int x0, int y0, const DigitTemplate& tmpl, uint8_t r = 201, uint8_t g = 201,
                       uint8_t b = 201) {
    for (int row = 0; row < 9; row++) {
        for (int col = 0; col < 7; col++) {
            if ((tmpl.rows[row] >> (6 - col)) & 1) {
                bmp.setPixel(x0 + col, y0 + row, r, g, b);
            }
        }
    }
}

// Paint a multi-digit number starting at (x0, y0) with spacing
// Returns the bitmap width used
static void PaintNumber(Bitmap& bmp, int x0, int y0, const std::string& digits, const DigitTemplate templates[10],
                        uint8_t r = 201, uint8_t g = 201, uint8_t b = 201) {
    int x = x0;
    for (char c : digits) {
        int d = c - '0';
        if (d >= 0 && d <= 9) {
            PaintDigit(bmp, x, y0, templates[d], r, g, b);
            x += 7;  // 7px wide + implicit gap from template
        }
    }
}

// Create a bitmap suitable for ReadNumber testing
// The number region is at (regionX, regionY) with size (regionW, regionH)
static Bitmap CreateNumberBitmap(int w, int h) {
    Bitmap bmp;
    bmp.create(w, h);
    // Dark background (like game UI)
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) bmp.setPixel(x, y, 20, 30, 35);
    return bmp;
}

// ============================================================================
// Helper: toggle specific pixels to simulate subpixel rendering variations
// ============================================================================

static void TogglePixel(Bitmap& bmp, int x, int y, bool on, uint8_t r = 201, uint8_t g = 201, uint8_t b = 201) {
    if (on)
        bmp.setPixel(x, y, r, g, b);
    else
        bmp.setPixel(x, y, 20, 30, 35);
}

// ============================================================================
// Test framework
// ============================================================================

static int g_tests_passed = 0;
static int g_tests_failed = 0;

static void TestReadNumber(const std::string& testName, Bitmap& bmp, int x, int y, int w, int h, int expected,
                           bool expectedValid = true) {
    FontAtlas dummyAtlas;
    TextReader::ReadConfig config;
    auto result = TextReader::ReadNumber(bmp, x, y, w, h, dummyAtlas, config);

    bool pass = (result.valid == expectedValid) && (!expectedValid || result.value == expected);

    if (pass) {
        g_tests_passed++;
        std::cout << "  PASS: " << testName << " -> " << result.value << std::endl;
    } else {
        g_tests_failed++;
        std::cout << "  FAIL: " << testName << " expected=" << expected << " got=" << result.value << " valid=" << result.valid
                  << " diag: " << result.diag << std::endl;
    }
}

// ============================================================================
// Test: Clean template digits (perfect pixel match)
// ============================================================================

static void TestCleanTemplates() {
    std::cout << "\n=== Clean Template Digits (white) ===" << std::endl;

    // Single digits
    for (int d = 0; d <= 9; d++) {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[d]);
        TestReadNumber("White digit " + std::to_string(d), bmp, 0, 0, 30, 13, d);
    }

    std::cout << "\n=== Clean Template Digits (orange) ===" << std::endl;

    for (int d = 0; d <= 9; d++) {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, ORANGE_TEMPLATES[d], 220, 100, 40);
        TestReadNumber("Orange digit " + std::to_string(d), bmp, 0, 0, 30, 13, d);
    }
}

// ============================================================================
// Test: Multi-digit numbers with known correct values
// ============================================================================

static void TestMultiDigitNumbers() {
    std::cout << "\n=== Multi-digit Numbers (white) ===" << std::endl;

    struct TestCase {
        std::string digits;
        int expected;
    };

    std::vector<TestCase> cases = {
        {"1000", 1000}, {"1212", 1212}, {"1352", 1352}, {"1396", 1396}, {"1552", 1552}, {"1609", 1609}, {"1762", 1762},
        {"1860", 1860}, {"1894", 1894}, {"1985", 1985}, {"2011", 2011}, {"2035", 2035}, {"2057", 2057}, {"2079", 2079},
        {"2159", 2159}, {"2303", 2303}, {"2329", 2329}, {"2379", 2379}, {"2382", 2382}, {"2412", 2412}, {"2429", 2429},
        {"2433", 2433}, {"2486", 2486}, {"2496", 2496}, {"2510", 2510}, {"2515", 2515}, {"2643", 2643}, {"2671", 2671},
        {"2715", 2715}, {"2782", 2782}, {"2824", 2824}, {"2841", 2841}, {"455", 455},
    };

    for (auto& tc : cases) {
        int numDigits = (int)tc.digits.size();
        int bmpW = numDigits * 7 + 6;  // digits + padding
        Bitmap bmp = CreateNumberBitmap(bmpW, 13);
        PaintNumber(bmp, 3, 2, tc.digits, DIGIT_TEMPLATES);
        TestReadNumber("White " + tc.digits, bmp, 0, 0, bmpW, 13, tc.expected);
    }

    std::cout << "\n=== Multi-digit Numbers (orange) ===" << std::endl;

    std::vector<TestCase> orangeCases = {
        {"2433", 2433},
        {"1988", 1988},
        {"455", 455},
    };

    for (auto& tc : orangeCases) {
        int numDigits = (int)tc.digits.size();
        int bmpW = numDigits * 7 + 6;
        Bitmap bmp = CreateNumberBitmap(bmpW, 13);
        PaintNumber(bmp, 3, 2, tc.digits, ORANGE_TEMPLATES, 220, 100, 40);
        TestReadNumber("Orange " + tc.digits, bmp, 0, 0, bmpW, 13, tc.expected);
    }
}

// ============================================================================
// Test: Subpixel variations - perturbed digits should still classify correctly
// ============================================================================

static void TestSubpixelVariations() {
    std::cout << "\n=== Subpixel Perturbations ===" << std::endl;

    // '9' with one center pixel removed
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[9]);
        TogglePixel(bmp, 3 + 3, 2 + 3, false);
        TestReadNumber("9 with empty center pixel", bmp, 0, 0, 30, 13, 9);
    }

    // '0' with bottom-left pixels removed
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[0]);
        TogglePixel(bmp, 3 + 0, 2 + 7, false);
        TogglePixel(bmp, 3 + 0, 2 + 8, false);
        TestReadNumber("0 with weakened bottom-left", bmp, 0, 0, 30, 13, 0);
    }

    // '8' with top-right pixels removed
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[8]);
        TogglePixel(bmp, 3 + 4, 2 + 2, false);
        TogglePixel(bmp, 3 + 5, 2 + 2, false);
        TestReadNumber("8 with empty topRight", bmp, 0, 0, 30, 13, 8);
    }

    // '8' with bottom-left col 1 removed
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[8]);
        TogglePixel(bmp, 3 + 1, 2 + 5, false);
        TogglePixel(bmp, 3 + 1, 2 + 6, false);
        TogglePixel(bmp, 3 + 1, 2 + 7, false);
        TestReadNumber("8 with col1 bottom removed", bmp, 0, 0, 30, 13, 8);
    }

    // '9' with stray bottom-left pixel
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[9]);
        TogglePixel(bmp, 3 + 0, 2 + 6, true);
        TestReadNumber("9 with stray bottom-left pixel", bmp, 0, 0, 30, 13, 9);
    }

    // '9' with several extra bottom-left pixels
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[9]);
        TogglePixel(bmp, 3 + 0, 2 + 5, true);
        TogglePixel(bmp, 3 + 0, 2 + 6, true);
        TogglePixel(bmp, 3 + 0, 2 + 7, true);
        TogglePixel(bmp, 3 + 2, 2 + 5, true);
        TestReadNumber("9 with extra bottom-left pixels", bmp, 0, 0, 30, 13, 9);
    }

    std::cout << "\n=== 5/6 Confusion ===" << std::endl;

    // Clean 5
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[5]);
        TestReadNumber("Clean 5", bmp, 0, 0, 30, 13, 5);
    }

    // Clean 6
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[6]);
        TestReadNumber("Clean 6", bmp, 0, 0, 30, 13, 6);
    }
}

// ============================================================================
// Test: Position independence - same digit at different x offsets
// This is the core benefit of right-alignment normalization.
// ============================================================================

static void TestPositionIndependence() {
    std::cout << "\n=== Position Independence ===" << std::endl;

    // Test each digit at various x positions within the region
    for (int d = 0; d <= 9; d++) {
        for (int xoff = 1; xoff <= 5; xoff += 2) {
            Bitmap bmp = CreateNumberBitmap(20, 13);
            PaintDigit(bmp, xoff, 2, DIGIT_TEMPLATES[d]);
            std::string name = "White " + std::to_string(d) + " at x=" + std::to_string(xoff);
            TestReadNumber(name, bmp, 0, 0, 20, 13, d);
        }
    }

    // Orange digits at different positions
    for (int d = 0; d <= 9; d++) {
        Bitmap bmp = CreateNumberBitmap(20, 13);
        PaintDigit(bmp, 4, 2, ORANGE_TEMPLATES[d], 220, 100, 40);
        std::string name = "Orange " + std::to_string(d) + " at x=4";
        TestReadNumber(name, bmp, 0, 0, 20, 13, d);
    }
}

// ============================================================================
// Test: Orange text binarization
// Anti-aliased orange pixels need relaxed threshold
// ============================================================================

static void TestOrangeBinarization() {
    std::cout << "\n=== Orange Text Binarization ===" << std::endl;

    // --- Test: Orange '2' with anti-aliased edge pixels ---
    // The original bug: strict isOrange (r>170) missed transition pixels,
    // causing '2' to look like '7'
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, ORANGE_TEMPLATES[2], 220, 100, 40);

        // Add anti-aliased edge pixels at reduced intensity
        // These are the transition pixels that isOrange() missed
        bmp.setPixel(3 + 0, 2 + 6, 140, 70, 30);  // dim orange at bottom-left of '2'
        bmp.setPixel(3 + 1, 2 + 6, 150, 80, 35);
        bmp.setPixel(3 + 0, 2 + 7, 135, 65, 25);
        bmp.setPixel(3 + 1, 2 + 7, 145, 75, 30);

        TestReadNumber("Orange 2 with anti-aliased edges", bmp, 0, 0, 30, 13, 2);
    }

    // --- Test: Full orange number 2433 ---
    {
        Bitmap bmp = CreateNumberBitmap(34, 13);
        PaintNumber(bmp, 3, 2, "2433", ORANGE_TEMPLATES, 220, 100, 40);
        TestReadNumber("Orange 2433", bmp, 0, 0, 34, 13, 2433);
    }
}

// ============================================================================
// Test: Merged blob splitting
// Digits that touch form wide blobs that must be split
// ============================================================================

static void TestBlobSplitting() {
    std::cout << "\n=== Merged Blob Splitting ===" << std::endl;

    // Templates always have col 6 empty, so 7px spacing creates a 1px gap.
    // To simulate merged blobs (as seen in real game), we bridge the gap
    // by adding pixels at the boundary.

    // --- Test: Two digits merged (bridge at boundary) ---
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[5]);   // '5' at x=3
        PaintDigit(bmp, 10, 2, DIGIT_TEMPLATES[2]);  // '2' at x=10
        // Bridge the gap at x=9 (col 6 of '5', normally empty)
        // Add pixels in rows where both neighbors have content
        TogglePixel(bmp, 9, 2 + 0, true);  // bridge top
        TogglePixel(bmp, 9, 2 + 1, true);
        TogglePixel(bmp, 9, 2 + 7, true);
        TogglePixel(bmp, 9, 2 + 8, true);  // bridge bottom
        TestReadNumber("Merged 52 (bridged gap)", bmp, 0, 0, 30, 13, 52);
    }

    // --- Test: 64 merged (this was a real failure case: 2643 digit pair) ---
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[6]);
        PaintDigit(bmp, 10, 2, DIGIT_TEMPLATES[4]);
        // Bridge at x=9
        TogglePixel(bmp, 9, 2 + 3, true);
        TogglePixel(bmp, 9, 2 + 4, true);
        TestReadNumber("Merged 64 (bridged gap)", bmp, 0, 0, 30, 13, 64);
    }
}

// ============================================================================
// Test: Known full-page results (verified against game screenshots)
// These are complete rows that we verified visually during development.
// ============================================================================

static void TestVerifiedPages() {
    std::cout << "\n=== Verified Page: Low Values (page 1) ===" << std::endl;

    // Page 1: first calibration page (values from game screenshot)
    struct VerifiedRow {
        std::string digits;
        int expected;
        std::string skill;  // for labeling only
    };

    std::vector<VerifiedRow> page1 = {
        {"1000", 1000, "Aim"},
        {"1000", 1000, "Alertness"},
        {"1212", 1212, "Analysis"},
        {"1352", 1352, "Anatomy"},
        {"1396", 1396, "Animal Lore"},
        {"1552", 1552, "Archaeological Lore"},
        {"1609", 1609, "Armor Technology"},
        {"1762", 1762, "Artefact Preservation"},
        {"1860", 1860, "Athletics"},
        {"1894", 1894, "Attachments Technology"},
        {"1985", 1985, "Avoidance"},
    };

    for (auto& row : page1) {
        int n = (int)row.digits.size();
        int bmpW = n * 7 + 6;
        Bitmap bmp = CreateNumberBitmap(bmpW, 13);
        PaintNumber(bmp, 3, 2, row.digits, DIGIT_TEMPLATES);
        TestReadNumber(row.skill + " (" + row.digits + ")", bmp, 0, 0, bmpW, 13, row.expected);
    }

    std::cout << "\n=== Verified Page: High Values (page 3) ===" << std::endl;

    std::vector<VerifiedRow> page3 = {
        {"2433", 2433, "Agility (orange)"},
        {"2438", 2438, "Aim"},
        {"2486", 2486, "Alertness"},
        {"2496", 2496, "Analysis"},
        {"2510", 2510, "Anatomy"},
        {"2515", 2515, "Animal Lore"},
        {"2643", 2643, "Animal Taming"},
        {"2671", 2671, "Armor Technology"},
        {"2715", 2715, "Artefact Preservation"},
        {"2782", 2782, "Athletics"},
        {"2824", 2824, "Attachments Technology"},
        {"2841", 2841, "Avoidance"},
    };

    for (auto& row : page3) {
        int n = (int)row.digits.size();
        int bmpW = n * 7 + 6;
        Bitmap bmp = CreateNumberBitmap(bmpW, 13);
        // Use orange for first row, white for rest
        bool isOrange = row.skill.find("orange") != std::string::npos;
        if (isOrange)
            PaintNumber(bmp, 3, 2, row.digits, ORANGE_TEMPLATES, 220, 100, 40);
        else
            PaintNumber(bmp, 3, 2, row.digits, DIGIT_TEMPLATES);
        TestReadNumber(row.skill + " (" + row.digits + ")", bmp, 0, 0, bmpW, 13, row.expected);
    }

    std::cout << "\n=== Verified Page: Page 4 (higher values) ===" << std::endl;

    std::vector<VerifiedRow> page4 = {
        {"2894", 2894, "Agility (orange)"}, {"2976", 2976, "Aim"},
        {"3020", 3020, "Alertness"},        {"3048", 3048, "Analysis"},
        {"3112", 3112, "Anatomy"},          {"3293", 3293, "Animal Lore"},
        {"3399", 3399, "Armor Technology"}, {"3435", 3435, "Artefact Preservation"},
        {"3489", 3489, "Athletics"},        {"3512", 3512, "Attachments Technology"},
        {"3723", 3723, "Avoidance"},        {"3916", 3916, "BLP Weaponry Technology"},
    };

    for (auto& row : page4) {
        int n = (int)row.digits.size();
        int bmpW = n * 7 + 6;
        Bitmap bmp = CreateNumberBitmap(bmpW, 13);
        bool isOrange = row.skill.find("orange") != std::string::npos;
        if (isOrange)
            PaintNumber(bmp, 3, 2, row.digits, ORANGE_TEMPLATES, 220, 100, 40);
        else
            PaintNumber(bmp, 3, 2, row.digits, DIGIT_TEMPLATES);
        TestReadNumber(row.skill + " (" + row.digits + ")", bmp, 0, 0, bmpW, 13, row.expected);
    }
}

// ============================================================================
// Test: Edge cases
// ============================================================================

static void TestEdgeCases() {
    std::cout << "\n=== Edge Cases ===" << std::endl;

    // Empty region (no text)
    {
        Bitmap bmp = CreateNumberBitmap(30, 13);
        TestReadNumber("Empty region", bmp, 0, 0, 30, 13, 0, false);
    }

    // Single digit
    {
        Bitmap bmp = CreateNumberBitmap(15, 13);
        PaintDigit(bmp, 3, 2, DIGIT_TEMPLATES[7]);
        TestReadNumber("Single digit 7", bmp, 0, 0, 15, 13, 7);
    }

    // Large number (6 digits)
    {
        Bitmap bmp = CreateNumberBitmap(50, 13);
        PaintNumber(bmp, 3, 2, "123456", DIGIT_TEMPLATES);
        TestReadNumber("Six digit 123456", bmp, 0, 0, 50, 13, 123456);
    }

    // Number with all same digits
    {
        Bitmap bmp = CreateNumberBitmap(40, 13);
        PaintNumber(bmp, 3, 2, "8888", DIGIT_TEMPLATES);
        TestReadNumber("All 8s (8888)", bmp, 0, 0, 40, 13, 8888);
    }

    // All 9s
    {
        Bitmap bmp = CreateNumberBitmap(40, 13);
        PaintNumber(bmp, 3, 2, "9999", DIGIT_TEMPLATES);
        TestReadNumber("All 9s (9999)", bmp, 0, 0, 40, 13, 9999);
    }

    // All 0s (except leading 1)
    {
        Bitmap bmp = CreateNumberBitmap(40, 13);
        PaintNumber(bmp, 3, 2, "1000", DIGIT_TEMPLATES);
        TestReadNumber("1000 (three zeros)", bmp, 0, 0, 40, 13, 1000);
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "EU Skill Reader - Number Recognition Regression Tests" << std::endl;
    std::cout << "=====================================================" << std::endl;

    TestCleanTemplates();
    TestMultiDigitNumbers();
    TestSubpixelVariations();
    TestPositionIndependence();
    TestOrangeBinarization();
    TestBlobSplitting();
    TestVerifiedPages();
    TestEdgeCases();

    std::cout << "\n=====================================================" << std::endl;
    std::cout << "Results: " << g_tests_passed << " passed, " << g_tests_failed << " failed" << std::endl;

    if (g_tests_failed > 0) {
        std::cout << "*** REGRESSION DETECTED ***" << std::endl;
        return 1;
    }

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
