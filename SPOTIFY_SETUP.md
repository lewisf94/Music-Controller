# Spotify API Setup Guide

To control Spotify from your CYD without a complex server, the device needs direct access to your account. This is completely safe as the keys stay on your device.

Please follow these steps to get your **Client ID**, **Client Secret**, and **Refresh Token**, and paste them into `src/main.cpp`.

## Step 1: Create a Spotify App
1. Go to the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
2. Log in and click **Create app**.
3. Fill in the required details:
    *   **App Name:** (e.g., "CYD Music Controller")
    *   **App Description:** (Whatever you like)
   *   **Redirect URI:** Enter exactly `http://localhost:8888/callback` (Do *not* include any quotes). 
   *   **Redirect URI:** Enter exactly `http://127.0.0.1:8888/callback` (Do *not* include any quotes). 
        > *Note: If it complains about security, ensure you are using `127.0.0.1` and not `localhost`.*
5. Once created, click **Settings** on your app page.
6. Copy the **Client ID** and **Client Secret** (you have to click "View client secret").
   * 👉 Paste these into lines 100-101 of `src/main.cpp`.

## Step 2: Get a Refresh Token
Since the CYD cannot open a web browser to log you in, we need to generate a "Refresh Token" once on your computer.

Because you have Python installed, I have created a small helper script for this!

1. Open a terminal in this project folder.
2. Run this command to install the required library:
   `pip install spotipy`
3. Run the helper script:
   `python get_spotify_token.py`
4. The script will ask you for your **Client ID** and **Client Secret**. Paste them in.
5. A web browser will open asking you to agree to connect to Spotify. Click **Agree**.
6. The script will print your **Refresh Token**.
   * 👉 Paste this into line 102 of `src/main.cpp`.

## Step 3: Add WiFi & Build
1. Add your home `YOUR_WIFI_SSID` and `YOUR_WIFI_PASSWORD` to `src/main.cpp`.
2. Click the **PlatformIO Build** button (the tick at the bottom of VS Code) and Upload to your CYD!
