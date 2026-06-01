#include "PlaylistManager.h"
#include <string.h>
#include <esp_heap_caps.h>

const char* PlaylistManager::FAVORITES_PATH  = "/Playlists/favorites.m3u";
const char* PlaylistManager::RECORDINGS_PATH = "/Recordings";

PlaylistManager::PlaylistManager() {}

bool PlaylistManager::ensureDir(const char* dir) {
    if (!SD.exists(dir)) return SD.mkdir(dir);
    return true;
}

int PlaylistManager::load(const char* path, char entries[][256], int max_entries) {
    File f = SD.open(path);
    if (!f) return 0;

    int count = 0;
    while (f.available() && count < max_entries) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;

        // Resolve relative paths
        if (line[0] != '/') {
            // Build path relative to playlist dir
            char dir[256] = {0};
            strncpy(dir, path, sizeof(dir) - 1);
            char* slash = strrchr(dir, '/');
            if (slash) {
                slash[1] = '\0';
                char resolved[256];
                snprintf(resolved, sizeof(resolved), "%s%s", dir, line.c_str());
                strncpy(entries[count], resolved, 255);
            } else {
                strncpy(entries[count], line.c_str(), 255);
            }
        } else {
            strncpy(entries[count], line.c_str(), 255);
        }
        entries[count][255] = '\0';
        count++;
    }
    f.close();
    return count;
}

bool PlaylistManager::save(const char* path, const char* const* track_paths, int count) {
    ensureDir("/Playlists");
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;

    f.println("#EXTM3U");
    for (int i = 0; i < count; i++) {
        f.println(track_paths[i]);
    }
    f.close();
    return true;
}

bool PlaylistManager::addFavorite(const char* track_path) {
    if (isFavorite(track_path)) return true;
    ensureDir("/Playlists");
    File f = SD.open(FAVORITES_PATH, FILE_APPEND);
    if (!f) {
        // Create with header
        f = SD.open(FAVORITES_PATH, FILE_WRITE);
        if (!f) return false;
        f.println("#EXTM3U");
    }
    f.println(track_path);
    f.close();
    return true;
}

bool PlaylistManager::removeFavorite(const char* track_path) {
    // Load all entries, remove the matching one, rewrite. The scratch buffer
    // (up to ~512 KB) is allocated in PSRAM to stay out of internal DRAM.
    typedef char Entry[256];
    Entry* entries = (Entry*)heap_caps_malloc(sizeof(Entry) * PLAYLIST_MAX_ENTRIES, MALLOC_CAP_SPIRAM);
    if (!entries) entries = (Entry*)malloc(sizeof(Entry) * PLAYLIST_MAX_ENTRIES);
    if (!entries) return false;

    int n = loadFavorites(entries, PLAYLIST_MAX_ENTRIES);
    SD.remove(FAVORITES_PATH);
    File f = SD.open(FAVORITES_PATH, FILE_WRITE);
    if (!f) { free(entries); return false; }
    f.println("#EXTM3U");
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i], track_path) != 0) f.println(entries[i]);
    }
    f.close();
    free(entries);
    return true;
}

bool PlaylistManager::isFavorite(const char* track_path) {
    File f = SD.open(FAVORITES_PATH);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (strcmp(line.c_str(), track_path) == 0) { f.close(); return true; }
    }
    f.close();
    return false;
}

int PlaylistManager::loadFavorites(char entries[][256], int max_entries) {
    return load(FAVORITES_PATH, entries, max_entries);
}
