#ifndef ELEVATION_VIEW_H
#define ELEVATION_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"

GtkWidget *create_elevation_view(Location *loc, DateTime *dt);
void elevation_view_set_selected(double ra, double dec);
void elevation_view_redraw();

#endif
