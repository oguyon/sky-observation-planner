#include <gtk/gtk.h>
#include <gdk/gdk.h>
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
    .star_mag_limit = 8.0, // Default requested
    .star_size_m0 = 7.0, // Default requested
    .star_size_ma = 0.4, // Default requested
    .show_star_colors = FALSE,
    .star_saturation = 1.0,
    .auto_star_settings = TRUE // Default requested
};

// UI Widgets for Target List
static GtkNotebook *target_notebook = NULL;
static TargetList *active_target_list = NULL;

// UI Widgets for Star Settings (needed for auto update?)
static GtkRange *range_mag = NULL;
static GtkRange *range_m0 = NULL;
static GtkRange *range_ma = NULL;
static GtkRange *range_sat = NULL;

static void update_all_views() {
    sky_view_redraw();
    elevation_view_redraw();
}

TargetList *get_active_target_list() {
    return active_target_list;
}

static void on_target_selection_changed(GtkSelectionModel *model, guint position, guint n_items, gpointer user_data) {
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint selected = gtk_single_selection_get_selected(sel);

    Target *target = NULL;
    if (selected != GTK_INVALID_LIST_POSITION && active_target_list) {
        target = target_list_get_target(active_target_list, (int)selected);
    }

    sky_view_set_highlighted_target(target);
    elevation_view_set_highlighted_target(target);
}

// Callback from target_list module
static void on_target_list_changed() {
    update_all_views();
    // We also need to refresh the current tab's list view, or all of them.
    // Ideally we find the widget associated with the list and update it.
    // For simplicity, we can just rebuild the current page's model if we can find it.

    int pages = gtk_notebook_get_n_pages(target_notebook);
    for (int i=0; i<pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(target_notebook, i);
        // The page child is a GtkScrolledWindow containing a GtkColumnView
        GtkWidget *scrolled = gtk_widget_get_first_child(page); // Box -> Scrolled
        // Wait, structure: Page is a Box. Box has ScrolledWindow.
        // Let's rely on standard widget traversal or store references.
        // To keep it simple: we can store the TargetList pointer in the page using g_object_set_data.

        TargetList *tl = g_object_get_data(G_OBJECT(page), "target_list");
        if (tl) {
            // Find the column view
            // Structure: Box -> ScrolledWindow -> ColumnView
            GtkWidget *sc = gtk_widget_get_first_child(page);
            while (sc && !GTK_IS_SCROLLED_WINDOW(sc)) {
                 sc = gtk_widget_get_next_sibling(sc);
            }
            if (!sc) continue;

            // GTK4 structure is ScrolledWindow -> Viewport -> ColumnView usually, or direct child?
            // gtk_scrolled_window_get_child() returns the child.
            GtkWidget *col_view = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(sc));

            if (GTK_IS_COLUMN_VIEW(col_view)) {
                // Rebuild model
                int cnt = target_list_get_count(tl);
                const char **items = malloc(sizeof(char*) * (cnt + 1));
                for (int k=0; k<cnt; k++) {
                    Target *t = target_list_get_target(tl, k);
                    char *buf = malloc(128);
                    snprintf(buf, 128, "%s (RA:%.2f, Dec:%.2f)", t->name, t->ra, t->dec);
                    items[k] = buf;
                }
                items[cnt] = NULL;

                GtkStringList *new_model = gtk_string_list_new(items);
                GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(new_model));
                g_signal_connect(sel, "selection-changed", G_CALLBACK(on_target_selection_changed), NULL);

                gtk_column_view_set_model(GTK_COLUMN_VIEW(col_view), GTK_SELECTION_MODEL(sel));

                for (int k=0; k<cnt; k++) free((char*)items[k]);
                free(items);
            }
        }
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

// Helper to create a view for a list
static GtkWidget *create_view_for_list(TargetList *list) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(box), "target_list", list); // Store reference

    GtkWidget *scrolled_list = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_list, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled_list);

    GtkColumnView *col_view = GTK_COLUMN_VIEW(gtk_column_view_new(NULL));
    gtk_widget_set_vexpand(GTK_WIDGET(col_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(col_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_list), GTK_WIDGET(col_view));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(target_list_setup_cb), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(target_list_bind_cb), NULL);
    GtkColumnViewColumn *col = gtk_column_view_column_new("Target", factory);
    gtk_column_view_append_column(col_view, col);

    return box;
}

