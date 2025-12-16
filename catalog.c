#include "catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

Star *stars = NULL;
int num_stars = 0;

Constellation *constellations = NULL;
int num_constellations = 0;

// Helper to parse Hipparcos line
// Format: Pipe separated.
// H1: HIP (Field 1, index 1 if 1-based, index 0 if 0-based split?)
// ReadMe says H1 is field 1.
// H5: Vmag (Field 5)
// H8: RA deg (Field 8)
// H9: Dec deg (Field 9)
static int parse_hip_line(char *line, Star *s) {
    // We need columns 1 (HIP), 5 (Vmag), 8 (RA), 9 (Dec).
    // Note: strtok modifies string.
    // Fields can be empty? "        |"
    // strtok skips empty tokens? Yes.
    // We must use strsep or custom loop to handle empty fields correctly.

    char *ptr = line;
    char *field_start = line;
    int field_idx = 1;

    char hip_str[16] = {0};
    char vmag_str[16] = {0};
    char ra_str[32] = {0};
    char dec_str[32] = {0};

    while (*ptr) {
        if (*ptr == '|') {
            *ptr = '\0';
            // Process field at field_start
            if (field_idx == 1) strncpy(hip_str, field_start, 15);
            else if (field_idx == 5) strncpy(vmag_str, field_start, 15);
            else if (field_idx == 8) strncpy(ra_str, field_start, 31);
            else if (field_idx == 9) strncpy(dec_str, field_start, 31);

            field_start = ptr + 1;
            field_idx++;
        }
        ptr++;
    }
    // Last field if needed (but H9 is not last)

    // Check if we got data
    if (ra_str[0] == 0 || dec_str[0] == 0) return 0;

    // Remove whitespace
    // sscanf handles whitespace usually.

    // HIP
    // s->id? We don't store ID in current struct.

    // Mag
    if (vmag_str[0]) s->mag = atof(vmag_str);
    else s->mag = 100.0; // Unknown/Dim

    // RA/Dec
    s->ra = atof(ra_str);
    s->dec = atof(dec_str);

    return 1;
}

int load_catalog() {
    // 1. Load Stars from Hipparcos
    FILE *f = fopen("hip_main.dat", "r");
    if (!f) {
        fprintf(stderr, "Error: hip_main.dat not found.\n");
        // Fallback or fail?
        // Let's try stars.6.json logic if hip fails?
        // User asked to use Hipparcos.
        return -1;
    }

    // Count lines or realloc
    // Hipparcos has ~118k lines.
    stars = calloc(120000, sizeof(Star));
    num_stars = 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (num_stars >= 120000) {
            fprintf(stderr, "Warning: Reached maximum star limit (120000). Stopping load.\n");
            break;
        }
        if (parse_hip_line(line, &stars[num_stars])) {
            num_stars++;
        }
    }
    fclose(f);
    printf("Loaded %d stars from Hipparcos catalog.\n", num_stars);

    // 2. Load Constellations from JSON (Same as before)
    json_error_t error;
    json_t *root = json_load_file("constellations.lines.json", 0, &error);
    if (!root) {
        fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
        return -1;
    }

    json_t *features = json_object_get(root, "features");

    if (!json_is_array(features)) {
        json_decref(root);
        return -1;
    }

    num_constellations = json_array_size(features);
    constellations = malloc(sizeof(Constellation) * num_constellations);

    for(int i = 0; i < num_constellations; i++) {
        json_t *feature = json_array_get(features, i);
        json_t *geometry = json_object_get(feature, "geometry");
        json_t *coordinates = json_object_get(geometry, "coordinates");
        json_t *id_obj = json_object_get(feature, "id");

        const char *id_str = json_string_value(id_obj);
        constellations[i].id = malloc(32);
        if (constellations[i].id) {
            if(id_str) {
                strncpy(constellations[i].id, id_str, 31);
                constellations[i].id[31] = '\0';
            } else {
                strcpy(constellations[i].id, "UNK");
            }
        }

        if(!json_is_array(coordinates)) continue;

        int num_lines = json_array_size(coordinates);
        constellations[i].num_lines = num_lines;
        constellations[i].line_lengths = malloc(sizeof(int) * num_lines);
        constellations[i].lines = malloc(sizeof(double*) * num_lines);

        for(int j = 0; j < num_lines; j++) {
            json_t *line = json_array_get(coordinates, j);
            int len = json_array_size(line);
            constellations[i].line_lengths[j] = len;
            constellations[i].lines[j] = malloc(sizeof(double) * len * 2);

            for(int k = 0; k < len; k++) {
                json_t *point = json_array_get(line, k);
                double ra = json_number_value(json_array_get(point, 0));
                double dec = json_number_value(json_array_get(point, 1));
                constellations[i].lines[j][k*2] = ra;
                constellations[i].lines[j][k*2+1] = dec;
            }
        }
    }

    json_decref(root);
    return 0;
}

void free_catalog() {
    if (stars) free(stars);

    if (constellations) {
        for (int i=0; i<num_constellations; i++) {
            for (int j=0; j<constellations[i].num_lines; j++) {
                free(constellations[i].lines[j]);
            }
            free(constellations[i].lines);
            free(constellations[i].line_lengths);
            if (constellations[i].id) free(constellations[i].id);
        }
        free(constellations);
    }
}
