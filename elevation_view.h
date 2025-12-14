#ifndef ELEVATION_VIEW_H
#define ELEVATION_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"

GtkWidget *elevation_view_new(AppState *app);
void elevation_view_redraw(GtkWidget *widget);

#endif
