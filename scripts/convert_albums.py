#!/usr/bin/env python3
"""Convert album images to RGB565 .bin files and generate metadata.csv for the CYD.

Usage:
  1) Drop JPG/PNG album art into scripts/input_albums/
     Naming convention: Artist-Album_Title.jpg  (hyphen separates artist from album)
     Examples:
       Kings_of_Leon-Aha_Shake_Heartbreak.jpg
       Arctic_Monkeys-Tranquility_Base.png
  2) Run:  python scripts/convert_albums.py
  3) Copy the contents of scripts/sd_card_albums/ to your SD card

Output:
  - scripts/sd_card_albums/*.bin    (120x120 RGB565 raw images)
  - scripts/sd_card_albums/metadata.csv  (title,artist,filename — copy this to SD too!)
"""
import os
import struct
import sys
import csv

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow library is required. Install it using: pip install Pillow")
    sys.exit(1)

TARGET_SIZE = (80, 80)

def guess_metadata(filename_stem):
    """Try to guess title and artist from filename. Falls back to filename as title."""

    # Try splitting on hyphen: "Artist-Album_Title"
    if '-' in filename_stem:
        parts = filename_stem.split('-', 1)
        artist = parts[0].replace('_', ' ').strip()
        title = parts[1].replace('_', ' ').strip()
        return (title, artist, "")

    # Fallback: use filename as title
    return (filename_stem.replace('_', ' '), "Unknown", "")


def convert_to_rgb565(input_path, output_path, size):
    img = Image.open(input_path).convert('RGB')
    img = img.resize(size, Image.Resampling.LANCZOS)
    pixels = img.load()
    width, height = img.size

    with open(output_path, 'wb') as f:
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                f.write(struct.pack('>H', rgb565))


def process_directory(input_dir, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    albums = []
    for filename in sorted(os.listdir(input_dir)):
        if filename.lower().endswith(('.png', '.jpg', '.jpeg', '.webp')):
            input_path = os.path.join(input_dir, filename)
            stem = os.path.splitext(filename)[0]
            bin_filename = stem + ".bin"
            output_path = os.path.join(output_dir, bin_filename)

            print(f"  Converting: {filename} -> {bin_filename}")
            convert_to_rgb565(input_path, output_path, TARGET_SIZE)

            title, artist, uri = guess_metadata(stem)
            albums.append((bin_filename, title, artist, uri))

    # 1. Read existing metadata.csv to preserve user-added URIs
    existing_uris = {}
    csv_path = os.path.join(output_dir, "metadata.csv")
    if os.path.exists(csv_path):
        with open(csv_path, 'r', newline='', encoding='utf-8') as f:
            reader = csv.reader(f)
            for row in reader:
                if len(row) >= 4 and row[3].strip():
                    existing_uris[row[0]] = row[3].strip()

    # 2. Write updated metadata.csv
    with open(csv_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        for bin_name, title, artist, default_uri in albums:
            # Prefer the URI they already typed in the CSV over the script default
            final_uri = existing_uris.get(bin_name, default_uri)
            writer.writerow([bin_name, title, artist, final_uri])

    print(f"\n  Done! {len(albums)} albums converted.")
    print(f"  Metadata written to: {csv_path} (Existing Spotify URIs were preserved!)")
    print(f"\n  Copy the entire '{os.path.basename(output_dir)}' folder to your SD card root.")


if __name__ == "__main__":
    current_dir = os.path.dirname(os.path.abspath(__file__))
    input_directory = os.path.join(current_dir, "input_albums")
    output_directory = os.path.join(current_dir, "sd_card_albums")

    if not os.path.exists(input_directory):
        os.makedirs(input_directory)
        print(f"Created folder: {input_directory}")
        print("Please drop your album JPG/PNG files in there and run this script again.")
    else:
        print("Converting album art to RGB565...\n")
        process_directory(input_directory, output_directory)
