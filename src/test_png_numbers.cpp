// ============================================================================
// PNG-based regression tests for number recognition
//
// Loads real screenshots of the POINTS column from the game UI,
// runs ReadNumber on each row, and compares against ground-truth CSV.
//
// Build: cl /EHsc /O2 /std:c++17 /DNOMINMAX test_png_numbers.cpp
//        text_reader.cpp font_atlas.cpp /Fe:test_png_numbers.exe
//        /link user32.lib gdi32.lib
//
// Run:   test_png_numbers.exe [base_path]
//        base_path defaults to ".." (assumes exe is in build/)
// ============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "text_reader.h"
#include "types.h"
#include "vendor/stb/stb_image.h"

namespace fs = std::filesystem;

// ============================================================================
// PNG loading
// ============================================================================

static bool LoadPNG(const std::string& path, Bitmap& bmp) {
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);  // force RGBA
    if (!data) {
        std::cerr << "  Failed to load: " << path << " (" << stbi_failure_reason() << ")" << std::endl;
        return false;
    }
    bmp.create(w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            bmp.setPixel(x, y, data[si + 0], data[si + 1], data[si + 2]);  // RGB -> BGRA internally
        }
    }
    stbi_image_free(data);
    return true;
}

// ============================================================================
// CSV loading
// ============================================================================

struct ExpectedRow {
    std::string filename;
    std::vector<int> values;
};

static std::vector<ExpectedRow> LoadCSV(const std::string& path) {
    std::vector<ExpectedRow> rows;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Failed to open CSV: " << path << std::endl;
        return rows;
    }
    std::string line;
    // Skip header
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        ExpectedRow row;
        std::string token;
        // First column: filename
        std::getline(ss, row.filename, ',');
        // Remaining columns: integer values
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                row.values.push_back(std::stoi(token));
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// ============================================================================
// Row detection: find content bands in the POINTS column image
// ============================================================================

static std::vector<int> FindRowTops(const Bitmap& bmp) {
    // Count only text pixels (white/orange) per scanline — NOT teal progress bars.
    // Teal bars fill the space between rows and would merge everything.
    std::vector<int> textCount(bmp.height, 0);
    for (int y = 0; y < bmp.height; y++) {
        for (int x = 0; x < bmp.width; x++) {
            Pixel p = bmp.pixel(x, y);
            if (p.isWhiteText() || p.isOrange()) {
                textCount[y]++;
            }
        }
    }

    // Find bands of consecutive scanlines with text content.
    // Threshold of 1: even a single text pixel counts (digit "1" is narrow).
    const int THRESHOLD = 1;
    std::vector<std::pair<int, int>> bands;  // (top, bottom) inclusive
    int bandStart = -1;
    for (int y = 0; y < bmp.height; y++) {
        if (textCount[y] >= THRESHOLD) {
            if (bandStart < 0) bandStart = y;
        } else {
            if (bandStart >= 0) {
                bands.push_back({bandStart, y - 1});
                bandStart = -1;
            }
        }
    }
    if (bandStart >= 0) {
        bands.push_back({bandStart, bmp.height - 1});
    }

    // Merge bands separated by tiny gaps (1-2px). Anti-aliased text can have
    // scanlines with 0 text pixels mid-glyph.
    const int MERGE_GAP = 2;
    std::vector<std::pair<int, int>> merged;
    for (auto& b : bands) {
        if (!merged.empty() && b.first - merged.back().second <= MERGE_GAP) {
            merged.back().second = b.second;
        } else {
            merged.push_back(b);
        }
    }

    // Filter out tiny bands (noise/artifacts). Real text bands are 7-9px tall.
    const int MIN_BAND_HEIGHT = 4;
    std::vector<std::pair<int, int>> filtered;
    for (auto& b : merged) {
        if (b.second - b.first + 1 >= MIN_BAND_HEIGHT) {
            filtered.push_back(b);
        }
    }

    // First band is the "POINTS" header — skip it.
    // Each subsequent band is a data row's text.
    std::vector<int> rowTops;
    for (int i = 1; i < (int)filtered.size(); i++) {
        rowTops.push_back(filtered[i].first);
    }

    return rowTops;
}

// ============================================================================
// Test runner
// ============================================================================

static int g_total_pass = 0;
static int g_total_fail = 0;
static int g_total_images = 0;
static int g_images_perfect = 0;

