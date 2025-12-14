#ifndef SKY_MODEL_H
#define SKY_MODEL_H

#include <libnova/libnova.h>
#include <stdbool.h>

typedef struct {
    char name[32];
    struct ln_equ_posn pos; // RA/Dec
    double mag;
    int constellation_id;
} Star;

typedef struct {
    int id;
    char name[32];
    int star_indices[20]; // Indices into the stars array, -1 terminated pair list
} Constellation;

// Application State
typedef struct {
    struct ln_lnlat_posn observer_location; // Lat/Lon
    struct ln_date date; // Current time

    Star *stars;
    int num_stars;

    Constellation *constellations;
    int num_constellations;

    // Selection
    bool has_selection;
    struct ln_equ_posn selection_equ; // RA/Dec of selection
    char selection_name[64];

    // Settings
    bool show_constellations;
} AppState;

void init_data(AppState *app);
void free_data(AppState *app);

#endif
