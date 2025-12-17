#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "catalog.h"
#include "sky_model.h"
#include "sky_view.h"
#include "elevation_view.h"
#include "source_selection.h"
#include "target_list.h"

// Site Definition
typedef struct {
    const char *name;
    double lat;
    double lon;
    double timezone_offset;
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
Location loc = {19.8207, -155.4681};
DateTime dt;

SkyViewOptions sky_options = {
    .show_constellation_lines = TRUE,
    .show_constellation_names = FALSE,
    .show_alt_az_grid = FALSE,
    .show_ra_dec_grid = FALSE,
    .show_planets = FALSE,
    .show_moon_circles = FALSE,
    .show_ecliptic = FALSE,
    .star_mag_limit = 6.0,
    .star_size_m0 = 8.0,
    .star_size_ma = 1.0
};

// UI Widgets for Target List
static GtkColumnView *target_list_view = NULL;

static void update_all_views() {
    sky_view_redraw();
    elevation_view_redraw();
}

static void on_target_selection_changed(GtkSelectionModel *model, guint position, guint n_items, gpointer user_data) {
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint selected = gtk_single_selection_get_selected(sel);

    int index = -1;
    if (selected != GTK_INVALID_LIST_POSITION) {
        index = (int)selected;
    }

    sky_view_set_highlighted_target(index);
    elevation_view_set_highlighted_target(index);
}

static void refresh_target_list_ui() {
    if (!target_list_view) return;

    // Rebuild the model.
    int cnt = target_list_get_count();
    const char **items = malloc(sizeof(char*) * (cnt + 1));
    for (int i=0; i<cnt; i++) {
        Target *t = target_list_get(i);
        char *buf = malloc(128);
        snprintf(buf, 128, "%s (RA:%.2f, Dec:%.2f)", t->name, t->ra, t->dec);
        items[i] = buf;
    }
    items[cnt] = NULL;

    GtkStringList *new_model = gtk_string_list_new(items);
    GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(new_model));
    g_signal_connect(sel, "selection-changed", G_CALLBACK(on_target_selection_changed), NULL);

    gtk_column_view_set_model(target_list_view, GTK_SELECTION_MODEL(sel));

    for (int i=0; i<cnt; i++) free((char*)items[i]);
    free(items);
}

// Callback from target_list module
static void on_target_list_changed() {
    refresh_target_list_ui();
    update_all_views();
}

static void on_delete_target_clicked(GtkButton *btn, gpointer user_data) {
    if (!target_list_view) return;
    GtkSelectionModel *model = gtk_column_view_get_model(target_list_view);
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint pos = gtk_single_selection_get_selected(sel);

    if (pos != GTK_INVALID_LIST_POSITION) {
        target_list_remove(pos);
        // refresh_target_list_ui will be called via callback
    }
}

static void target_list_setup_cb(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_label_new(NULL);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_item_set_child(list_item, label);
}

static void target_list_bind_cb(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    GtkStringObject *strobj = gtk_list_item_get_item(list_item);
    gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(strobj));
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

    show_source_selection_dialog(NULL, equ.ra, equ.dec, &loc, &dt);
}

static void on_time_selected_from_plot(DateTime new_dt) {
    dt = new_dt;
    update_all_views();
}

// Toggles (Simplified helper?) No, keep explicit for GCallback casting
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

static void on_day_selected(GtkCalendar *calendar, gpointer user_data) {
    GtkLabel *label = GTK_LABEL(user_data);
    GDateTime *date = gtk_calendar_get_date(calendar);
    if (date) {
        dt.year = g_date_time_get_year(date);
        dt.month = g_date_time_get_month(date);
        dt.day = g_date_time_get_day_of_month(date);
        dt.hour = 0;
        dt.minute = 0;
        dt.second = 0;

        char *date_str = g_date_time_format(date, "%Y-%m-%d");
        gtk_label_set_text(label, date_str);
        g_free(date_str);

        g_date_time_unref(date);
        update_all_views();
    }
}

