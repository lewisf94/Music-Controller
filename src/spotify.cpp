#include "spotify.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include <SD.h>
#include "ui.h"

static const char* wifi_ssid;
static const char* wifi_password;
static const char* client_id;
static const char* client_secret;
static const char* refresh_token;

static String access_token = "";
static unsigned long token_expiry = 0;

SpotifyTrackInfo current_track_info = {false, "", "", "", 0, 0, "", -1};
bool track_info_updated = false;

static void download_album_art(const char* url) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    ui_suspend_sprite();
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();
    HTTPClient https;
    if (https.begin(*client, url)) {
        int httpCode = https.GET();
        if (httpCode == HTTP_CODE_OK) {
            SD.remove("/sd_card_albums/nowplaying.jpg");
            File f = SD.open("/sd_card_albums/nowplaying.jpg", FILE_WRITE);
            if (f) {
                https.writeToStream(&f);
                f.close();
                Serial.println("Album art downloaded to /sd_card_albums/nowplaying.jpg");
            } else {
                Serial.println("Failed to open /sd_card_albums/nowplaying.jpg for writing");
            }
        }
    }
    https.end();
    delete client;
    ui_resume_sprite();
}



// DigiCert Global Root G2 (used by accounts.spotify.com and api.spotify.com)
static const char* const spotify_ca =
"-----BEGIN CERTIFICATE-----\n"
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCGHKhHm3bpO3w4BKyA+y+r\n"
"nSWm2oE8LGBn0R0P1Kk2vQo1E9B1jH+B7bYyZ4R/uB9iP++i8P9N/fT9P9L8bI+M\n"
"B9o/Q9M=-----END CERTIFICATE-----\n";

String base64_encode(String text) {
    unsigned char output[256];
    size_t olen;
    mbedtls_base64_encode(output, 256, &olen, (const unsigned char*)text.c_str(), text.length());
    return String((char*)output, olen);
}

void spotify_init(const char* ssid, const char* password, const char* clientId, const char* clientSecret, const char* refreshToken) {
    wifi_ssid = ssid;
    wifi_password = password;
    client_id = clientId;
    client_secret = clientSecret;
    refresh_token = refreshToken;

    Serial.print("Connecting to WiFi: ");
    Serial.println(wifi_ssid);
    WiFi.begin(wifi_ssid, wifi_password);
}

void refresh_access_token() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    ui_suspend_sprite();
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure(); // Use insecure for auth slightly faster, we'll secure the API later

    HTTPClient https;
    if (https.begin(*client, "https://accounts.spotify.com/api/token")) {
        https.addHeader("Content-Type", "application/x-www-form-urlencoded");
        String auth_str = String(client_id) + ":" + String(client_secret);
        https.addHeader("Authorization", "Basic " + base64_encode(auth_str));

        String payload = "grant_type=refresh_token&refresh_token=" + String(refresh_token);
        int httpCode = https.POST(payload);

        if (httpCode == HTTP_CODE_OK) {
            String response = https.getString();
            JsonDocument doc;
            deserializeJson(doc, response);
            access_token = doc["access_token"].as<String>();
            token_expiry = millis() + (doc["expires_in"].as<int>() * 1000) - 60000;
            Serial.println("Spotify Token Refreshed!");
        } else {
            Serial.printf("Spotify Auth Failed: %d\n", httpCode);
            Serial.println(https.getString());
        }
        https.end();
    }
    delete client;
    ui_resume_sprite();
}

void spotify_update() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    static bool printed_connected = false;
    if(!printed_connected) {
        Serial.println("WiFi connected!");
        printed_connected = true;
    }

    static unsigned long last_auth_attempt = 0;
    static bool auth_attempted = false;
    
    // Attempt auth if token is missing/expired, but no more than once every 5 seconds to prevent memory leaks/freezes
    if (access_token == "" || millis() > token_expiry) {
        if (!auth_attempted || millis() - last_auth_attempt > 5000) {
            auth_attempted = true;
            last_auth_attempt = millis();
            refresh_access_token();
        }
    }

    static unsigned long last_fetch = 0;
    // Fetch currently playing every 2 seconds when we have a token
    if (access_token != "" && millis() - last_fetch > 2000) {
        last_fetch = millis();
        // Since network calls block the UI, we only do this periodically
        spotify_fetch_currently_playing();
    }
}

bool spotify_play_album(const char* album_uri) {
    if (access_token == "" || WiFi.status() != WL_CONNECTED) return false;

    ui_suspend_sprite();
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure(); // skipping cert validation for now for speed

    HTTPClient https;
    if (https.begin(*client, "https://api.spotify.com/v1/me/player/play")) {
        https.addHeader("Authorization", "Bearer " + access_token);
        https.addHeader("Content-Type", "application/json");
        
        String payload = "{\"context_uri\": \"" + String(album_uri) + "\"}";
        
        int httpCode = https.PUT(payload);
        https.end();
        delete client;
        ui_resume_sprite();
        
        if (httpCode == 204 || httpCode == 200) {
            Serial.println("Playback Started!");
            return true;
        } else {
            Serial.printf("Playback Failed: %d\n", httpCode);
            return false;
        }
    }
    delete client;
    ui_resume_sprite();
    return false;
}

void spotify_fetch_currently_playing() {
    if (access_token == "" || WiFi.status() != WL_CONNECTED) return;

    ui_suspend_sprite();
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();

    HTTPClient https;
    if (https.begin(*client, "https://api.spotify.com/v1/me/player/currently-playing")) {
        https.addHeader("Authorization", "Bearer " + access_token);
        int httpCode = https.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, https.getStream());
            if (!error && doc["item"]) {
                current_track_info.is_playing = doc["is_playing"].as<bool>();
                
                const char* new_title = doc["item"]["name"] | "Unknown Track";
                const char* new_artist = doc["item"]["artists"][0]["name"] | "Unknown Artist";
                const char* new_album = doc["item"]["album"]["name"] | "Unknown Album";
                const char* new_url = doc["item"]["album"]["images"][0]["url"] | "";
                
                current_track_info.progress_ms = doc["progress_ms"].as<uint32_t>();
                current_track_info.duration_ms = doc["item"]["duration_ms"].as<uint32_t>();
                current_track_info.local_album_idx = -1;
                
                bool song_changed = strncmp(current_track_info.title, new_title, 63) != 0;
                
                strncpy(current_track_info.title, new_title, 63);
                current_track_info.title[63] = '\0';
                strncpy(current_track_info.artist, new_artist, 63);
                current_track_info.artist[63] = '\0';
                strncpy(current_track_info.album, new_album, 63);
                current_track_info.album[63] = '\0';
                
                if (song_changed || strncmp(current_track_info.album_art_url, new_url, 127) != 0) {
                    strncpy(current_track_info.album_art_url, new_url, 127);
                    current_track_info.album_art_url[127] = '\0';
                    if (new_url[0] != '\0') {
                        // Pass through to download_album_art which has its own suspend/resume,
                        // so we momentarily resume before calling it.
                        ui_resume_sprite();
                        download_album_art(new_url);
                        ui_suspend_sprite();
                    }
                }
                track_info_updated = true;
            }
        } else if (httpCode == 204) {
            current_track_info.is_playing = false;
        } else {
            Serial.printf("Currently Playing Error: %d\n", httpCode);
        }
    }
    https.end();
    delete client;
    ui_resume_sprite();
}
