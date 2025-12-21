#!/bin/bash

# URLs for the data files
# Hipparcos Catalog I/239 from VizieR (Tarball)
HIP_TAR_URL="https://cdsarc.cds.unistra.fr/viz-bin/nph-Cat/tar.gz?I/239"
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

# Clean up broken attempts
if [ -f "hip_main.dat.gz" ] && [ ! -s "hip_main.dat.gz" ]; then
    rm hip_main.dat.gz
fi

# Download Hipparcos
if [ ! -f "hip_main.dat" ]; then
    echo "Downloading Hipparcos Catalog..."
    download_file "$HIP_TAR_URL" "I_239.tar.gz"

    echo "Extracting Hipparcos Catalog..."
    tar -xvf I_239.tar.gz --wildcards "*hip_main.dat" --transform='s/.*\///'

    if [ -f "hip_main.dat.gz" ]; then
        gunzip hip_main.dat.gz
    fi

    rm I_239.tar.gz
fi

# Download Constellations
download_file "$CONSTELLATIONS_URL" "constellations.lines.json"

echo "Data download complete."
