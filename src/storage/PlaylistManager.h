#pragma once
#include <SD.h>
#include "../types.h"
#include "../config.h"

#define PLAYLIST_MAX_ENTRIES 300

class PlaylistManager {
public:
    PlaylistManager();

    // Load an M3U file from SD, returns number of paths loaded
    int  load(const char* path, char entries[][128], int max_entries);

    // Save current queue as M3U to SD
    bool save(const char* path, const char* const* track_paths, int count);

    // Favorites management
    bool addFavorite(const char* track_path);
    bool removeFavorite(const char* track_path);
    bool isFavorite(const char* track_path);
    int  loadFavorites(char entries[][128], int max_entries);

    static const char* FAVORITES_PATH;
    static const char* RECORDINGS_PATH;

private:
    bool ensureDir(const char* dir);
};
