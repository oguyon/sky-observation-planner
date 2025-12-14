#include <gtk/gtk.h>
#include <libnova/libnova.h>
#include "sky_model.h"
#include "sky_view.h"
#include "elevation_view.h"

static AppState app_state;
static GtkWidget *sky_widget;
static GtkWidget *elevation_widget;

static void on_constellation_toggled(GtkToggleButton *button, gpointer user_data) {
    app_state.show_constellations = gtk_check_button_get_active(GTK_CHECK_BUTTON(button));
    sky_view_redraw(sky_widget);
}

static void on_time_add_1h(GtkWidget *widget, gpointer user_data) {
    double jd = ln_get_julian_day(&app_state.date);
    jd += 1.0/24.0;
    ln_get_date(jd, &app_state.date);
    sky_view_redraw(sky_widget);
    elevation_view_redraw(elevation_widget);
}

static void on_time_sub_1h(GtkWidget *widget, gpointer user_data) {
    double jd = ln_get_julian_day(&app_state.date);
    jd -= 1.0/24.0;
    ln_get_date(jd, &app_state.date);
    sky_view_redraw(sky_widget);
    elevation_view_redraw(elevation_widget);
}

static void on_selection_changed(void *user_data) {
    elevation_view_redraw(elevation_widget);
}

static void on_lat_changed(GtkSpinButton *spin_button, gpointer user_data) {
    app_state.observer_location.lat = gtk_spin_button_get_value(spin_button);
    sky_view_redraw(sky_widget);
    elevation_view_redraw(elevation_widget);
}

static void on_lon_changed(GtkSpinButton *spin_button, gpointer user_data) {
    app_state.observer_location.lng = gtk_spin_button_get_value(spin_button);
    sky_view_redraw(sky_widget);
    elevation_view_redraw(elevation_widget);
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *paned;
    GtkWidget *frame_left, *frame_right;
    GtkWidget *box_right;
    GtkWidget *controls_box;
    GtkWidget *location_box;
    GtkWidget *btn_const;
    GtkWidget *btn_time_p, *btn_time_m;
    GtkWidget *spin_lat, *spin_lon;
    GtkWidget *lbl_lat, *lbl_lon;

    init_data(&app_state);

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Night Sky Tool");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_window_set_child(GTK_WINDOW(window), paned);

    elevation_widget = elevation_view_new(&app_state);

    // Left Panel
    frame_left = gtk_frame_new("Sky Map");
    sky_widget = sky_view_new(&app_state, on_selection_changed, NULL);
    gtk_frame_set_child(GTK_FRAME(frame_left), sky_widget);
    gtk_paned_set_start_child(GTK_PANED(paned), frame_left);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);

    // Right Panel
    frame_right = gtk_frame_new("Analysis");
    box_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_frame_set_child(GTK_FRAME(frame_right), box_right);

    gtk_box_append(GTK_BOX(box_right), elevation_widget);

    // Location Controls
    location_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    lbl_lat = gtk_label_new("Lat:");
    gtk_box_append(GTK_BOX(location_box), lbl_lat);

    spin_lat = gtk_spin_button_new_with_range(-90.0, 90.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_lat), app_state.observer_location.lat);
    g_signal_connect(spin_lat, "value-changed", G_CALLBACK(on_lat_changed), NULL);
    gtk_box_append(GTK_BOX(location_box), spin_lat);

    lbl_lon = gtk_label_new("Lon:");
    gtk_box_append(GTK_BOX(location_box), lbl_lon);

    spin_lon = gtk_spin_button_new_with_range(-180.0, 180.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_lon), app_state.observer_location.lng);
    g_signal_connect(spin_lon, "value-changed", G_CALLBACK(on_lon_changed), NULL);
    gtk_box_append(GTK_BOX(location_box), spin_lon);

    gtk_box_append(GTK_BOX(box_right), location_box);

    // General Controls
    controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    btn_const = gtk_check_button_new_with_label("Constellations");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(btn_const), TRUE);
    g_signal_connect(btn_const, "toggled", G_CALLBACK(on_constellation_toggled), NULL);
    gtk_box_append(GTK_BOX(controls_box), btn_const);

    btn_time_m = gtk_button_new_with_label("-1h");
    g_signal_connect(btn_time_m, "clicked", G_CALLBACK(on_time_sub_1h), NULL);
    gtk_box_append(GTK_BOX(controls_box), btn_time_m);

    btn_time_p = gtk_button_new_with_label("+1h");
    g_signal_connect(btn_time_p, "clicked", G_CALLBACK(on_time_add_1h), NULL);
    gtk_box_append(GTK_BOX(controls_box), btn_time_p);

    gtk_box_append(GTK_BOX(box_right), controls_box);

    gtk_paned_set_end_child(GTK_PANED(paned), frame_right);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 600);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.example.nightsky", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    free_data(&app_state);

    return status;
}