static void on_mag_limit_changed(GtkRange *range, gpointer user_data) {
    sky_options.star_mag_limit = gtk_range_get_value(range);
    sky_view_redraw();
}

static void on_m0_changed(GtkRange *range, gpointer user_data) {
    sky_options.star_size_m0 = gtk_range_get_value(range);
    sky_view_redraw();
}

static void on_ma_changed(GtkRange *range, gpointer user_data) {
    sky_options.star_size_ma = gtk_range_get_value(range);
    sky_view_redraw();
}

static void activate(GtkApplication *app, gpointer user_data) {
    if (load_catalog() != 0) {
        fprintf(stderr, "Failed to load catalog.\n");
        return;
    }

    // Register callback
    target_list_set_change_callback(on_target_list_changed);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Night Sky Tool");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 700);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_window_set_child(GTK_WINDOW(window), paned);

    // Left Panel: Sky View
    GtkWidget *sky_area = create_sky_view(&loc, &dt, &sky_options, on_sky_click);
    gtk_widget_set_size_request(sky_area, 500, 500);
    gtk_paned_set_start_child(GTK_PANED(paned), sky_area);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

    // Right Panel: Split Vertical (Elevation Top, Bottom Area)
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

    // Bottom Area: Split Horizontal (Controls Left, Target List Right)
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_vexpand(bottom_box, TRUE);
    gtk_box_append(GTK_BOX(right_box), bottom_box);

    // Controls Frame (Left Side of Bottom)
    GtkWidget *controls_frame = gtk_frame_new("Controls");
    gtk_widget_set_hexpand(controls_frame, TRUE);
    gtk_box_append(GTK_BOX(bottom_box), controls_frame);

    GtkWidget *controls_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(controls_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(controls_grid), 10);
    gtk_widget_set_margin_top(controls_grid, 10);
    gtk_widget_set_margin_bottom(controls_grid, 10);
    gtk_widget_set_margin_start(controls_grid, 10);
    gtk_widget_set_margin_end(controls_grid, 10);
    gtk_frame_set_child(GTK_FRAME(controls_frame), controls_grid);

    // Site Dropdown
    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Site:"), 0, 0, 1, 1);
    GtkStringList *site_list = gtk_string_list_new(NULL);
    for (int i = 0; sites[i].name != NULL; i++) {
        gtk_string_list_append(site_list, sites[i].name);
    }
    GtkWidget *dropdown_site = gtk_drop_down_new(G_LIST_MODEL(site_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown_site), 0);
    g_signal_connect(dropdown_site, "notify::selected", G_CALLBACK(on_site_changed), NULL);
    gtk_grid_attach(GTK_GRID(controls_grid), dropdown_site, 1, 0, 1, 1);

    // Date Control
    gtk_grid_attach(GTK_GRID(controls_grid), gtk_label_new("Date:"), 0, 1, 1, 1);
    GtkWidget *date_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    char date_buf[32];
    sprintf(date_buf, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    GtkWidget *lbl_date_value = gtk_label_new(date_buf);
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
    GtkWidget *toggle_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_grid_attach(GTK_GRID(controls_grid), toggle_box, 0, 2, 2, 1);

    GtkWidget *cb_lines = gtk_check_button_new_with_label("Constellation Lines");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_lines), sky_options.show_constellation_lines);
    g_signal_connect(cb_lines, "toggled", G_CALLBACK(on_toggle_constellation_lines), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_lines);

    GtkWidget *cb_names = gtk_check_button_new_with_label("Constellation Names");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_names), sky_options.show_constellation_names);
    g_signal_connect(cb_names, "toggled", G_CALLBACK(on_toggle_constellation_names), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_names);

    GtkWidget *cb_alt = gtk_check_button_new_with_label("Alt/Az Grid");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_alt), sky_options.show_alt_az_grid);
    g_signal_connect(cb_alt, "toggled", G_CALLBACK(on_toggle_alt_az), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_alt);

    GtkWidget *cb_ra = gtk_check_button_new_with_label("RA/Dec Grid");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_ra), sky_options.show_ra_dec_grid);
    g_signal_connect(cb_ra, "toggled", G_CALLBACK(on_toggle_ra_dec), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_ra);

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

    GtkWidget *btn_reset = gtk_button_new_with_label("Reset View");
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(sky_view_reset_view), NULL);
    gtk_box_append(GTK_BOX(toggle_box), btn_reset);

    // Star Settings
    GtkWidget *star_expander = gtk_expander_new("Star Settings");
    gtk_grid_attach(GTK_GRID(controls_grid), star_expander, 0, 3, 2, 1);

    GtkWidget *star_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_expander_set_child(GTK_EXPANDER(star_expander), star_box);

    // Mag Limit
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Mag Limit:"));
    GtkWidget *scale_limit = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 15.0, 0.1);
    gtk_range_set_value(GTK_RANGE(scale_limit), sky_options.star_mag_limit);
    g_signal_connect(scale_limit, "value-changed", G_CALLBACK(on_mag_limit_changed), NULL);
    gtk_box_append(GTK_BOX(star_box), scale_limit);

    // M0
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Spot Size M0:"));
    GtkWidget *scale_m0 = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 20.0, 0.1);
    gtk_range_set_value(GTK_RANGE(scale_m0), sky_options.star_size_m0);
    g_signal_connect(scale_m0, "value-changed", G_CALLBACK(on_m0_changed), NULL);
    gtk_box_append(GTK_BOX(star_box), scale_m0);

    // MA
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Spot Size MA:"));
    GtkWidget *scale_ma = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    gtk_range_set_value(GTK_RANGE(scale_ma), sky_options.star_size_ma);
    g_signal_connect(scale_ma, "value-changed", G_CALLBACK(on_ma_changed), NULL);
    gtk_box_append(GTK_BOX(star_box), scale_ma);

    // Status Label
    gtk_grid_attach(GTK_GRID(controls_grid), status_label, 0, 4, 2, 1);

    // Target List Frame (Right Side of Bottom)
    GtkWidget *targets_frame = gtk_frame_new("Targets");
    gtk_widget_set_hexpand(targets_frame, TRUE);
    gtk_box_append(GTK_BOX(bottom_box), targets_frame);

    GtkWidget *target_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(target_box, 5);
    gtk_widget_set_margin_bottom(target_box, 5);
    gtk_widget_set_margin_start(target_box, 5);
    gtk_widget_set_margin_end(target_box, 5);
    gtk_frame_set_child(GTK_FRAME(targets_frame), target_box);

    GtkWidget *scrolled_list = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_list, TRUE);
    gtk_box_append(GTK_BOX(target_box), scrolled_list);

    target_list_view = GTK_COLUMN_VIEW(gtk_column_view_new(NULL));
    gtk_widget_set_vexpand(GTK_WIDGET(target_list_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(target_list_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_list), GTK_WIDGET(target_list_view));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(target_list_setup_cb), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(target_list_bind_cb), NULL);
    GtkColumnViewColumn *col = gtk_column_view_column_new("Target", factory);
    gtk_column_view_append_column(target_list_view, col);

    GtkWidget *btn_del_target = gtk_button_new_with_label("Delete Selected");
    g_signal_connect(btn_del_target, "clicked", G_CALLBACK(on_delete_target_clicked), NULL);
    gtk_box_append(GTK_BOX(target_box), btn_del_target);

    refresh_target_list_ui();

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    dt.year = tm->tm_year + 1900;
    dt.month = tm->tm_mon + 1;
    dt.day = tm->tm_mday;
    dt.hour = 0;
    dt.minute = 0;
    dt.second = 0;
    dt.timezone_offset = -10.0;

    GtkApplication *app;
    int status;

    app = gtk_application_new("org.example.nightsky", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    free_catalog();
    return status;
}
