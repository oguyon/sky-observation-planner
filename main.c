#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "catalog.h"
#include "sky_model.h"
#include "sky_view.h"
#include "elevation_view.h"

// Site Definition
typedef struct {
    const char *name;
    double lat;
    double lon;
    double timezone_offset; // Hours from UTC (e.g. -10 for Hawaii)
} Site;

Site sites[] = {
    {"Maunakea Observatories", 19.8207, -155.4681, -10.0},
    {"La Palma (Roque de los Muchachos)", 28.7636, -17.8947, 0.0},
    {"Paranal Observatory", -24.6275, -70.4044, -4.0},
    {"Las Campanas Observatory", -29.0146, -70.6926, -4.0},
    {"New York City", 40.7128, -74.0060, -5.0},
    {NULL, 0, 0, 0}
};

// Global State
Location loc = {19.8207, -155.4681}; // Default Maunakea
DateTime dt; // Initialized in main

SkyViewOptions sky_options = {
    .show_constellation_lines = TRUE,
    .show_constellation_names = FALSE,
    .show_alt_az_grid = FALSE,
    .show_ra_dec_grid = FALSE,
    .show_planets = FALSE,
    .show_moon_circles = FALSE,
    .show_ecliptic = FALSE
};

static void update_all_views() {
    sky_view_redraw();
    elevation_view_redraw();
}

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

// Callback from elevation view
static void on_time_selected_from_plot(DateTime new_dt) {
    dt = new_dt;
    update_all_views();
}

// Toggles
static void on_toggle_constellation_lines(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_constellation_lines = gtk_check_button_get_active(source);
    sky_view_redraw();
}
static void on_toggle_constellation_names(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_constellation_names = gtk_check_button_get_active(source);
    sky_view_redraw();
}
static void on_toggle_alt_az(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_alt_az_grid = gtk_check_button_get_active(source);
    sky_view_redraw();
}
static void on_toggle_ra_dec(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_ra_dec_grid = gtk_check_button_get_active(source);
    sky_view_redraw();
}
static void on_toggle_planets(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_planets = gtk_check_button_get_active(source);
    sky_view_redraw();
}
static void on_toggle_moon_circles(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_moon_circles = gtk_check_button_get_active(source);
    sky_view_redraw();
}

static void on_toggle_ecliptic(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_ecliptic = gtk_check_button_get_active(source);
    sky_view_redraw();
}

static void on_site_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    GtkDropDown *dropdown = GTK_DROP_DOWN(object);
    guint selected = gtk_drop_down_get_selected(dropdown);

    if (selected != GTK_INVALID_LIST_POSITION && sites[selected].name) {
        loc.lat = sites[selected].lat;
        loc.lon = sites[selected].lon;
        dt.timezone_offset = sites[selected].timezone_offset;
        update_all_views();
    }
}

// Calendar day selected
static void on_day_selected(GtkCalendar *calendar, gpointer user_data) {
    GtkLabel *label = GTK_LABEL(user_data);
    GDateTime *date = gtk_calendar_get_date(calendar);
    if (date) {
        dt.year = g_date_time_get_year(date);
        dt.month = g_date_time_get_month(date);
        dt.day = g_date_time_get_day_of_month(date);
        dt.hour = 0; // Midnight Local Time
        dt.minute = 0;
        dt.second = 0;

        char *date_str = g_date_time_format(date, "%Y-%m-%d");
        gtk_label_set_text(label, date_str);
        g_free(date_str);

        g_date_time_unref(date);
        update_all_views();
    }
}

