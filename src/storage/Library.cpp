#include "Library.h"
#include <string.h>
#include <stdlib.h>

static const TrackInfo EMPTY_TRACK = {};

// Supported audio extensions
static bool isAudioFile(const char* name) {
    const char* ext = strrchr(name, '.');
    if (!ext) return false;
    ext++;
    return (strcasecmp(ext, "mp3")  == 0 ||
            strcasecmp(ext, "flac") == 0 ||
            strcasecmp(ext, "wav")  == 0 ||
            strcasecmp(ext, "aac")  == 0 ||
            strcasecmp(ext, "m4a")  == 0 ||
            strcasecmp(ext, "ogg")  == 0 ||
            strcasecmp(ext, "opus") == 0);
}

Library::Library() {
    memset(m_tracks, 0, sizeof(m_tracks));
    memset(m_artists, 0, sizeof(m_artists));
    memset(m_albums,  0, sizeof(m_albums));
    memset(m_genres,  0, sizeof(m_genres));
}

bool Library::scan() {
    m_count = 0;
    m_artist_count = 0;
    m_album_count  = 0;
    m_genre_count  = 0;

    File root = SD.open("/Music");
    if (root) {
        scanDir(root, 0);
        root.close();
    }
    // Also index root
    root = SD.open("/");
    if (root) {
        File f = root.openNextFile();
        while (f && m_count < LIBRARY_MAX_TRACKS) {
            if (!f.isDirectory() && isAudioFile(f.name())) {
                TrackInfo& t = m_tracks[m_count];
                snprintf(t.path, sizeof(t.path), "/%s", f.name());
                strncpy(t.title, f.name(), sizeof(t.title) - 1);
                parseID3v2(t.path, t);
                m_count++;
            }
            f.close();
            f = root.openNextFile();
        }
        root.close();
    }

    indexArtistsAlbums();
    return m_count > 0;
}

void Library::scanDir(File& dir, int depth) {
    if (depth >= SD_SCAN_MAX_DEPTH) return;

    File f = dir.openNextFile();
    while (f && m_count < LIBRARY_MAX_TRACKS) {
        char full[256];
        snprintf(full, sizeof(full), "%s/%s", dir.path(), f.name());

        if (f.isDirectory()) {
            File sub = SD.open(full);
            if (sub) {
                scanDir(sub, depth + 1);
                sub.close();
            }
        } else if (isAudioFile(f.name())) {
            TrackInfo& t = m_tracks[m_count];
            strncpy(t.path, full, sizeof(t.path) - 1);
            strncpy(t.title, f.name(), sizeof(t.title) - 1);

            const char* ext = strrchr(f.name(), '.');
            if (ext && (strcasecmp(ext+1, "flac") == 0 || strcasecmp(ext+1, "ogg") == 0)) {
                parseVorbisComment(t.path, t);
            } else {
                parseID3v2(t.path, t);
            }
            m_count++;
        }
        f.close();
        f = dir.openNextFile();
    }
}

