#ifndef SOURCE_SELECTION_H
#define SOURCE_SELECTION_H

#include <gtk/gtk.h>
#include "sky_model.h"

void show_source_selection_dialog(GtkWindow *parent, double ra, double dec, Location *loc, DateTime *dt);

#endif
