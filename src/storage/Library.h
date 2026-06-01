#pragma once
#include <SD.h>

// File-browser backed "library" — no pre-scan, no heap allocation.
// Reads one directory at a time; user navigates folders to find tracks.
class Library {
public:
    static constexpr int MAX_ENTRIES = 64;

    struct Entry {
        char name[56];   // filename only (no path prefix)
        bool is_dir;
    };

    Library();

    void begin(const char* root = "/");
    void refresh();                          // re-read current directory

    int          count()       const { return m_count; }
    const Entry& entry(int i)  const;
    const char*  currentPath() const { return m_path; }

    bool enterDir(int idx);                  // descend into directory at idx
    void goUp();                             // ascend to parent

    bool getFullPath(int idx, char* out, size_t out_size) const;
    bool isAudioFile(int idx) const;

    // Returns the full path of the next (dir>0) or previous (dir<0) audio
    // file in the current listing relative to `current_path`. Returns false
    // if there is no adjacent audio file.
    bool getAdjacentTrack(const char* current_path, int dir,
                          char* out, size_t out_size) const;

private:
    char  m_path[128];
    Entry m_entries[MAX_ENTRIES];
    int   m_count;

    void loadEntries();
    static bool isAudioExt(const char* name);
};
