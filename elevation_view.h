#ifndef ELEVATION_VIEW_H
#define ELEVATION_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"

// Callback type for when a time is clicked on the elevation graph
typedef void (*TimeSelectedCallback)(DateTime new_dt);

GtkWidget *create_elevation_view(Location *loc, DateTime *dt, GtkLabel *status_label, TimeSelectedCallback on_time_selected);
void elevation_view_set_selected(double ra, double dec);
void elevation_view_redraw();
 void elevation_view_set_highlighted_target(int index);

#endif
