#pragma once
#include "types.h"

// ============================================================================
// Font Atlas: Load TTF fonts, render glyph bitmaps for template matching
// ============================================================================

namespace FontEngine {

// Load a TTF font file and generate glyph atlas at specified point size
// Uses Win32 GDI to render each character identically to how Windows apps render them.
// quality: 0=NONANTIALIASED, 1=ANTIALIASED, 2=CLEARTYPE
// charset: if non-empty, only load glyphs for these characters (plus space)
FontAtlas LoadFont(const std::string& fontPath, int pointSize, bool bold = false, bool italic = false, int quality = 1,
                   const std::string& charset = "");

// Render a string using a font atlas, returning a grayscale bitmap
// The bitmap shows white text on black background
std::vector<uint8_t> RenderString(const FontAtlas& atlas, const std::string& text, int& outWidth, int& outHeight);

// Attempt to identify which font/size best matches a text region from a screenshot
// Returns the best matching FontAtlas from the candidates (currently unused)
// struct FontMatch {
//     int fontIndex;
//     int pointSize;
//     float confidence;   // 0.0 - 1.0
// };
//
// FontMatch IdentifyFont(
//     const Bitmap& screenshot,
//     int textX, int textY, int textW, int textH,
//     const std::string& knownText,
//     const std::vector<std::string>& fontPaths,
//     bool bold = false
// );

// Generate font atlases for all candidate fonts at common sizes (currently unused)
// std::vector<FontAtlas> GenerateCandidateAtlases(
//     const std::vector<std::string>& fontPaths,
//     const std::vector<int>& sizes = {9, 10, 11, 12, 13, 14, 15, 16}
// );

}  // namespace FontEngine
