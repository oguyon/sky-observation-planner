#include "catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Star *stars = NULL;
int num_stars = 0;

Constellation *constellations = NULL;
int num_constellations = 0;

static int load_stars(const char *filename) {
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "Error loading stars: %s line %d: %s\n", filename, error.line, error.text);
        return -1;
    }

    json_t *features = json_object_get(root, "features");
    if (!json_is_array(features)) {
        json_decref(root);
        return -1;
    }

    num_stars = json_array_size(features);
    stars = malloc(sizeof(Star) * num_stars);

    for (int i = 0; i < num_stars; i++) {
        json_t *feature = json_array_get(features, i);
        json_t *geometry = json_object_get(feature, "geometry");
        json_t *coordinates = json_object_get(geometry, "coordinates");
        json_t *properties = json_object_get(feature, "properties");

        // coordinates: [ra, dec]
        if (json_is_array(coordinates) && json_array_size(coordinates) >= 2) {
            double ra = json_number_value(json_array_get(coordinates, 0));
            double dec = json_number_value(json_array_get(coordinates, 1));
            double mag = json_number_value(json_object_get(properties, "mag"));

            stars[i].ra = ra;
            stars[i].dec = dec;
            stars[i].mag = mag;
            stars[i].id = NULL;
        } else {
            // Handle error or skip
            stars[i].ra = 0;
            stars[i].dec = 0;
            stars[i].mag = 0;
        }
    }

    json_decref(root);
    return 0;
}

static int load_constellations(const char *filename) {
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "Error loading constellations: %s line %d: %s\n", filename, error.line, error.text);
        return -1;
    }

    json_t *features = json_object_get(root, "features");
    if (!json_is_array(features)) {
        json_decref(root);
        return -1;
    }

    num_constellations = json_array_size(features);
    constellations = malloc(sizeof(Constellation) * num_constellations);

    for (int i = 0; i < num_constellations; i++) {
        json_t *feature = json_array_get(features, i);
        json_t *id_obj = json_object_get(feature, "id");
        const char *id_str = json_string_value(id_obj);

        constellations[i].id = id_str ? strdup(id_str) : strdup("");

        json_t *geometry = json_object_get(feature, "geometry");
        json_t *coordinates = json_object_get(geometry, "coordinates");

        int num_lines = json_array_size(coordinates);
        constellations[i].num_lines = num_lines;
        constellations[i].line_lengths = malloc(sizeof(int) * num_lines);
        constellations[i].lines = malloc(sizeof(double*) * num_lines);

        for(int j=0; j<num_lines; j++) {
            json_t *line = json_array_get(coordinates, j);
            int num_points = json_array_size(line);
            constellations[i].line_lengths[j] = num_points;
            constellations[i].lines[j] = malloc(sizeof(double) * num_points * 2);

            for(int k=0; k<num_points; k++) {
                json_t *pt = json_array_get(line, k);
                if (json_is_array(pt) && json_array_size(pt) >= 2) {
                    double ra = json_number_value(json_array_get(pt, 0));
                    double dec = json_number_value(json_array_get(pt, 1));
                    constellations[i].lines[j][k*2] = ra;
                    constellations[i].lines[j][k*2+1] = dec;
                }
            }
        }
    }

    json_decref(root);
    return 0;
}

int load_catalog() {
    if (load_stars("stars.6.json") != 0) return -1;
    if (load_constellations("constellations.lines.json") != 0) return -1;
    return 0;
}

void free_catalog() {
    if (stars) free(stars);
    for (int i=0; i<num_constellations; i++) {
        free(constellations[i].id);
        free(constellations[i].line_lengths);
        for(int j=0; j<constellations[i].num_lines; j++) {
            free(constellations[i].lines[j]);
        }
        free(constellations[i].lines);
    }
    if (constellations) free(constellations);
}
