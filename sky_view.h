#ifndef SKY_VIEW_H
#define SKY_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"

typedef struct {
    gboolean show_constellation_lines;
    gboolean show_constellation_names;
    gboolean show_alt_az_grid;
    gboolean show_ra_dec_grid;
    gboolean show_planets;
    gboolean show_moon_circles;
} SkyViewOptions;

GtkWidget *create_sky_view(Location *loc, DateTime *dt, SkyViewOptions *options, void (*on_sky_click)(double alt, double az));
void sky_view_redraw();

#endif