static void activate(GtkApplication *app, gpointer user_data) {
    if (load_catalog() != 0) {
        fprintf(stderr, "Failed to load catalog. Make sure stars.6.json and constellations.lines.json are present.\n");
        return;
    }

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Night Sky Tool");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_window_set_child(GTK_WINDOW(window), paned);

    // Left Panel: Sky View Only
    GtkWidget *sky_area = create_sky_view(&loc, &dt, &sky_options, on_sky_click);
    gtk_widget_set_size_request(sky_area, 500, 500);
    gtk_paned_set_start_child(GTK_PANED(paned), sky_area);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

    // Right Panel: Split Vertical (Elevation Top, Controls Bottom)
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_paned_set_end_child(GTK_PANED(paned), right_box);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    // Elevation Area (Top Half)
    GtkWidget *status_label = gtk_label_new("Hover over graph");
    GtkWidget *elev_area = create_elevation_view(&loc, &dt, GTK_LABEL(status_label), on_time_selected_from_plot);

    gtk_widget_set_vexpand(elev_area, TRUE);
    gtk_widget_set_hexpand(elev_area, TRUE);
    gtk_box_append(GTK_BOX(right_box), gtk_label_new("Elevation (Midnight +/- 8h)"));
    gtk_box_append(GTK_BOX(right_box), elev_area);

    // Controls Box (Bottom Half)
    GtkWidget *controls_frame = gtk_frame_new("Controls");
    gtk_widget_set_vexpand(controls_frame, TRUE);
    gtk_box_append(GTK_BOX(right_box), controls_frame);

    GtkWidget *controls_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(controls_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(controls_grid), 10);
    gtk_widget_set_margin_top(controls_grid, 20);
    gtk_widget_set_margin_bottom(controls_grid, 20);
    gtk_widget_set_margin_start(controls_grid, 20);
    gtk_widget_set_margin_end(controls_grid, 20);
    gtk_frame_set_child(GTK_FRAME(controls_frame), controls_grid);

    // Site Dropdown
    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Site:"), 0, 0, 1, 1);

    GtkStringList *site_list = gtk_string_list_new(NULL);
    for (int i = 0; sites[i].name != NULL; i++) {
        gtk_string_list_append(site_list, sites[i].name);
    }

    GtkWidget *dropdown_site = gtk_drop_down_new(G_LIST_MODEL(site_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown_site), 0); // Default Maunakea
    g_signal_connect(dropdown_site, "notify::selected", G_CALLBACK(on_site_changed), NULL);
    gtk_grid_attach(GTK_GRID(controls_grid), dropdown_site, 1, 0, 1, 1);

    // Date Control (Calendar Popover)
    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Date:"), 0, 1, 1, 1);

    GtkWidget *date_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *lbl_date_value = gtk_label_new("2024-01-01"); // Init value
    GtkWidget *cal_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(cal_button), "Select");

    gtk_box_append(GTK_BOX(date_box), lbl_date_value);
    gtk_box_append(GTK_BOX(date_box), cal_button);

    GtkWidget *popover = gtk_popover_new();
    GtkWidget *calendar = gtk_calendar_new();
    g_signal_connect(calendar, "day-selected", G_CALLBACK(on_day_selected), lbl_date_value);
    gtk_popover_set_child(GTK_POPOVER(popover), calendar);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(cal_button), popover);

    gtk_grid_attach(GTK_GRID(controls_grid), date_box, 1, 1, 1, 1);

    // Toggles
    GtkWidget *toggle_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_grid_attach(GTK_GRID(controls_grid), toggle_box, 0, 2, 2, 1);

    GtkWidget *cb_const_lines = gtk_check_button_new_with_label("Constellation Lines");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_const_lines), sky_options.show_constellation_lines);
    g_signal_connect(cb_const_lines, "toggled", G_CALLBACK(on_toggle_constellation_lines), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_const_lines);

    GtkWidget *cb_const_names = gtk_check_button_new_with_label("Constellation Names");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_const_names), sky_options.show_constellation_names);
    g_signal_connect(cb_const_names, "toggled", G_CALLBACK(on_toggle_constellation_names), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_const_names);

    GtkWidget *cb_alt_az = gtk_check_button_new_with_label("Alt/Az Grid");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_alt_az), sky_options.show_alt_az_grid);
    g_signal_connect(cb_alt_az, "toggled", G_CALLBACK(on_toggle_alt_az), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_alt_az);

    GtkWidget *cb_ra_dec = gtk_check_button_new_with_label("RA/Dec Grid");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_ra_dec), sky_options.show_ra_dec_grid);
    g_signal_connect(cb_ra_dec, "toggled", G_CALLBACK(on_toggle_ra_dec), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_ra_dec);

    GtkWidget *cb_planets = gtk_check_button_new_with_label("Planets");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_planets), sky_options.show_planets);
    g_signal_connect(cb_planets, "toggled", G_CALLBACK(on_toggle_planets), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_planets);

    GtkWidget *cb_moon = gtk_check_button_new_with_label("Moon Circles");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_moon), sky_options.show_moon_circles);
    g_signal_connect(cb_moon, "toggled", G_CALLBACK(on_toggle_moon_circles), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_moon);

    GtkWidget *cb_ecliptic = gtk_check_button_new_with_label("Ecliptic");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_ecliptic), sky_options.show_ecliptic);
    g_signal_connect(cb_ecliptic, "toggled", G_CALLBACK(on_toggle_ecliptic), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_ecliptic);

    // Reset View Button
    GtkWidget *btn_reset = gtk_button_new_with_label("Reset View");
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(sky_view_reset_view), NULL);
    gtk_box_append(GTK_BOX(toggle_box), btn_reset);

    // Status Label
    gtk_grid_attach(GTK_GRID(controls_grid), status_label, 0, 3, 2, 1);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    // Initialize dt to current local time
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    dt.year = tm->tm_year + 1900;
    dt.month = tm->tm_mon + 1;
    dt.day = tm->tm_mday;
    dt.hour = 0; // Midnight local
    dt.minute = 0;
    dt.second = 0;
    dt.timezone_offset = -10.0; // Default Maunakea

    GtkApplication *app;
    int status;

    app = gtk_application_new("org.example.nightsky", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    free_catalog();
    return status;
}
