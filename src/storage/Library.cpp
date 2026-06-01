#include "Library.h"
#include <string.h>
#include <ctype.h>

static const Library::Entry EMPTY_ENTRY = {};

Library::Library() : m_count(0) {
    m_path[0] = '/';
    m_path[1] = '\0';
    memset(m_entries, 0, sizeof(m_entries));
}

void Library::begin(const char* root) {
    strncpy(m_path, root, sizeof(m_path) - 1);
    m_path[sizeof(m_path) - 1] = '\0';
    loadEntries();
}

void Library::refresh() { loadEntries(); }

bool Library::isAudioExt(const char* name) {
    const char* ext = strrchr(name, '.');
    if (!ext) return false;
    ext++;
    return strcasecmp(ext, "mp3")  == 0 || strcasecmp(ext, "wav")  == 0 ||
           strcasecmp(ext, "flac") == 0 || strcasecmp(ext, "aac")  == 0 ||
           strcasecmp(ext, "m4a")  == 0 || strcasecmp(ext, "ogg")  == 0;
}

void Library::loadEntries() {
    m_count = 0;

    File dir = SD.open(m_path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File f = dir.openNextFile();
    while (f && m_count < MAX_ENTRIES) {
        bool is_dir = f.isDirectory();
        if (is_dir || isAudioExt(f.name())) {
            strncpy(m_entries[m_count].name, f.name(), sizeof(Entry::name) - 1);
            m_entries[m_count].name[sizeof(Entry::name) - 1] = '\0';
            m_entries[m_count].is_dir = is_dir;
            m_count++;
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();

    // Stable-partition: directories before audio files
    for (int i = 1; i < m_count; i++) {
        if (m_entries[i].is_dir && !m_entries[i - 1].is_dir) {
            for (int j = i; j > 0 && m_entries[j].is_dir && !m_entries[j - 1].is_dir; j--) {
                Entry tmp     = m_entries[j];
                m_entries[j]  = m_entries[j - 1];
                m_entries[j - 1] = tmp;
            }
        }
    }
}

const Library::Entry& Library::entry(int i) const {
    return (i >= 0 && i < m_count) ? m_entries[i] : EMPTY_ENTRY;
}

bool Library::getFullPath(int idx, char* out, size_t out_size) const {
    if (idx < 0 || idx >= m_count) return false;
    if (m_path[0] == '/' && m_path[1] == '\0')
        snprintf(out, out_size, "/%s", m_entries[idx].name);
    else
        snprintf(out, out_size, "%s/%s", m_path, m_entries[idx].name);
    return true;
}

bool Library::isAudioFile(int idx) const {
    return (idx >= 0 && idx < m_count) && !m_entries[idx].is_dir;
}

bool Library::enterDir(int idx) {
    if (idx < 0 || idx >= m_count || !m_entries[idx].is_dir) return false;
    char new_path[128];
    if (!getFullPath(idx, new_path, sizeof(new_path))) return false;
    strncpy(m_path, new_path, sizeof(m_path) - 1);
    m_path[sizeof(m_path) - 1] = '\0';
    loadEntries();
    return true;
}

void Library::goUp() {
    if (m_path[0] == '/' && m_path[1] == '\0') return;
    char* slash = strrchr(m_path, '/');
    if (!slash || slash == m_path) {
        m_path[0] = '/';
        m_path[1] = '\0';
    } else {
        *slash = '\0';
    }
    loadEntries();
}

bool Library::getAdjacentTrack(const char* path, int direction,
                                char* out, size_t out_size) const {
    const char* fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;

    int cur = -1;
    for (int i = 0; i < m_count; i++) {
        if (!m_entries[i].is_dir && strcmp(m_entries[i].name, fname) == 0) {
            cur = i;
            break;
        }
    }
    if (cur < 0) return false;

    int step = direction > 0 ? 1 : -1;
    for (int i = cur + step; i >= 0 && i < m_count; i += step) {
        if (!m_entries[i].is_dir)
            return getFullPath(i, out, out_size);
    }
    return false;
}
