#ifndef ELEVATION_VIEW_H
#define ELEVATION_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"
#include "target_list.h"

// Callback type for when a time is clicked on the elevation graph
typedef void (*TimeSelectedCallback)(DateTime new_dt);

// Callback type for when the cursor hovers over the elevation graph
typedef void (*ElevationHoverCallback)(int active, DateTime time, double elev);

GtkWidget *create_elevation_view(Location *loc, DateTime *dt, GtkLabel *status_label, TimeSelectedCallback on_time_selected, ElevationHoverCallback on_hover);
void elevation_view_set_selected(double ra, double dec);
void elevation_view_redraw();
void elevation_view_set_highlighted_target(Target *target);

#endif
