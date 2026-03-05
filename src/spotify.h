#ifndef _SPOTIFY_H_
#define _SPOTIFY_H_

#include <Arduino.h>
#include <vector>

void spotify_init(const char* ssid, const char* password, const char* clientId, const char* clientSecret, const char* refreshToken);

// Call this in loop to periodically check token validity and player state
void spotify_update();

// Command Spotify to play a specific album URI (e.g., "spotify:album:123456")
bool spotify_play_album(const char* album_uri);

#endif // _SPOTIFY_H_