// Minimal ID3v2 parser — reads TIT2, TPE1, TALB, TRCK tags
void Library::parseID3v2(const char* path, TrackInfo& info) {
    File f = SD.open(path);
    if (!f) return;

    uint8_t hdr[10];
    if (f.read(hdr, 10) != 10) { f.close(); return; }
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') { f.close(); return; }

    uint32_t tag_size = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                        ((uint32_t)(hdr[7] & 0x7F) << 14) |
                        ((uint32_t)(hdr[8] & 0x7F) << 7)  |
                         (uint32_t)(hdr[9] & 0x7F);

    uint32_t pos = 10;
    while (pos + 10 < tag_size) {
        uint8_t frame_hdr[10];
        if (f.read(frame_hdr, 10) != 10) break;
        pos += 10;

        uint32_t frame_size = ((uint32_t)frame_hdr[4] << 24) |
                              ((uint32_t)frame_hdr[5] << 16) |
                              ((uint32_t)frame_hdr[6] << 8)  |
                               (uint32_t)frame_hdr[7];

        if (frame_size == 0 || frame_size > 4096) break;

        char frame_id[5] = {0};
        memcpy(frame_id, frame_hdr, 4);

        char* target = nullptr;
        size_t target_sz = 0;

        if      (strcmp(frame_id, "TIT2") == 0) { target = info.title;  target_sz = sizeof(info.title); }
        else if (strcmp(frame_id, "TPE1") == 0) { target = info.artist; target_sz = sizeof(info.artist); }
        else if (strcmp(frame_id, "TALB") == 0) { target = info.album;  target_sz = sizeof(info.album); }
        else if (strcmp(frame_id, "TRCK") == 0) { /* handled below */  }
        else if (strcmp(frame_id, "APIC") == 0) { info.has_art = true; }

        if (target) {
            uint8_t enc = f.read();  // encoding byte
            uint32_t read_sz = (frame_size - 1) < (target_sz - 1) ? (frame_size - 1) : (target_sz - 1);
            f.read((uint8_t*)target, read_sz);
            target[read_sz] = '\0';
            // Strip UTF-16 BOM/null-bytes for encoding 1
            if (enc == 1 && read_sz >= 2 && (uint8_t)target[0] == 0xFF) {
                // Simple UTF-16LE → ASCII strip
                for (uint32_t i = 0; i < read_sz / 2; i++) {
                    target[i] = target[i * 2 + 2];
                }
            }
            // Skip any remaining frame bytes
            int32_t remaining = (int32_t)frame_size - 1 - (int32_t)read_sz;
            if (remaining > 0) f.seek(f.position() + remaining);
        } else if (strcmp(frame_id, "TRCK") == 0) {
            uint8_t enc = f.read();
            (void)enc;
            char tmp[8] = {0};
            uint32_t rs = frame_size - 1 < 7 ? frame_size - 1 : 7;
            f.read((uint8_t*)tmp, rs);
            info.track_num = (uint8_t)atoi(tmp);
            int32_t rem = (int32_t)frame_size - 1 - (int32_t)rs;
            if (rem > 0) f.seek(f.position() + rem);
        } else {
            f.seek(f.position() + frame_size);
        }
        pos += frame_size;
    }

    // If title still empty, use filename
    if (info.title[0] == '\0') {
        const char* slash = strrchr(path, '/');
        strncpy(info.title, slash ? slash + 1 : path, sizeof(info.title) - 1);
    }

    f.close();
}

// Minimal Vorbis Comment parser for FLAC/OGG
void Library::parseVorbisComment(const char* path, TrackInfo& info) {
    // For FLAC: find VORBIS_COMMENT metadata block
    File f = SD.open(path);
    if (!f) return;

    uint8_t magic[4];
    f.read(magic, 4);
    bool is_flac = (magic[0] == 'f' && magic[1] == 'L' && magic[2] == 'a' && magic[3] == 'C');
    if (!is_flac) { f.close(); return; }

    // Walk metadata blocks
    while (f.available()) {
        uint8_t block_hdr[4];
        if (f.read(block_hdr, 4) != 4) break;
        bool last   = (block_hdr[0] & 0x80) != 0;
        uint8_t type = block_hdr[0] & 0x7F;
        uint32_t len = ((uint32_t)block_hdr[1] << 16) |
                       ((uint32_t)block_hdr[2] << 8)  |
                        (uint32_t)block_hdr[3];

        if (type == 4) {
            // VORBIS_COMMENT block
            uint8_t* buf = (uint8_t*)malloc(len + 1);
            if (buf) {
                f.read(buf, len);
                buf[len] = '\0';
                uint32_t off = 0;
                // vendor string
                uint32_t vlen = *(uint32_t*)(buf + off); off += 4 + vlen;
                uint32_t n_comments = *(uint32_t*)(buf + off); off += 4;
                for (uint32_t i = 0; i < n_comments && off < len; i++) {
                    uint32_t clen = *(uint32_t*)(buf + off); off += 4;
                    if (off + clen > len) break;
                    char* comment = (char*)(buf + off); off += clen;
                    char tmp[256] = {0};
                    uint32_t copy = clen < 255 ? clen : 255;
                    memcpy(tmp, comment, copy);
                    if      (strncasecmp(tmp, "TITLE=",  6) == 0) strncpy(info.title,  tmp + 6, sizeof(info.title)  - 1);
                    else if (strncasecmp(tmp, "ARTIST=", 7) == 0) strncpy(info.artist, tmp + 7, sizeof(info.artist) - 1);
                    else if (strncasecmp(tmp, "ALBUM=",  6) == 0) strncpy(info.album,  tmp + 6, sizeof(info.album)  - 1);
                    else if (strncasecmp(tmp, "TRACKNUMBER=", 12) == 0) info.track_num = atoi(tmp + 12);
                }
                free(buf);
            }
            break;
        } else {
            f.seek(f.position() + len);
        }
        if (last) break;
    }

    if (info.title[0] == '\0') {
        const char* slash = strrchr(path, '/');
        strncpy(info.title, slash ? slash + 1 : path, sizeof(info.title) - 1);
    }
    f.close();
}

