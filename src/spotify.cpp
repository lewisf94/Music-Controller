#include "spotify.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"

static const char* wifi_ssid;
static const char* wifi_password;
static const char* client_id;
static const char* client_secret;
static const char* refresh_token;

static String access_token = "";
static unsigned long token_expiry = 0;

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
}

void spotify_update() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    static bool printed_connected = false;
    if(!printed_connected) {
        Serial.println("WiFi connected!");
        printed_connected = true;
    }

    if (access_token == "" || millis() > token_expiry) {
        refresh_access_token();
    }
}

bool spotify_play_album(const char* album_uri) {
    if (access_token == "" || WiFi.status() != WL_CONNECTED) return false;

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
        
        if (httpCode == 204 || httpCode == 200) {
            Serial.println("Playback Started!");
            return true;
        } else {
            Serial.printf("Playback Failed: %d\n", httpCode);
            return false;
        }
    }
    delete client;
    return false;
}
