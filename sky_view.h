#ifndef SKY_VIEW_H
#define SKY_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"

GtkWidget *create_sky_view(Location *loc, DateTime *dt, gboolean *show_constellations, void (*on_sky_click)(double alt, double az));
void sky_view_redraw();

#endif
