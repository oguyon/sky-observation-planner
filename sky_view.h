#ifndef SKY_VIEW_H
#define SKY_VIEW_H

#include <gtk/gtk.h>
#include "sky_model.h"

typedef void (*SelectionChangedCallback)(void *user_data);

GtkWidget *sky_view_new(AppState *app, SelectionChangedCallback callback, void *callback_data);
void sky_view_redraw(GtkWidget *widget);

#endif
