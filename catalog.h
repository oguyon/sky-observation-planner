#ifndef CATALOG_H
#define CATALOG_H

#include <jansson.h>

typedef struct {
    double ra; // degrees
    double dec; // degrees
    double mag;
    const char *id; // Can be null or numeric id as string
} Star;

typedef struct {
    char *id;
    int num_lines;
    int *line_lengths; // Array of size num_lines
    double **lines; // Array of array of points. points are stored as ra1, dec1, ra2, dec2...
} Constellation;

// Global Arrays
extern Star *stars;
extern int num_stars;

extern Constellation *constellations;
extern int num_constellations;

int load_catalog();
void free_catalog();

#endif