void Library::indexArtistsAlbums() {
    for (int i = 0; i < m_count; i++) {
        const char* artist = m_tracks[i].artist;
        if (artist[0] != '\0') {
            bool found = false;
            for (int j = 0; j < m_artist_count; j++) {
                if (strcmp(m_artists[j], artist) == 0) { found = true; break; }
            }
            if (!found && m_artist_count < 512) m_artists[m_artist_count++] = artist;
        }
        const char* album = m_tracks[i].album;
        if (album[0] != '\0') {
            bool found = false;
            for (int j = 0; j < m_album_count; j++) {
                if (strcmp(m_albums[j], album) == 0) { found = true; break; }
            }
            if (!found && m_album_count < 512) m_albums[m_album_count++] = album;
        }
    }
}

const TrackInfo& Library::track(int idx) const {
    if (idx < 0 || idx >= m_count) return EMPTY_TRACK;
    return m_tracks[idx];
}

int Library::getFiltered(int* out, int max, const char* filter) const {
    int n = 0;
    for (int i = 0; i < m_count && n < max; i++) {
        if (!filter || filter[0] == '\0' ||
            strcasestr(m_tracks[i].title,  filter) ||
            strcasestr(m_tracks[i].artist, filter) ||
            strcasestr(m_tracks[i].album,  filter)) {
            out[n++] = i;
        }
    }
    return n;
}

const char* Library::artistName(int idx) const { return (idx >= 0 && idx < m_artist_count) ? m_artists[idx] : ""; }
const char* Library::albumName(int idx)  const { return (idx >= 0 && idx < m_album_count)  ? m_albums[idx]  : ""; }
const char* Library::genreName(int idx)  const { return (idx >= 0 && idx < m_genre_count)  ? m_genres[idx]  : ""; }

int Library::getByArtist(int artist_idx, int* out, int max) const {
    if (artist_idx < 0 || artist_idx >= m_artist_count) return 0;
    int n = 0;
    for (int i = 0; i < m_count && n < max; i++) {
        if (strcmp(m_tracks[i].artist, m_artists[artist_idx]) == 0) out[n++] = i;
    }
    return n;
}

int Library::getByAlbum(int album_idx, int* out, int max) const {
    if (album_idx < 0 || album_idx >= m_album_count) return 0;
    int n = 0;
    for (int i = 0; i < m_count && n < max; i++) {
        if (strcmp(m_tracks[i].album, m_albums[album_idx]) == 0) out[n++] = i;
    }
    return n;
}

int Library::getByGenre(int genre_idx, int* out, int max) const {
    (void)genre_idx; (void)out; (void)max;
    return 0; // genre field populated when TCON ID3 tag is parsed (extend parseID3v2)
}

int Library::findByPath(const char* path) const {
    for (int i = 0; i < m_count; i++) {
        if (strcmp(m_tracks[i].path, path) == 0) return i;
    }
    return -1;
}

int Library::getRecentlyAdded(int* out, int max) const {
    // Return last min(max, count) tracks (most recently scanned = highest index)
    int start = m_count > max ? m_count - max : 0;
    int n = 0;
    for (int i = m_count - 1; i >= start && n < max; i--) out[n++] = i;
    return n;
}

void Library::buildShuffleOrder(int* order, int count) const {
    for (int i = 0; i < count; i++) order[i] = i;
    for (int i = count - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
}

void Library::setBrowseMode(BrowseMode mode) { m_mode = mode; }
