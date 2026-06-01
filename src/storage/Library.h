#pragma once
#include "../types.h"
#include "../config.h"
#include <SD.h>

enum class BrowseMode : uint8_t {
    ALL_SONGS = 0,
    BY_ARTIST,
    BY_ALBUM,
    BY_FOLDER,
    RECORDINGS,
    FAVORITES,
    RECENTLY_ADDED,
    GENRE,
};

class Library {
public:
    Library();

    // Scan SD card and populate track index
    bool scan();

    // Track count
    int  count() const { return m_count; }
    const TrackInfo& track(int idx) const;

    // Browse / filter
    void        setBrowseMode(BrowseMode mode);
    BrowseMode  browseMode() const { return m_mode; }

    // Returns filtered list indices for the current browse mode
    // Caller provides a pre-allocated buffer
    int  getFiltered(int* out_indices, int max_count, const char* filter = nullptr) const;

    // Genre groups
    int  genreCount() const { return m_genre_count; }
    const char* genreName(int idx) const;
    int  getByGenre(int genre_idx, int* out_indices, int max_count) const;

    // Artist / Album helpers
    int  artistCount() const { return m_artist_count; }
    const char* artistName(int idx) const;
    int  getByArtist(int artist_idx, int* out_indices, int max_count) const;

    int  albumCount() const { return m_album_count; }
    const char* albumName(int idx) const;
    int  getByAlbum(int album_idx, int* out_indices, int max_count) const;

    // Shuffle support
    void buildShuffleOrder(int* order, int count) const;

    // Find track index by path
    int  findByPath(const char* path) const;

    // Recently added
    int  getRecentlyAdded(int* out_indices, int max_count) const;

private:
    void scanDir(File& dir, int depth);
    void parseID3v2(const char* path, TrackInfo& info);
    void parseVorbisComment(const char* path, TrackInfo& info);
    void indexArtistsAlbums();

    TrackInfo* m_tracks = nullptr;   // allocated in PSRAM (see constructor)
    int        m_count = 0;
    BrowseMode m_mode = BrowseMode::ALL_SONGS;

    // Unique artist/album/genre strings (pointers into track data)
    const char* m_artists[512];
    int         m_artist_count = 0;
    const char* m_albums[512];
    int         m_album_count = 0;
    const char* m_genres[64];
    int         m_genre_count = 0;
};
