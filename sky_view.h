#ifndef SKY_VIEW_H
#define SKY_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"
#include "target_list.h"

typedef struct {
    gboolean show_constellation_lines;
    gboolean show_constellation_names;
    gboolean show_alt_az_grid;
    gboolean show_ra_dec_grid;
    gboolean show_planets;
    gboolean show_moon_circles;
    gboolean show_ecliptic;
    double star_mag_limit;
    double star_size_m0;
    double star_size_ma;
    gboolean show_star_colors;
    double star_saturation;
    gboolean auto_star_settings;
    double font_scale;
    gboolean ephemeris_use_ut;
} SkyViewOptions;

GtkWidget *create_sky_view(Location *loc, DateTime *dt, SkyViewOptions *options, void (*on_sky_click)(double alt, double az));
void sky_view_redraw();
void sky_view_reset_view();
void sky_view_toggle_projection();
void sky_view_set_highlighted_target(Target *target);
void sky_view_set_hover_state(int active, DateTime time, double elev);
double sky_view_get_zoom();

#endif
