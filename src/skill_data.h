#pragma once
#include <string>
#include <vector>

// ============================================================================
// Entropia Universe skill list
//
// Skills are compiled into the binary as a Win32 resource from
// resources/eu_skill_list.csv, but can be overridden by placing an
// eu_skill_list.csv file next to the executable (same CSV format).
//
// Skills appear in the game UI in alphabetical order. Hidden skills (hidden=true)
// only appear once unlocked by the player. Non-hidden skills always appear.
//
// When reading sequentially, after matching skill at index N, the next row's
// candidates are: all consecutive hidden skills starting at N+1, plus the
// next non-hidden skill. This gives at most ~5 candidates per row.
// ============================================================================

namespace SkillData {

struct SkillInfo {
    std::string name;
    bool hidden;  // true = only visible once unlocked
};

// Load skill list: tries eu_skill_list.csv in exeDir, falls back to embedded data.
// exeDir should end with a path separator (e.g. "C:\path\to\").
// Returns empty string on success, error message on failure.
std::string Load(const std::string& exeDir);

// Load from a specific file path. Returns empty string on success, error on failure.
std::string LoadFromFile(const std::string& path);

// Returns "embedded" or the file path that was loaded
const std::string& GetSource();

// Returns true if a skill list has been loaded
bool IsLoaded();

// Get the loaded skill list (call Load first)
const std::vector<SkillInfo>& GetSkillList();

// Get candidates for the next row given the current list cursor position.
// Returns indices into the skill list.
std::vector<int> GetNextCandidates(int listCursor);

}  // namespace SkillData