static void refresh_tabs() {
    // Rebuild tabs? Or just add missing ones?
    // Simplest: Remove all, add all.
    int n_pages = gtk_notebook_get_n_pages(target_notebook);
    for (int i=n_pages-1; i>=0; i--) {
        gtk_notebook_remove_page(target_notebook, i);
    }

    int count = target_list_get_list_count();
    for (int i=0; i<count; i++) {
        TargetList *tl = target_list_get_list_by_index(i);
        GtkWidget *page = create_view_for_list(tl);
        gtk_notebook_append_page(target_notebook, page, gtk_label_new(target_list_get_name(tl)));
    }
    on_target_list_changed(); // Populate data

    // Restore active list if possible
    if (count > 0) {
         if (active_target_list) {
             // Find index
             for(int i=0; i<count; i++) {
                 if(target_list_get_list_by_index(i) == active_target_list) {
                     gtk_notebook_set_current_page(target_notebook, i);
                     break;
                 }
             }
         } else {
             active_target_list = target_list_get_list_by_index(0);
         }
    } else {
        active_target_list = NULL;
    }
}

static void on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data) {
    TargetList *tl = g_object_get_data(G_OBJECT(page), "target_list");
    active_target_list = tl;
}

static void on_new_list_create_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *entry = GTK_WIDGET(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text && strlen(text) > 0) {
        target_list_create(text);
        refresh_tabs();
        // Switch to new tab
        int count = target_list_get_list_count();
        gtk_notebook_set_current_page(target_notebook, count-1);
    }
    GtkWidget *window = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(btn)));
    gtk_window_destroy(GTK_WINDOW(window));
}

static void on_new_list_cancel_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *window = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(btn)));
    gtk_window_destroy(GTK_WINDOW(window));
}

static void on_new_list_clicked(GtkButton *btn, gpointer user_data) {
    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "New Target List");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Name:"));
    GtkWidget *entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(hbox), entry);

    GtkWidget *bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(bbox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), bbox);

    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_new_list_cancel_clicked), NULL);
    gtk_box_append(GTK_BOX(bbox), btn_cancel);

    GtkWidget *btn_create = gtk_button_new_with_label("Create");
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_new_list_create_clicked), entry);
    gtk_box_append(GTK_BOX(bbox), btn_create);

    gtk_window_present(GTK_WINDOW(window));
}

// Delete Selected Target
static void on_delete_target_clicked(GtkButton *btn, gpointer user_data) {
    if (!active_target_list) return;

    GtkWidget *page = gtk_notebook_get_nth_page(target_notebook, gtk_notebook_get_current_page(target_notebook));
    GtkWidget *scrolled = gtk_widget_get_first_child(page);
    while (scrolled && !GTK_IS_SCROLLED_WINDOW(scrolled)) scrolled = gtk_widget_get_next_sibling(scrolled);
    if (!scrolled) return;

    GtkWidget *col_view = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scrolled));
    GtkSelectionModel *model = gtk_column_view_get_model(GTK_COLUMN_VIEW(col_view));
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint pos = gtk_single_selection_get_selected(sel);

    if (pos != GTK_INVALID_LIST_POSITION) {
        target_list_remove_target(active_target_list, pos);
    }
}

// Save/Load
static void on_save_finish(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish(dialog, res, &error);

    if (file) {
        char *filename = g_file_get_path(file);
        if (active_target_list && filename) {
            target_list_save(active_target_list, filename);
        }
        g_free(filename);
        g_object_unref(file);
    } else {
        if (error) g_error_free(error);
    }
    g_object_unref(dialog);
}

static void on_save_list_clicked(GtkButton *btn, gpointer user_data) {
    if (!active_target_list) return;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save Target List");
    gtk_file_dialog_save(dialog, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))), NULL, on_save_finish, NULL);
}

