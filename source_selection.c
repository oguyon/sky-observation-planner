#include "source_selection.h"
#include "catalog.h"
#include "target_list.h"
#include "sky_view.h"
#include "elevation_view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libnova/mercury.h>
#include <libnova/venus.h>
#include <libnova/mars.h>
#include <libnova/jupiter.h>
#include <libnova/saturn.h>
#include <libnova/uranus.h>
#include <libnova/neptune.h>
#include <libnova/solar.h>
#include <libnova/lunar.h>
#include <libnova/angular_separation.h>

// Dialog State
static double center_ra, center_dec;
static Location *dlg_loc;
static DateTime *dlg_dt;
static GtkWidget *plot_area;
static GtkStringList *source_list_model;
static double search_fov = 10.0;

typedef struct {
    char name[64];
    double ra;
    double dec;
    double mag;
    double dist;
} Candidate;

static Candidate *candidates = NULL;
static int candidate_count = 0;

static double get_planet_mag(PlanetID p, double jd) {
    switch(p) {
        case PLANET_MERCURY: return ln_get_mercury_magnitude(jd);
        case PLANET_VENUS:   return ln_get_venus_magnitude(jd);
        case PLANET_MARS:    return ln_get_mars_magnitude(jd);
        case PLANET_JUPITER: return ln_get_jupiter_magnitude(jd);
        case PLANET_SATURN:  return ln_get_saturn_magnitude(jd);
        case PLANET_URANUS:  return ln_get_uranus_magnitude(jd);
        case PLANET_NEPTUNE: return ln_get_neptune_magnitude(jd);
        default: return 0.0;
    }
}

static void get_planet_equ(PlanetID p, double jd, struct ln_equ_posn *pos) {
    switch(p) {
        case PLANET_MERCURY: ln_get_mercury_equ_coords(jd, pos); break;
        case PLANET_VENUS:   ln_get_venus_equ_coords(jd, pos); break;
        case PLANET_MARS:    ln_get_mars_equ_coords(jd, pos); break;
        case PLANET_JUPITER: ln_get_jupiter_equ_coords(jd, pos); break;
        case PLANET_SATURN:  ln_get_saturn_equ_coords(jd, pos); break;
        case PLANET_URANUS:  ln_get_uranus_equ_coords(jd, pos); break;
        case PLANET_NEPTUNE: ln_get_neptune_equ_coords(jd, pos); break;
    }
}

static void update_candidate_list() {
    if (candidates) free(candidates);
    candidates = NULL;
    candidate_count = 0;

    double jd = get_julian_day(*dlg_dt);
    struct ln_equ_posn center_equ = {center_ra, center_dec};

    int max_cand = num_stars + 20;
    candidates = malloc(sizeof(Candidate) * max_cand);

    // Stars
    for (int i=0; i<num_stars; i++) {
        struct ln_equ_posn star_equ = {stars[i].ra, stars[i].dec};
        double dist = ln_get_angular_separation(&center_equ, &star_equ);
        if (dist <= search_fov) {
            candidates[candidate_count].ra = stars[i].ra;
            candidates[candidate_count].dec = stars[i].dec;
            candidates[candidate_count].mag = stars[i].mag;
            candidates[candidate_count].dist = dist;
            snprintf(candidates[candidate_count].name, 64, "Star (Mag %.1f)", stars[i].mag);
            candidate_count++;
        }
    }

    // Planets
    PlanetID p_ids[] = {PLANET_MERCURY, PLANET_VENUS, PLANET_MARS, PLANET_JUPITER, PLANET_SATURN, PLANET_URANUS, PLANET_NEPTUNE};
    const char *p_names[] = {"Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};

    for (int p=0; p<7; p++) {
         struct ln_equ_posn p_equ;
         get_planet_equ(p_ids[p], jd, &p_equ);
         double dist = ln_get_angular_separation(&center_equ, &p_equ);
         if (dist <= search_fov) {
            candidates[candidate_count].ra = p_equ.ra;
            candidates[candidate_count].dec = p_equ.dec;
            candidates[candidate_count].mag = get_planet_mag(p_ids[p], jd);
            candidates[candidate_count].dist = dist;
            strcpy(candidates[candidate_count].name, p_names[p]);
            candidate_count++;
         }
    }

    // Sun
    struct ln_equ_posn sun_equ;
    ln_get_solar_equ_coords(jd, &sun_equ);
    double sun_dist = ln_get_angular_separation(&center_equ, &sun_equ);
    if (sun_dist <= search_fov) {
        candidates[candidate_count].ra = sun_equ.ra;
        candidates[candidate_count].dec = sun_equ.dec;
        candidates[candidate_count].mag = -26.7;
        candidates[candidate_count].dist = sun_dist;
        strcpy(candidates[candidate_count].name, "Sun");
        candidate_count++;
    }

    // Moon
    struct ln_equ_posn moon_equ;
    ln_get_lunar_equ_coords(jd, &moon_equ);
    double moon_dist = ln_get_angular_separation(&center_equ, &moon_equ);
    if (moon_dist <= search_fov) {
        candidates[candidate_count].ra = moon_equ.ra;
        candidates[candidate_count].dec = moon_equ.dec;
        candidates[candidate_count].mag = -12.0; // Approx
        candidates[candidate_count].dist = moon_dist;
        strcpy(candidates[candidate_count].name, "Moon");
        candidate_count++;
    }
}

static void on_plot_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0, 0, 0);

    double min_mag = 100, max_mag = -100;
    for (int i=0; i<candidate_count; i++) {
        if (candidates[i].mag < min_mag) min_mag = candidates[i].mag;
        if (candidates[i].mag > max_mag) max_mag = candidates[i].mag;
    }
    if (min_mag > max_mag) { min_mag = 0; max_mag = 10; }
    // Expand range slightly
    double mag_span = max_mag - min_mag;
    if (mag_span < 1.0) mag_span = 1.0;
    min_mag -= mag_span * 0.1;
    max_mag += mag_span * 0.1;

    double pad = 40;
    double gw = width - 2*pad;
    double gh = height - 2*pad;

    // Axes
    cairo_move_to(cr, pad, pad);
    cairo_line_to(cr, pad, height - pad);
    cairo_line_to(cr, width - pad, height - pad);
    cairo_stroke(cr);

    // Labels
    cairo_move_to(cr, width/2 - 20, height - 5);
    cairo_show_text(cr, "Dist (deg)");

    cairo_save(cr);
    cairo_rotate(cr, -M_PI/2);
    cairo_move_to(cr, -height/2 - 20, 15);
    cairo_show_text(cr, "Magnitude");
    cairo_restore(cr);

    // Plot Points
    // X: 0 to search_fov
    // Y: min_mag (Bright, Top) to max_mag (Dim, Bottom)

    for (int i=0; i<candidate_count; i++) {
        double x = pad + (candidates[i].dist / search_fov) * gw;
        double y = pad + ((candidates[i].mag - min_mag) / (max_mag - min_mag)) * gh;

        cairo_arc(cr, x, y, 3, 0, 2*M_PI);
        cairo_fill(cr);
    }
}