static void TestImage(const std::string& basePath, const ExpectedRow& expected) {
    std::string imgPath = (fs::path(basePath) / "testdata" / expected.filename).string();

    Bitmap bmp;
    if (!LoadPNG(imgPath, bmp)) {
        std::cout << "SKIP: " << expected.filename << " (load failed)" << std::endl;
        g_total_fail += (int)expected.values.size();
        return;
    }

    g_total_images++;
    std::string shortName = fs::path(expected.filename).filename().string();
    std::cout << "\n--- " << shortName << " (" << bmp.width << "x" << bmp.height << ", " << expected.values.size()
              << " rows expected) ---" << std::endl;

    // Find data rows
    std::vector<int> rowTops = FindRowTops(bmp);

    if (rowTops.size() != expected.values.size()) {
        std::cout << "  ROW COUNT MISMATCH: found " << rowTops.size() << " rows, expected " << expected.values.size()
                  << std::endl;
    }

    int rowCount = (int)(std::min)(rowTops.size(), expected.values.size());
    int imagePass = 0;
    int imageFail = 0;

    FontAtlas dummyAtlas;
    TextReader::ReadConfig config;
    const int ROW_TEXT_H = 13;  // top half of 25px row (avoids teal progress bars)

    for (int r = 0; r < rowCount; r++) {
        int rowY = rowTops[r];
        auto result = TextReader::ReadNumber(bmp, 0, rowY, bmp.width, ROW_TEXT_H, dummyAtlas, config);

        bool pass = result.valid && result.value == expected.values[r];
        if (pass) {
            imagePass++;
            g_total_pass++;
        } else {
            imageFail++;
            g_total_fail++;
            std::cout << "  FAIL row " << r << ": expected=" << expected.values[r]
                      << " got=" << (result.valid ? std::to_string(result.value) : "INVALID") << " diag: " << result.diag
                      << std::endl;
        }
    }

    // Count extra/missing rows as failures
    if (rowTops.size() > expected.values.size()) {
        int extra = (int)rowTops.size() - (int)expected.values.size();
        std::cout << "  " << extra << " extra row(s) detected" << std::endl;
    }
    if (expected.values.size() > rowTops.size()) {
        int missing = (int)expected.values.size() - (int)rowTops.size();
        g_total_fail += missing;
        std::cout << "  " << missing << " row(s) not detected" << std::endl;
    }

    if (imageFail == 0 && rowTops.size() == expected.values.size()) {
        g_images_perfect++;
        std::cout << "  ALL " << rowCount << " rows PASS" << std::endl;
    } else {
        std::cout << "  " << imagePass << "/" << rowCount << " rows pass" << std::endl;
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "EU Skill Reader - PNG Number Recognition Tests" << std::endl;
    std::cout << "===============================================" << std::endl;

    // Determine base path (project root)
    std::string basePath;
    if (argc > 1) {
        basePath = argv[1];
    } else {
        // Default: assume exe is in build/Release/ or build/
        fs::path exePath = fs::path(argv[0]).parent_path();
        if (fs::exists(exePath / ".." / ".." / "testdata")) {
            basePath = (exePath / ".." / "..").string();
        } else if (fs::exists(exePath / ".." / "testdata")) {
            basePath = (exePath / "..").string();
        } else if (fs::exists(fs::path("testdata"))) {
            basePath = ".";
        } else {
            std::cerr << "Cannot find testdata/ directory." << std::endl;
            std::cerr << "Usage: " << argv[0] << " [project_root_path]" << std::endl;
            return 1;
        }
    }

    std::string csvPath = (fs::path(basePath) / "testdata" / "expected_points.csv").string();
    std::cout << "Base path: " << fs::canonical(basePath).string() << std::endl;
    std::cout << "CSV: " << csvPath << std::endl;

    auto expected = LoadCSV(csvPath);
    if (expected.empty()) {
        std::cerr << "No test cases loaded from CSV." << std::endl;
        return 1;
    }

    std::cout << "Loaded " << expected.size() << " test images from CSV." << std::endl;

    for (auto& row : expected) {
        TestImage(basePath, row);
    }

    std::cout << "\n===============================================" << std::endl;
    std::cout << "Images: " << g_images_perfect << "/" << g_total_images << " perfect" << std::endl;
    std::cout << "Rows:   " << g_total_pass << " passed, " << g_total_fail << " failed, " << (g_total_pass + g_total_fail)
              << " total" << std::endl;

    if (g_total_fail > 0) {
        std::cout << "*** " << g_total_fail << " FAILURES ***" << std::endl;
    } else {
        std::cout << "All tests passed!" << std::endl;
    }

    // Return 0 always — these are baseline measurements, not gating tests
    return 0;
}
