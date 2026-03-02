#pragma once
#include "capture.h"
#include "font_atlas.h"
#include "skill_window.h"
#include "text_reader.h"
#include "types.h"

// ============================================================================
// Application: Manages the workflow of calibration, monitoring, and data export
// ============================================================================

// Result of parsing and validating a user-supplied decryption key
struct KeyParseResult {
    bool valid = false;
    uint8_t key[16] = {};
    std::string error;  // human-readable error if !valid
};

// Callback: prompt user for the pak decryption key.
// Returns true if user provided a key (written into out_key), false on cancel.
using KeyPromptCallback = std::function<bool(uint8_t out_key[16])>;

class App {
public:
    App();
    ~App();

    // --- Configuration ---
    void SetTargetWindow(HWND hwnd);
    void SetKeyPromptCallback(KeyPromptCallback cb) { m_keyPromptCb = cb; }

    // Parse a user-supplied key string (hex, \x-escaped, or base64) and validate
    // its SHA-256 against the expected hash. Public static so dialog procs can
    // call it for inline validation.
    static KeyParseResult ParseAndValidateKey(const char* input);

    // --- Workflow ---
    // Step 1: Calibrate - detect the Skills window and identify the font
    bool Calibrate();

    // Step 2: Start monitoring for page changes
    void StartMonitoring();
    void StopMonitoring();
    bool IsMonitoring() const { return m_monitoring; }

    // Step 3: Manual capture trigger
    bool CaptureCurrentPage();

    // --- Data ---
    const std::vector<SkillEntry>& GetSkills() const { return m_allSkills; }
    void ClearSkills();

    // Export to CSV
    bool ExportCSV(const std::string& path);

    // --- Status ---
    AppState GetState() const { return m_state; }
    const std::string& GetStatusText() const { return m_status; }
    const SkillWindowLayout& GetLayout() const { return m_layout; }
    int GetCurrentPage() const { return m_currentPage; }
    int GetTotalPages() const { return m_totalPages; }
    int GetTotalSkillsRead() const { return (int)m_allSkills.size(); }
    int GetTotalPoints() const {
        int total = 0;
        for (const auto& s : m_allSkills) total += s.points;
        return total;
    }

    // --- Callbacks ---
    void SetLogCallback(LogCallback cb) { m_logCb = cb; }
    void SetSkillUpdateCallback(std::function<void()> cb) { m_skillUpdateCb = cb; }

    // Call from timer/thread to check for page changes
    void PollForChanges();

    // Load a screenshot from file (for testing/development)
    bool LoadScreenshot(const std::string& path);

    // Save current screenshot for debugging
    bool SaveScreenshot(const std::string& path) const;
    bool SaveScreenshotPNG(const std::string& path) const;

    // Get the last captured screenshot
    const Bitmap& GetLastCapture() const { return m_lastCapture; }

private:
    void Log(const std::string& msg);
    bool LoadFonts();
    void AddSkills(const std::vector<SkillEntry>& newSkills, const std::string& category);

    AppState m_state = AppState::Idle;
    std::string m_status;

    HWND m_targetWindow = nullptr;

    // Font atlases
    FontAtlas m_nameFont;
    FontAtlas m_rankFont;
    FontAtlas m_numberFont;
    bool m_fontsLoaded = false;
    bool m_fontsCalibrated = false;

    // Layout
    SkillWindowLayout m_layout;

    // Monitoring
    bool m_monitoring = false;
    std::vector<int> m_lastPagePoints;  // points from last processed page

    // Data
    std::vector<SkillEntry> m_allSkills;
    std::map<std::string, SkillEntry> m_skillMap;  // dedupe by name
    int m_currentPage = 0;
    int m_totalPages = 0;
    std::string m_currentCategory;
    int m_skillListCursor = 0;  // position in SkillData::GetSkillList() for next page

    // Last capture
    Bitmap m_lastCapture;

    // Callbacks
    LogCallback m_logCb;
    std::function<void()> m_skillUpdateCb;
    KeyPromptCallback m_keyPromptCb;
};
