#ifndef _SPOTIFY_H_
#define _SPOTIFY_H_

#include <Arduino.h>
#include <vector>

void spotify_init(const char* ssid, const char* password, const char* clientId, const char* clientSecret, const char* refreshToken);

// Call this in loop to periodically check token validity and player state
void spotify_update();

// Command Spotify to play a specific album URI (e.g., "spotify:album:123456")
bool spotify_play_album(const char* album_uri);

struct SpotifyTrackInfo {
    bool is_playing;
    char title[64];
    char artist[64];
    char album[64];
    uint32_t progress_ms;
    uint32_t duration_ms;
    char album_art_url[128];
    int local_album_idx;
};

extern SpotifyTrackInfo current_track_info;
extern bool track_info_updated;

void spotify_fetch_currently_playing();

#endif // _SPOTIFY_H_
