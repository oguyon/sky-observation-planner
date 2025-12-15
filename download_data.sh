#!/bin/bash

# URLs for the data files
STARS_URL="https://raw.githubusercontent.com/ofrohn/d3-celestial/master/data/stars.6.json"
CONSTELLATIONS_URL="https://raw.githubusercontent.com/ofrohn/d3-celestial/master/data/constellations.lines.json"

# Function to download a file if it doesn't exist
download_file() {
    local url=$1
    local filename=$2

    if [ -f "$filename" ]; then
        echo "$filename already exists. Skipping download."
    else
        echo "Downloading $filename..."
        if command -v wget >/dev/null 2>&1; then
            wget "$url" -O "$filename"
        elif command -v curl >/dev/null 2>&1; then
            curl -L "$url" -o "$filename"
        else
            echo "Error: Neither wget nor curl is available. Please install one of them."
            exit 1
        fi

        if [ $? -eq 0 ]; then
            echo "$filename downloaded successfully."
        else
            echo "Failed to download $filename."
            exit 1
        fi
    fi
}

# Download the files
download_file "$STARS_URL" "stars.6.json"
download_file "$CONSTELLATIONS_URL" "constellations.lines.json"

echo "Data download complete."
