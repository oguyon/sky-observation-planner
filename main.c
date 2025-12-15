#include <gtk/gtk.h>
#include <stdlib.h>
#include "catalog.h"
#include "sky_model.h"
#include "sky_view.h"
#include "elevation_view.h"

// Global State
Location loc = {40.7128, -74.0060}; // NYC default
DateTime dt = {2024, 1, 1, 22, 0, 0, -5}; // Default time
gboolean show_constellations = TRUE;

void on_sky_click(double alt, double az) {
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;

    struct ln_hrz_posn hrz;
    hrz.alt = alt;
    hrz.az = az;

    double JD = get_julian_day(dt);
    struct ln_equ_posn equ;
    ln_get_equ_from_hrz(&hrz, &observer, JD, &equ);

    elevation_view_set_selected(equ.ra, equ.dec);
}

static void on_toggle_constellations(GtkToggleButton *source, gpointer user_data) {
    show_constellations = gtk_toggle_button_get_active(source);
    sky_view_redraw();
}

static void on_lat_changed(GtkSpinButton *spin_button, gpointer user_data) {
    loc.lat = gtk_spin_button_get_value(spin_button);
    sky_view_redraw();
    elevation_view_redraw();
}

static void on_lon_changed(GtkSpinButton *spin_button, gpointer user_data) {
    loc.lon = gtk_spin_button_get_value(spin_button);
    sky_view_redraw();
    elevation_view_redraw();
}

static void on_hour_changed(GtkSpinButton *spin_button, gpointer user_data) {
    dt.hour = (int)gtk_spin_button_get_value(spin_button);
    sky_view_redraw();
    elevation_view_redraw();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    if (load_catalog() != 0) {
        fprintf(stderr, "Failed to load catalog. Make sure stars.6.json and constellations.lines.json are present.\n");
        return 1;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Night Sky Tool");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(window), paned);

    // Left Panel: VBox with Controls + SkyView
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_paned_add1(GTK_PANED(paned), left_box);

    // Controls
    GtkWidget *controls_grid = gtk_grid_new();
    gtk_box_pack_start(GTK_BOX(left_box), controls_grid, FALSE, FALSE, 5);

    // Location
    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Lat:"), 0, 0, 1, 1);
    GtkWidget *spin_lat = gtk_spin_button_new_with_range(-90, 90, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_lat), loc.lat);
    g_signal_connect(spin_lat, "value-changed", G_CALLBACK(on_lat_changed), NULL);
    gtk_grid_attach(GTK_GRID(controls_grid), spin_lat, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Lon:"), 2, 0, 1, 1);
    GtkWidget *spin_lon = gtk_spin_button_new_with_range(-180, 180, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_lon), loc.lon);
    g_signal_connect(spin_lon, "value-changed", G_CALLBACK(on_lon_changed), NULL);
    gtk_grid_attach(GTK_GRID(controls_grid), spin_lon, 3, 0, 1, 1);

    // Time (Hour only for simplicity)
    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Hour:"), 4, 0, 1, 1);
    GtkWidget *spin_hour = gtk_spin_button_new_with_range(0, 23, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_hour), dt.hour);
    g_signal_connect(spin_hour, "value-changed", G_CALLBACK(on_hour_changed), NULL);
    gtk_grid_attach(GTK_GRID(controls_grid), spin_hour, 5, 0, 1, 1);

    // Toggle
    GtkWidget *check_const = gtk_check_button_new_with_label("Constellations");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_const), show_constellations);
    g_signal_connect(check_const, "toggled", G_CALLBACK(on_toggle_constellations), NULL);
    gtk_grid_attach(GTK_GRID(controls_grid), check_const, 0, 1, 2, 1);

    // Sky View
    GtkWidget *sky_area = create_sky_view(&loc, &dt, &show_constellations, on_sky_click);
    gtk_box_pack_start(GTK_BOX(left_box), sky_area, TRUE, TRUE, 0);

    // Right Panel: Elevation Graph
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_paned_add2(GTK_PANED(paned), right_box);

    gtk_box_pack_start(GTK_BOX(right_box), gtk_label_new("Elevation (17:00 - 07:00)"), FALSE, FALSE, 5);
    GtkWidget *elev_area = create_elevation_view(&loc, &dt);
    gtk_box_pack_start(GTK_BOX(right_box), elev_area, TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    free_catalog();
    return 0;
}