static void on_load_finish(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, res, &error);

    if (file) {
        char *filename = g_file_get_path(file);
        if (filename) {
            TargetList *tl = target_list_load(filename);
            if (tl) {
                refresh_tabs();
                // Switch to new list
                int count = target_list_get_list_count();
                gtk_notebook_set_current_page(target_notebook, count-1);
            }
        }
        g_free(filename);
        g_object_unref(file);
    } else {
        if (error) g_error_free(error);
    }
    g_object_unref(dialog);
}

static void on_load_list_clicked(GtkButton *btn, gpointer user_data) {
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Load Target List");
    gtk_file_dialog_open(dialog, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))), NULL, on_load_finish, NULL);
}

// Copy/Paste
static void on_copy_targets_clicked(GtkButton *btn, gpointer user_data) {
    if (!active_target_list) return;

    // For now, copy selected target only? Or allow multi-selection?
    // User asked "copy and past of targets". Let's assume single selection first as we used GtkSingleSelection.
    // If we want multi, we need GtkMultiSelection.
    // Sticking to single for now based on current code structure, or try to upgrade to multi?
    // Code uses GtkSingleSelection. Let's stick to single copy.

    GtkWidget *page = gtk_notebook_get_nth_page(target_notebook, gtk_notebook_get_current_page(target_notebook));
    GtkWidget *scrolled = gtk_widget_get_first_child(page);
    while (scrolled && !GTK_IS_SCROLLED_WINDOW(scrolled)) scrolled = gtk_widget_get_next_sibling(scrolled);

    GtkWidget *col_view = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scrolled));
    GtkSelectionModel *model = gtk_column_view_get_model(GTK_COLUMN_VIEW(col_view));
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint pos = gtk_single_selection_get_selected(sel);

    if (pos != GTK_INVALID_LIST_POSITION) {
        int idx = (int)pos;
        char *data = target_list_serialize_targets(active_target_list, &idx, 1);
        if (data) {
            GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(btn));
            gdk_clipboard_set_text(clipboard, data);
            free(data);
        }
    }
}

static void on_paste_received(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GdkClipboard *clipboard = GDK_CLIPBOARD(source_object);
    char *text = gdk_clipboard_read_text_finish(clipboard, res, NULL);
    if (text) {
        if (active_target_list) {
            target_list_deserialize_and_add(active_target_list, text);
        }
        g_free(text);
    }
}

static void on_paste_targets_clicked(GtkButton *btn, gpointer user_data) {
    if (!active_target_list) return;
    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(btn));
    gdk_clipboard_read_text_async(clipboard, NULL, on_paste_received, NULL);
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

    show_source_selection_dialog(NULL, equ.ra, equ.dec, &loc, &dt, get_active_target_list());
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

static void on_toggle_star_colors(GtkCheckButton *source, gpointer user_data) {
    sky_options.show_star_colors = gtk_check_button_get_active(source);
    sky_view_redraw();
}

