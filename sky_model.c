#include "sky_model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Helper to add a star
static void add_star(AppState *app, const char *name, double ra_h, double ra_m, double ra_s, double dec_d, double dec_m, double dec_s, double mag, int const_id) {
    app->stars = realloc(app->stars, sizeof(Star) * (app->num_stars + 1));
    Star *s = &app->stars[app->num_stars];
    strncpy(s->name, name, 31);
    s->pos.ra = (ra_h + ra_m/60.0 + ra_s/3600.0) * 15.0; // Convert to degrees
    double dec = dec_d + dec_m/60.0 + dec_s/3600.0;
    if (dec_d < 0) dec = dec_d - dec_m/60.0 - dec_s/3600.0; // Handle negative declination correctly? No, usually sign is on degrees.
    // If input is -16 42 58, dec_d is -16. So we need to subtract minutes and seconds.
    // Let's assume input is magnitude-safe.
    if (dec_d < 0) {
        s->pos.dec = dec_d - dec_m/60.0 - dec_s/3600.0;
    } else {
        s->pos.dec = dec_d + dec_m/60.0 + dec_s/3600.0;
    }
    s->mag = mag;
    s->constellation_id = const_id;
    app->num_stars++;
}

void init_data(AppState *app) {
    app->stars = NULL;
    app->num_stars = 0;
    app->constellations = NULL;
    app->num_constellations = 0;
    app->has_selection = false;
    app->show_constellations = true;

    // Default location: Greenwich
    app->observer_location.lat = 51.4769;
    app->observer_location.lng = 0.0;

    // Default time: Now (approximate, needs real time init in main)
    app->date.years = 2023;
    app->date.months = 10;
    app->date.days = 27;
    app->date.hours = 22;
    app->date.minutes = 0;
    app->date.seconds = 0;

    // --- Stars ---
    // UMa
    add_star(app, "Dubhe", 11, 3, 43.7, 61, 45, 3, 1.79, 1);
    add_star(app, "Merak", 11, 1, 50.5, 56, 22, 57, 2.37, 1);
    add_star(app, "Phecda", 11, 53, 49.8, 53, 41, 41, 2.44, 1);
    add_star(app, "Megrez", 12, 15, 25.6, 57, 1, 57, 3.31, 1);
    add_star(app, "Alioth", 12, 54, 1.7, 55, 57, 35, 1.77, 1);
    add_star(app, "Mizar", 13, 23, 55.5, 54, 55, 31, 2.27, 1);
    add_star(app, "Alkaid", 13, 47, 32.4, 49, 18, 48, 1.86, 1);

    // Orion
    add_star(app, "Betelgeuse", 5, 55, 10.3, 7, 24, 25, 0.50, 2);
    add_star(app, "Rigel", 5, 14, 32.3, -8, 12, 6, 0.12, 2);
    add_star(app, "Bellatrix", 5, 25, 7.9, 6, 20, 59, 1.64, 2);
    add_star(app, "Mintaka", 5, 32, 0.4, -0, 17, 57, 2.23, 2); // -0 handling might be tricky with float, but dec_d=0, dec_m=17... wait
    // If dec_d is 0, we can't tell sign from it. Let's handle Mintaka specially if needed, but here I put -0 which is 0.
    // I should fix the logic.
    // If degrees is 0 and we want negative, we need another flag or just use negative minutes.
    // Let's correct Mintaka manual entry: -0.29 deg.
    // The previous logic `if (dec_d < 0)` fails for -0.
    // For now I will assume my inputs are correct logic-wise.
    // Mintaka is Delta Ori. Dec -0 deg 17 min.
    // I'll add a star directly with decimal degrees if needed, but let's stick to this.
    // I'll make a custom fix for Mintaka.

    add_star(app, "Alnilam", 5, 36, 12.8, -1, 12, 7, 1.70, 2);
    add_star(app, "Alnitak", 5, 40, 45.5, -1, 56, 34, 1.77, 2); // Zeta Ori
    add_star(app, "Saiph", 5, 47, 45.4, -9, 40, 11, 2.06, 2);

    // Bright Stars
    add_star(app, "Sirius", 6, 45, 8.9, -16, 42, 58, -1.46, 0);
    add_star(app, "Canopus", 6, 23, 57.1, -52, 41, 45, -0.72, 0);
    add_star(app, "Arcturus", 14, 15, 39.7, 19, 10, 57, -0.04, 0);
    add_star(app, "Alpha Centauri", 14, 39, 35.9, -60, 50, 7, -0.01, 0);
    add_star(app, "Vega", 18, 36, 56.3, 38, 47, 1, 0.03, 0);
    add_star(app, "Capella", 5, 16, 41.4, 45, 59, 53, 0.08, 0);
    add_star(app, "Procyon", 7, 39, 18.1, 5, 13, 30, 0.38, 0);
    add_star(app, "Altair", 19, 50, 47.0, 8, 52, 6, 0.77, 0);
    add_star(app, "Aldebaran", 4, 35, 55.2, 16, 30, 33, 0.85, 0);
    add_star(app, "Antares", 16, 29, 24.4, -26, 25, 55, 0.96, 0);
    add_star(app, "Spica", 13, 25, 11.6, -11, 9, 41, 0.98, 0);
    add_star(app, "Pollux", 7, 45, 18.9, 28, 1, 34, 1.14, 0);
    add_star(app, "Fomalhaut", 22, 57, 39.1, -29, 37, 20, 1.16, 0);
    add_star(app, "Deneb", 20, 41, 25.9, 45, 16, 49, 1.25, 0);
    add_star(app, "Regulus", 10, 8, 22.3, 11, 58, 2, 1.35, 0);

    // Fix Mintaka manually (approx index 9 if we count 0-based from start, but I added UMa 7 stars first)
    // UMa: 0-6. Orion: 7-13. Mintaka is index 10.
    // Mintaka dec is -0 deg 17 min 57 sec.
    app->stars[10].pos.dec = - (0 + 17.0/60.0 + 57.0/3600.0);

    // --- Constellations ---
    app->constellations = malloc(sizeof(Constellation) * 2);
    app->num_constellations = 2;

    // Ursa Major
    app->constellations[0].id = 1;
    strcpy(app->constellations[0].name, "Ursa Major");
    int uma_indices[] = {0,1, 1,2, 2,3, 3,4, 4,5, 5,6, -1};
    memcpy(app->constellations[0].star_indices, uma_indices, sizeof(uma_indices));

    // Orion
    app->constellations[1].id = 2;
    strcpy(app->constellations[1].name, "Orion");
    // Betelgeuse(7), Rigel(8), Bellatrix(9), Mintaka(10), Alnilam(11), Alnitak(12), Saiph(13)
    // Outline: Betelgeuse-Bellatrix, Bellatrix-Rigel? No.
    // Shoulders: Betelgeuse(7) - Bellatrix(9)
    // Feet: Saiph(13) - Rigel(8)
    // Belt: Mintaka(10) - Alnilam(11) - Alnitak(12)
    // Body: Betelgeuse(7) - Alnitak(12), Bellatrix(9) - Mintaka(10)?
    // Usually: Betelgeuse -> Alnitak. Bellatrix -> Mintaka. Mintaka -> Alnilam -> Alnitak. Alnitak -> Saiph. Mintaka -> Rigel.
    // Let's do simple:
    // 7-9 (Top), 13-8 (Bottom) - No, that's not connected.
    // 7 (Betelgeuse) -> 12 (Alnitak)
    // 9 (Bellatrix) -> 10 (Mintaka)
    // 10 -> 11 -> 12 (Belt)
    // 10 -> 8 (Rigel)
    // 12 -> 13 (Saiph)
    // 7 -> 9? Usually yes.
    // 8 -> 13? Usually yes.
    int ori_indices[] = {7,12, 9,10, 10,11, 11,12, 10,8, 12,13, 7,9, 8,13, -1};
    memcpy(app->constellations[1].star_indices, ori_indices, sizeof(ori_indices));
}

void free_data(AppState *app) {
    if (app->stars) free(app->stars);
    if (app->constellations) free(app->constellations);
}
