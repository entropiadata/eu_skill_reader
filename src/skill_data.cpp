#include "skill_data.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace SkillData {

static std::vector<SkillInfo> s_skills;
static bool s_loaded = false;
static std::string s_source;  // "embedded" or file path

// Trim whitespace from both ends
static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Shared parser — works on any input stream
static std::string ParseStream(std::istream& input, std::vector<SkillInfo>& out) {
    std::string line;
    int lineNum = 0;

    while (std::getline(input, line)) {
        lineNum++;

        // Strip BOM from first line if present
        if (lineNum == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }

        line = Trim(line);

        // Skip blank lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Find last comma (skill names don't contain commas)
        size_t commaPos = line.rfind(',');
        if (commaPos == std::string::npos) {
            return "Line " + std::to_string(lineNum) + ": missing comma - expected 'SkillName,true/false'";
        }

        std::string name = Trim(line.substr(0, commaPos));
        std::string hiddenStr = Trim(line.substr(commaPos + 1));

        if (name.empty()) {
            return "Line " + std::to_string(lineNum) + ": empty skill name";
        }

        bool hidden;
        if (hiddenStr == "true") {
            hidden = true;
        } else if (hiddenStr == "false") {
            hidden = false;
        } else {
            return "Line " + std::to_string(lineNum) + ": hidden flag must be 'true' or 'false', got '" + hiddenStr + "'";
        }

        // Check for duplicates
        for (const auto& s : out) {
            if (s.name == name) {
                return "Line " + std::to_string(lineNum) + ": duplicate skill '" + name + "'";
            }
        }

        out.push_back({name, hidden});
    }

    if (out.empty()) {
        return "No skills found";
    }

    return "";  // success
}

std::string LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "Cannot open skill list file: " + path;
    }

    std::vector<SkillInfo> skills;
    std::string err = ParseStream(file, skills);
    if (!err.empty()) return err;

    s_skills = std::move(skills);
    s_loaded = true;
    s_source = path;
    return "";  // success
}

// Load skill list from embedded Win32 resource (RCDATA compiled from eu_skill_list.csv)
static std::string LoadEmbedded() {
    HRSRC hRes = FindResourceA(nullptr, "IDR_SKILL_LIST", RT_RCDATA);
    if (!hRes) return "Embedded skill resource not found";

    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return "Failed to load embedded skill resource";

    const char* data = (const char*)LockResource(hData);
    DWORD size = SizeofResource(nullptr, hRes);
    if (!data || size == 0) return "Embedded skill resource is empty";

    std::string csv(data, size);
    std::istringstream stream(csv);
    std::vector<SkillInfo> skills;
    std::string err = ParseStream(stream, skills);
    if (!err.empty()) return "Embedded skill data error: " + err;

    s_skills = std::move(skills);
    s_loaded = true;
    s_source = "embedded";
    return "";
}

std::string Load(const std::string& exeDir) {
    // Try external file first
    std::string csvPath = exeDir + "eu_skill_list.csv";
    {
        std::ifstream test(csvPath);
        if (test.is_open()) {
            test.close();
            std::string err = LoadFromFile(csvPath);
            if (!err.empty()) return err;
            return "";  // loaded from external file
        }
    }

    // No external file — use embedded data
    return LoadEmbedded();
}

const std::string& GetSource() { return s_source; }

bool IsLoaded() { return s_loaded; }

const std::vector<SkillInfo>& GetSkillList() { return s_skills; }

std::vector<int> GetNextCandidates(int listCursor) {
    std::vector<int> candidates;

    for (int i = listCursor; i < (int)s_skills.size(); i++) {
        candidates.push_back(i);
        if (!s_skills[i].hidden) {
            // This non-hidden skill MUST appear, so stop here
            break;
        }
    }
    return candidates;
}

}  // namespace SkillData