static void on_toggle_auto_star_settings(GtkCheckButton *source, gpointer user_data) {
    sky_options.auto_star_settings = gtk_check_button_get_active(source);
    // Enable/Disable sliders?
    gboolean active = !sky_options.auto_star_settings;
    if (range_mag) gtk_widget_set_sensitive(GTK_WIDGET(range_mag), active);
    if (range_m0) gtk_widget_set_sensitive(GTK_WIDGET(range_m0), active);
    if (range_ma) gtk_widget_set_sensitive(GTK_WIDGET(range_ma), active);
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

static void on_saturation_changed(GtkRange *range, gpointer user_data) {
    sky_options.star_saturation = gtk_range_get_value(range);
    sky_view_redraw();
}

static void activate(GtkApplication *app, gpointer user_data) {
    if (load_catalog() != 0) {
        fprintf(stderr, "Failed to load catalog.\n");
        return;
    }

    target_list_init();
    active_target_list = target_list_create("Default");

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
    // We want the divider to be draggable between Sky View and (Elevation + Controls).
    // That's what we did with paned.
    // Now, inside the Right Panel, we have Elevation on top and Controls/Lists on bottom.
    // The user requested: "Make the delimiter between control panel and target list draggable/movable."
    // Ah, wait. "Left panel: full night sky view... Right panel: Elevation..."
    // "Make the delimiter between control panel and target list draggable/movable."
    // Originally, Elevation was Top Right, and Bottom Right was split horizontally into Controls (Left) and Target List (Right).
    // If the delimiter between Control Panel and Target List should be draggable, then the Bottom Right container should be a GtkPaned (Horizontal).

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
    // Using GtkPaned here for draggability.
    GtkWidget *bottom_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(bottom_paned, TRUE);
    gtk_box_append(GTK_BOX(right_box), bottom_paned);

    // Controls Frame (Left Side of Bottom)
    GtkWidget *controls_frame = gtk_frame_new("Controls");
    gtk_widget_set_hexpand(controls_frame, FALSE); // Don't greedily expand, let paned decide
    gtk_paned_set_start_child(GTK_PANED(bottom_paned), controls_frame);
    gtk_paned_set_resize_start_child(GTK_PANED(bottom_paned), FALSE); // Controls usually fixed size? Or allow resize. Allow.

    GtkWidget *controls_scroll = gtk_scrolled_window_new();
    gtk_frame_set_child(GTK_FRAME(controls_frame), controls_scroll);

    GtkWidget *controls_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(controls_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(controls_grid), 10);
    gtk_widget_set_margin_top(controls_grid, 10);
    gtk_widget_set_margin_bottom(controls_grid, 10);
    gtk_widget_set_margin_start(controls_grid, 10);
    gtk_widget_set_margin_end(controls_grid, 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(controls_scroll), controls_grid);

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

    GtkWidget *cb_colors = gtk_check_button_new_with_label("Star Colors");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_colors), sky_options.show_star_colors);
    g_signal_connect(cb_colors, "toggled", G_CALLBACK(on_toggle_star_colors), NULL);
    gtk_box_append(GTK_BOX(toggle_box), cb_colors);

    GtkWidget *btn_reset = gtk_button_new_with_label("Reset View");
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(sky_view_reset_view), NULL);
    gtk_box_append(GTK_BOX(toggle_box), btn_reset);

    // Star Settings
    GtkWidget *star_expander = gtk_expander_new("Star Settings");
    gtk_grid_attach(GTK_GRID(controls_grid), star_expander, 0, 3, 2, 1);

    GtkWidget *star_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_expander_set_child(GTK_EXPANDER(star_expander), star_box);

    // Auto Star Settings Toggle
    GtkWidget *cb_auto = gtk_check_button_new_with_label("Auto Star Settings");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_auto), sky_options.auto_star_settings);
    g_signal_connect(cb_auto, "toggled", G_CALLBACK(on_toggle_auto_star_settings), NULL);
    gtk_box_append(GTK_BOX(star_box), cb_auto);

    // Mag Limit
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Mag Limit:"));
    GtkWidget *scale_limit = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 15.0, 0.1);
    range_mag = GTK_RANGE(scale_limit);
    gtk_scale_set_draw_value(GTK_SCALE(scale_limit), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale_limit), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(scale_limit), sky_options.star_mag_limit);
    g_signal_connect(scale_limit, "value-changed", G_CALLBACK(on_mag_limit_changed), NULL);
    gtk_widget_set_sensitive(scale_limit, !sky_options.auto_star_settings);
    gtk_box_append(GTK_BOX(star_box), scale_limit);

    // M0
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Spot Size M0:"));
    GtkWidget *scale_m0 = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 20.0, 0.1);
    range_m0 = GTK_RANGE(scale_m0);
    gtk_scale_set_draw_value(GTK_SCALE(scale_m0), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale_m0), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(scale_m0), sky_options.star_size_m0);
    g_signal_connect(scale_m0, "value-changed", G_CALLBACK(on_m0_changed), NULL);
    gtk_widget_set_sensitive(scale_m0, !sky_options.auto_star_settings);
    gtk_box_append(GTK_BOX(star_box), scale_m0);

    // MA
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Spot Size MA:"));
    GtkWidget *scale_ma = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 5.0, 0.1);
    range_ma = GTK_RANGE(scale_ma);
    gtk_scale_set_draw_value(GTK_SCALE(scale_ma), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale_ma), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(scale_ma), sky_options.star_size_ma);
    g_signal_connect(scale_ma, "value-changed", G_CALLBACK(on_ma_changed), NULL);
    gtk_widget_set_sensitive(scale_ma, !sky_options.auto_star_settings);
    gtk_box_append(GTK_BOX(star_box), scale_ma);

    // Saturation
    gtk_box_append(GTK_BOX(star_box), gtk_label_new("Color Saturation:"));
    GtkWidget *scale_sat = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 3.0, 0.1);
    range_sat = GTK_RANGE(scale_sat);
    gtk_scale_set_draw_value(GTK_SCALE(scale_sat), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale_sat), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(scale_sat), sky_options.star_saturation);
    g_signal_connect(scale_sat, "value-changed", G_CALLBACK(on_saturation_changed), NULL);
    gtk_box_append(GTK_BOX(star_box), scale_sat);

    // Status Label
    gtk_grid_attach(GTK_GRID(controls_grid), status_label, 0, 4, 2, 1);

    // Target List Frame (Right Side of Bottom)
    GtkWidget *targets_frame = gtk_frame_new("Targets");
    gtk_widget_set_hexpand(targets_frame, TRUE);
    gtk_paned_set_end_child(GTK_PANED(bottom_paned), targets_frame);
    gtk_paned_set_resize_end_child(GTK_PANED(bottom_paned), TRUE);

    GtkWidget *target_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(target_box, 5);
    gtk_widget_set_margin_bottom(target_box, 5);
    gtk_widget_set_margin_start(target_box, 5);
    gtk_widget_set_margin_end(target_box, 5);
    gtk_frame_set_child(GTK_FRAME(targets_frame), target_box);

    // Target List Toolbar
    GtkWidget *tb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(target_box), tb_box);

    GtkWidget *btn_new_list = gtk_button_new_with_label("New List");
    g_signal_connect(btn_new_list, "clicked", G_CALLBACK(on_new_list_clicked), NULL);
    gtk_box_append(GTK_BOX(tb_box), btn_new_list);

    GtkWidget *btn_save_list = gtk_button_new_with_label("Save");
    g_signal_connect(btn_save_list, "clicked", G_CALLBACK(on_save_list_clicked), NULL);
    gtk_box_append(GTK_BOX(tb_box), btn_save_list);

    GtkWidget *btn_load_list = gtk_button_new_with_label("Load");
    g_signal_connect(btn_load_list, "clicked", G_CALLBACK(on_load_list_clicked), NULL);
    gtk_box_append(GTK_BOX(tb_box), btn_load_list);

    GtkWidget *btn_copy = gtk_button_new_with_label("Copy");
    g_signal_connect(btn_copy, "clicked", G_CALLBACK(on_copy_targets_clicked), NULL);
    gtk_box_append(GTK_BOX(tb_box), btn_copy);

    GtkWidget *btn_paste = gtk_button_new_with_label("Paste");
    g_signal_connect(btn_paste, "clicked", G_CALLBACK(on_paste_targets_clicked), NULL);
    gtk_box_append(GTK_BOX(tb_box), btn_paste);

    target_notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_tab_pos(target_notebook, GTK_POS_TOP);
    gtk_widget_set_vexpand(GTK_WIDGET(target_notebook), TRUE);
    g_signal_connect(target_notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);
    gtk_box_append(GTK_BOX(target_box), GTK_WIDGET(target_notebook));

    GtkWidget *btn_del_target = gtk_button_new_with_label("Delete Selected Target");
    g_signal_connect(btn_del_target, "clicked", G_CALLBACK(on_delete_target_clicked), NULL);
    gtk_box_append(GTK_BOX(target_box), btn_del_target);

    refresh_tabs();

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

    target_list_cleanup();
    free_catalog();
    return status;
}