static void setup_cb(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_label_new(NULL);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_item_set_child(list_item, label);
}

static void bind_cb(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    GtkStringObject *strobj = gtk_list_item_get_item(list_item);
    gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(strobj));
}

static void populate_list(GtkColumnView *list) {
    const char **items = malloc(sizeof(char*) * (candidate_count + 1));
    for (int i=0; i<candidate_count; i++) {
        char *buf = malloc(128);
        snprintf(buf, 128, "%s | M:%.1f | D:%.2f", candidates[i].name, candidates[i].mag, candidates[i].dist);
        items[i] = buf;
    }
    items[candidate_count] = NULL;

    source_list_model = gtk_string_list_new(items);

    GtkSingleSelection *selection = gtk_single_selection_new(G_LIST_MODEL(source_list_model));
    gtk_column_view_set_model(list, GTK_SELECTION_MODEL(selection));

    for (int i=0; i<candidate_count; i++) free((char*)items[i]);
    free(items);
}

static void on_search_clicked(GtkButton *btn, gpointer user_data) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(user_data);
    search_fov = gtk_spin_button_get_value(spin);

    GtkColumnView *list = g_object_get_data(G_OBJECT(btn), "list_view");

    update_candidate_list();
    populate_list(list);
    gtk_widget_queue_draw(plot_area);
}

static void on_add_target_clicked(GtkButton *btn, gpointer user_data) {
    GtkColumnView *list = GTK_COLUMN_VIEW(user_data);
    GtkSelectionModel *model = gtk_column_view_get_model(list);
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint pos = gtk_single_selection_get_selected(sel);

    if (pos != GTK_INVALID_LIST_POSITION && pos < candidate_count) {
        target_list_add(candidates[pos].name, candidates[pos].ra, candidates[pos].dec, candidates[pos].mag);
        sky_view_redraw();
        elevation_view_redraw();
    }
}

void show_source_selection_dialog(GtkWindow *parent, double ra, double dec, Location *loc, DateTime *dt) {
    center_ra = ra;
    center_dec = dec;
    dlg_loc = loc;
    dlg_dt = dt;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), "Select Source");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    // Controls
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Search Radius (deg):"));
    GtkWidget *spin = gtk_spin_button_new_with_range(0.1, 90.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), search_fov);
    gtk_box_append(GTK_BOX(hbox), spin);

    GtkWidget *btn_search = gtk_button_new_with_label("Search");
    gtk_box_append(GTK_BOX(hbox), btn_search);

    // Paned
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(vbox), paned);

    // List
    GtkWidget *list_scroll = gtk_scrolled_window_new();
    gtk_paned_set_start_child(GTK_PANED(paned), list_scroll);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_widget_set_size_request(list_scroll, 250, -1);

    GtkColumnView *list = GTK_COLUMN_VIEW(gtk_column_view_new(NULL)); // Init with NULL model
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), GTK_WIDGET(list));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(setup_cb), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(bind_cb), NULL);

    GtkColumnViewColumn *col = gtk_column_view_column_new("Candidate", factory);
    gtk_column_view_append_column(list, col);

    // Plot
    plot_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(plot_area), on_plot_draw, NULL, NULL);
    gtk_widget_set_size_request(plot_area, 300, -1);
    gtk_paned_set_end_child(GTK_PANED(paned), plot_area);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);

    // Add Button
    GtkWidget *btn_add = gtk_button_new_with_label("Add Selected to Targets");
    gtk_box_append(GTK_BOX(vbox), btn_add);

    // Signals
    g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search_clicked), spin);
    g_object_set_data(G_OBJECT(btn_search), "list_view", list);

    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_target_clicked), list);

    // Initial Populate
    update_candidate_list();
    populate_list(list);

    gtk_window_present(GTK_WINDOW(dialog));
}
