#!/usr/bin/env python3
"""Convert RGB565 .bin files into C PROGMEM header files for embedding in ESP32 firmware."""
import os
import sys

# Maximum albums to embed (each is 28.8KB, ESP32 has ~337KB free flash)
MAX_ALBUMS = 9

# Album metadata: (bin_filename_prefix, display_name, spotify_uri)
# Order these however you want them to appear in the Cover Flow
ALBUM_LIST = [
    ("Aha_Shake_Heartbreak-Kings_of_Leon",          "Kings of Leon",       "spotify:album:5CnpZV3q5BcESefcB3WJmz"),
    ("Arctic_Monkeys_–_Tranquility_Base_Hotel",      "Arctic Monkeys",      "spotify:album:1jeMiSeSnNS0Oys375qegp"),
    ("Cage_the_Elephant_Social_Cues",                "Cage the Elephant",   "spotify:album:41bTjJBZMFBVMI9GtNNriq"),
    ("Fontaines_D.C.-Skinty_Fia",                    "Fontaines D.C.",      "spotify:album:3GqMHMTyoMEiRWsPtCEMCz"),
    ("Gorillaz (Gorillaz)_2001_album",               "Gorillaz",            "spotify:album:0bUTHlWbkSQysoM3VsWldT"),
    ("Grian_Chatten-Chaos_for_the_Fly",              "Grian Chatten",       "spotify:album:2vbsM0VHzIxsPMFbkKyqTa"),
    ("Kendrick_Lamar-To_Pimp_a_Butterfly",           "Kendrick Lamar",      "spotify:album:7ycBtnsMtyVbbwTfJwRjSP"),
    ("Loyle_Carner_-_Hugo",                          "Loyle Carner",        "spotify:album:2ryMzFzazSdEJxMEVJkkRB"),
    ("Magdalena_Bay-Imaginal_Disk",                  "Magdalena Bay",       "spotify:album:2IysJM9VNnOWLhhIhj4eNo"),
    ("Plasticbeach-Gorillaz",                        "Gorillaz",            "spotify:album:2dIGnmEIy1WZIcZCFSj6i8"),
    ("The_Strokes-Is_This_It_cover",                 "The Strokes",         "spotify:album:2k8KgmDp9oHrJlJlkyjMrB"),
    ("The_Strokes-The_New_Abnormal",                 "The Strokes",         "spotify:album:2xkZV2Hl1Omi1fDP7xaujy"),
    ("What_Went_Down-Foals",                         "Foals",               "spotify:album:5wHFPIn5SqfOaHNkShfnZr"),
    ("Whatever_People_Say_I_Am",                     "Arctic Monkeys",      "spotify:album:6rsQnwaoJHxXJRCDBPIYfL"),
]

# Album title (parsed from filename for display)
ALBUM_TITLES = [
    "Aha Shake Heartbreak",
    "Tranquility Base",
    "Social Cues",
    "Skinty Fia",
    "Gorillaz",
    "Chaos for the Fly",
    "To Pimp a Butterfly",
    "Hugo",
    "Imaginal Disk",
    "Plastic Beach",
    "Is This It",
    "The New Abnormal",
    "What Went Down",
    "Whatever People Say",
]

import struct

def bin_to_header(bin_path, header_path, var_name):
    with open(bin_path, 'rb') as f:
        data = f.read()

    with open(header_path, 'w') as f:
        f.write(f"// Auto-generated from {os.path.basename(bin_path)}\n")
        f.write(f"// {len(data)} bytes = {len(data)//2} pixels (120x120 RGB565)\n")
        f.write(f"#pragma once\n")
        f.write(f"#include <Arduino.h>\n\n")
        f.write(f"const uint16_t {var_name}[{len(data)//2}] PROGMEM = {{\n")

        for i in range(0, len(data), 2):
            if i % 24 == 0:
                f.write("  ")
            val = (data[i] << 8) | data[i+1]
            f.write(f"0x{val:04X},")
            if (i + 2) % 24 == 0:
                f.write("\n")

        f.write("};\n")
    print(f"  {os.path.basename(bin_path)} -> {os.path.basename(header_path)} ({len(data)} bytes)")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    bin_dir = os.path.join(script_dir, "sd_card_albums")
    out_dir = os.path.join(os.path.dirname(script_dir), "src", "album_art")

    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    bins = sorted([f for f in os.listdir(bin_dir) if f.endswith('.bin')])
    if not bins:
        print("No .bin files found!")
        return

    # Match album list to actual bin files
    matched = []
    for prefix, artist, uri in ALBUM_LIST:
        for b in bins:
            if b.startswith(prefix):
                matched.append((b, artist, uri))
                break

    # Limit to MAX_ALBUMS
    to_embed = matched[:MAX_ALBUMS]
    print(f"Embedding {len(to_embed)} of {len(matched)} albums into PROGMEM headers...\n")

    names = []
    for i, (bin_name, artist, uri) in enumerate(to_embed):
        var_name = f"album_art_{i}"
        header_name = f"album_{i}.h"
        bin_to_header(
            os.path.join(bin_dir, bin_name),
            os.path.join(out_dir, header_name),
            var_name
        )
        idx = next((j for j, (p, _, _) in enumerate(ALBUM_LIST) if bin_name.startswith(p)), i)
        title = ALBUM_TITLES[idx] if idx < len(ALBUM_TITLES) else "Unknown"
        names.append((var_name, title, artist, uri, header_name))

    # Write index header
    with open(os.path.join(out_dir, "album_art.h"), 'w') as f:
        f.write("// Auto-generated album art index\n")
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        for _, _, _, _, hdr in names:
            f.write(f'#include "{hdr}"\n')
        f.write(f"\n#define EMBEDDED_ALBUM_COUNT {len(names)}\n\n")
        f.write("const uint16_t* embedded_albums[] = {\n")
        for var, _, _, _, _ in names:
            f.write(f"  {var},\n")
        f.write("};\n\n")
        f.write("const char* embedded_album_titles[] = {\n")
        for _, title, _, _, _ in names:
            f.write(f'  "{title}",\n')
        f.write("};\n\n")
        f.write("const char* embedded_album_artists[] = {\n")
        for _, _, artist, _, _ in names:
            f.write(f'  "{artist}",\n')
        f.write("};\n\n")
        f.write("const char* embedded_album_uris[] = {\n")
        for _, _, _, uri, _ in names:
            f.write(f'  "{uri}",\n')
        f.write("};\n")

    print(f"\nDone! {len(names)} albums embedded.")
    print(f"Estimated flash usage: ~{len(names) * 28800 // 1024}KB for images")

if __name__ == "__main__":
    main()
