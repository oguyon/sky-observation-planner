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
static GtkColumnView *list_view;
static GtkStringList *source_list_model;
static double search_fov = 10.0;

typedef struct {
    char name[64];
    double ra;
    double dec;
    double mag;
    double dist;
    double bv; // Add B-V color index
} Candidate;

static Candidate *candidates = NULL;
static int candidate_count = 0;

static int selected_candidate_index = -1;

// ROI
typedef struct {
    double min_x, max_x;
    double min_y, max_y;
    int active;
} ROI;
static ROI roi = {0, 0, 0, 0, 0};
static double drag_start_x, drag_start_y;

static int plot_mode = 0; // 0: Dist vs Mag, 1: Color vs Mag

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
            candidates[candidate_count].bv = stars[i].bv;
            if (stars[i].id) {
                snprintf(candidates[candidate_count].name, 64, "%s (Mag %.1f)", stars[i].id, stars[i].mag);
            } else {
                snprintf(candidates[candidate_count].name, 64, "Star (Mag %.1f)", stars[i].mag);
            }
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
            candidates[candidate_count].bv = 0.0; // Default planet color?
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
        candidates[candidate_count].bv = 0.65; // Solar B-V
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
        candidates[candidate_count].bv = 0.0; // Neutral
        strcpy(candidates[candidate_count].name, "Moon");
        candidate_count++;
    }
}

// Helpers for Plot Scaling
static double p_min_mag = 0, p_max_mag = 10;
static double p_pad = 40;
static double p_gw, p_gh;

static void update_plot_ranges(int width, int height) {
    p_min_mag = 100; p_max_mag = -100;
    int found = 0;
    for (int i=0; i<candidate_count; i++) {
        if (candidates[i].mag < p_min_mag) p_min_mag = candidates[i].mag;
        if (candidates[i].mag > p_max_mag) p_max_mag = candidates[i].mag;
        found = 1;
    }
    if (!found) { p_min_mag = 0; p_max_mag = 10; }

    double span = p_max_mag - p_min_mag;
    if (span < 1.0) span = 1.0;
    p_min_mag -= span * 0.1;
    p_max_mag += span * 0.1;

    p_gw = width - 2*p_pad;
    p_gh = height - 2*p_pad;
}

static void map_point(double val_x, double mag, double *x, double *y) {
    // Mode 0: X = Dist. Range: 0 to search_fov.
    // Mode 1: X = Color (B-V). Range: -0.5 to 2.5 (Fixed reasonable range for B-V).

    if (plot_mode == 0) {
        *x = p_pad + (val_x / search_fov) * p_gw;
    } else {
        double min_bv = -0.5;
        double max_bv = 2.5;
        *x = p_pad + ((val_x - min_bv) / (max_bv - min_bv)) * p_gw;
    }

    // Y: Bright (Min Mag) at Top (p_pad), Dim (Max Mag) at Bottom (height - p_pad)
    *y = p_pad + ((mag - p_min_mag) / (p_max_mag - p_min_mag)) * p_gh;
}

static void unmap_point(double x, double y, double *val_x, double *mag) {
    if (plot_mode == 0) {
        *val_x = ((x - p_pad) / p_gw) * search_fov;
    } else {
        double min_bv = -0.5;
        double max_bv = 2.5;
        *val_x = min_bv + ((x - p_pad) / p_gw) * (max_bv - min_bv);
    }

    *mag = p_min_mag + ((y - p_pad) / p_gh) * (p_max_mag - p_min_mag);
}

static int is_in_roi(double val_x, double mag) {
    if (!roi.active) return 1;
    // ROI stores data coords. We assume ROI coords match current mode.
    // When switching mode, we should clear ROI.
    return (val_x >= roi.min_x && val_x <= roi.max_x && mag >= roi.min_y && mag <= roi.max_y);
}

// B-V to RGB (Replicated from sky_view.c)
static void plot_bv_to_rgb(double bv, double *r, double *g, double *b) {
    if (bv < 0.0) { *r = 0.6; *g = 0.6; *b = 1.0; } // Blue
    else if (bv < 0.5) {
        double t = bv / 0.5;
        *r = 0.6 + 0.4*t; *g = 0.6 + 0.4*t; *b = 1.0;
    } else if (bv < 1.0) {
        double t = (bv - 0.5) / 0.5;
        *r = 1.0; *g = 1.0; *b = 1.0 - 0.5*t;
    } else if (bv < 1.5) {
        double t = (bv - 1.0) / 0.5;
        *r = 1.0; *g = 1.0 - 0.4*t; *b = 0.5 - 0.5*t;
    } else {
        *r = 1.0; *g = 0.6; *b = 0.0; // Red
    }
}

// Forward decl
static void populate_list();

static void on_plot_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    update_plot_ranges(width, height);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);

    // Axes
    cairo_move_to(cr, p_pad, p_pad);
    cairo_line_to(cr, p_pad, height - p_pad);
    cairo_line_to(cr, width - p_pad, height - p_pad);
    cairo_stroke(cr);

    // Labels
    cairo_move_to(cr, width/2 - 20, height - 5);
    if (plot_mode == 0) cairo_show_text(cr, "Dist (deg)");
    else cairo_show_text(cr, "Color (B-V)");

    cairo_save(cr);
    cairo_rotate(cr, -M_PI/2);
    cairo_move_to(cr, -height/2 - 20, 15);
    cairo_show_text(cr, "Magnitude");
    cairo_restore(cr);

    // Ticks X
    for (int i=0; i<=5; i++) {
        double val;
        if (plot_mode == 0) val = (i / 5.0) * search_fov;
        else val = -0.5 + (i / 5.0) * 3.0; // -0.5 to 2.5

        double x, y_dummy;
        map_point(val, p_min_mag, &x, &y_dummy);
        cairo_move_to(cr, x, height - p_pad);
        cairo_line_to(cr, x, height - p_pad + 5);
        cairo_stroke(cr);
        char buf[16]; snprintf(buf, 16, "%.1f", val);
        cairo_text_extents_t ext; cairo_text_extents(cr, buf, &ext);
        cairo_move_to(cr, x - ext.width/2, height - p_pad + 15);
        cairo_show_text(cr, buf);
    }

    // Ticks Y (Mag)
    for (int i=0; i<=5; i++) {
        double m = p_min_mag + (i/5.0) * (p_max_mag - p_min_mag);
        double x_dummy, y;
        map_point(0, m, &x_dummy, &y); // x value irrelevant for Y mapping
        cairo_move_to(cr, p_pad, y);
        cairo_line_to(cr, p_pad - 5, y);
        cairo_stroke(cr);
        char buf[16]; snprintf(buf, 16, "%.1f", m);
        cairo_text_extents_t ext; cairo_text_extents(cr, buf, &ext);
        cairo_move_to(cr, p_pad - 8 - ext.width, y + ext.height/2);
        cairo_show_text(cr, buf);
    }

    // Points
    for (int i=0; i<candidate_count; i++) {
        double val_x = (plot_mode == 0) ? candidates[i].dist : candidates[i].bv;
        int in_r = is_in_roi(val_x, candidates[i].mag);

        double x, y;
        map_point(val_x, candidates[i].mag, &x, &y);

        double r, g, b;
        plot_bv_to_rgb(candidates[i].bv, &r, &g, &b);

        if (roi.active && !in_r) {
            cairo_set_source_rgba(cr, r, g, b, 0.2); // Greyed/Dimmed color
        } else {
            cairo_set_source_rgb(cr, r, g, b);
        }

        cairo_arc(cr, x, y, 3, 0, 2*M_PI);
        cairo_fill(cr);

        // Stroke black to make it visible on white bg? Or just fill?
        // Bright stars on white might be hard to see (e.g. yellow).
        // Let's add a thin black outline.
        cairo_set_line_width(cr, 0.5);
        cairo_set_source_rgba(cr, 0, 0, 0, (roi.active && !in_r) ? 0.2 : 1.0);
        cairo_arc(cr, x, y, 3, 0, 2*M_PI);
        cairo_stroke(cr);

        // Selected Circle
        if (i == selected_candidate_index) {
            cairo_set_source_rgb(cr, 1, 0, 0); // Red
            cairo_set_line_width(cr, 2.0);
            cairo_arc(cr, x, y, 6, 0, 2*M_PI);
            cairo_stroke(cr);
        }
    }

    // Draw ROI Rect
    if (roi.active) {
        double x1, y1, x2, y2;
        map_point(roi.min_x, roi.min_y, &x1, &y1);
        map_point(roi.max_x, roi.max_y, &x2, &y2);

        cairo_set_source_rgba(cr, 0, 0, 1, 0.1);
        cairo_rectangle(cr, x1, y1, x2-x1, y2-y1);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0, 0, 1);
        cairo_rectangle(cr, x1, y1, x2-x1, y2-y1);
        cairo_stroke(cr);
    }
}

static void on_plot_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    if (candidate_count == 0) return;

    // Find nearest
    int best_idx = -1;
    double best_dist_sq = 1e9;

    for (int i=0; i<candidate_count; i++) {
        if (roi.active && !is_in_roi(candidates[i].dist, candidates[i].mag)) continue;

        double px, py;
        map_point(candidates[i].dist, candidates[i].mag, &px, &py);
        double d2 = (x-px)*(x-px) + (y-py)*(y-py);
        if (d2 < best_dist_sq) {
            best_dist_sq = d2;
            best_idx = i;
        }
    }

    if (best_idx >= 0 && best_dist_sq < 400) { // 20px radius tolerance
        selected_candidate_index = best_idx;
        gtk_widget_queue_draw(plot_area);

        if (source_list_model) {
            // Iterate candidates, count how many valid before `best_idx`.
            int list_idx = 0;
            for (int i=0; i<best_idx; i++) {
                double val_x = (plot_mode == 0) ? candidates[i].dist : candidates[i].bv;
                if (!roi.active || is_in_roi(val_x, candidates[i].mag)) {
                    list_idx++;
                }
            }

            GtkSelectionModel *sel_model = gtk_column_view_get_model(list_view);
            gtk_selection_model_select_item(sel_model, list_idx, TRUE);
        }
    }
}

static void on_plot_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data) {
    drag_start_x = x;
    drag_start_y = y;
    roi.active = 0; // Clear old ROI on new drag? Or add modifier? Let's clear.
    populate_list(); // Update list to show all
    gtk_widget_queue_draw(plot_area);
}

static void on_plot_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    double end_x = drag_start_x + offset_x;
    double end_y = drag_start_y + offset_y;

    double d1, m1, d2, m2;
    unmap_point(drag_start_x, drag_start_y, &d1, &m1);
    unmap_point(end_x, end_y, &d2, &m2);

    roi.min_x = (d1 < d2) ? d1 : d2;
    roi.max_x = (d1 > d2) ? d1 : d2;
    roi.min_y = (m1 < m2) ? m1 : m2;
    roi.max_y = (m1 > m2) ? m1 : m2;
    roi.active = 1;

    gtk_widget_queue_draw(plot_area);
}

static void on_plot_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    // Refresh list to filter
    populate_list();
}

static void on_list_selection_changed(GtkSelectionModel *model, guint position, guint n_items, gpointer user_data) {
    // Find which candidate corresponds to list index `position` (if selected)
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint selected = gtk_single_selection_get_selected(sel);

    if (selected == GTK_INVALID_LIST_POSITION) {
        selected_candidate_index = -1;
    } else {
        // Map List Index -> Candidate Index
        int current_list_idx = 0;
        for (int i=0; i<candidate_count; i++) {
            double val_x = (plot_mode == 0) ? candidates[i].dist : candidates[i].bv;
            if (!roi.active || is_in_roi(val_x, candidates[i].mag)) {
                if (current_list_idx == selected) {
                    selected_candidate_index = i;
                    break;
                }
                current_list_idx++;
            }
        }
    }
    gtk_widget_queue_draw(plot_area);
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

// Maps Candidate Index to List Item String
static void populate_list() {
    // Count valid
    int valid_count = 0;
    for (int i=0; i<candidate_count; i++) {
        double val_x = (plot_mode == 0) ? candidates[i].dist : candidates[i].bv;
        if (!roi.active || is_in_roi(val_x, candidates[i].mag)) valid_count++;
    }

    const char **items = malloc(sizeof(char*) * (valid_count + 1));
    int idx = 0;
    for (int i=0; i<candidate_count; i++) {
        double val_x = (plot_mode == 0) ? candidates[i].dist : candidates[i].bv;
        if (!roi.active || is_in_roi(val_x, candidates[i].mag)) {
            char *buf = malloc(128);
            if (plot_mode == 0) {
                snprintf(buf, 128, "%s | M:%.1f | D:%.2f", candidates[i].name, candidates[i].mag, candidates[i].dist);
            } else {
                snprintf(buf, 128, "%s | M:%.1f | BV:%.2f", candidates[i].name, candidates[i].mag, candidates[i].bv);
            }
            items[idx++] = buf;
        }
    }
    items[valid_count] = NULL;

    source_list_model = gtk_string_list_new(items);
    GtkSingleSelection *selection = gtk_single_selection_new(G_LIST_MODEL(source_list_model));
    g_signal_connect(selection, "selection-changed", G_CALLBACK(on_list_selection_changed), NULL);

    gtk_column_view_set_model(list_view, GTK_SELECTION_MODEL(selection));

    for (int i=0; i<valid_count; i++) free((char*)items[i]);
    free(items);
}

static void on_search_clicked(GtkButton *btn, gpointer user_data) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(user_data);
    search_fov = gtk_spin_button_get_value(spin);
    update_candidate_list();
    populate_list();
    gtk_widget_queue_draw(plot_area);
}

static void on_clear_roi_clicked(GtkButton *btn, gpointer user_data) {
    roi.active = 0;
    populate_list();
    gtk_widget_queue_draw(plot_area);
}

static void on_plot_mode_toggled(GtkCheckButton *btn, gpointer user_data) {
    int new_mode = gtk_check_button_get_active(btn) ? 1 : 0;
    if (new_mode != plot_mode) {
        plot_mode = new_mode;
        roi.active = 0; // Clear ROI on mode switch
        populate_list();
        gtk_widget_queue_draw(plot_area);
    }
}

static TargetList *dlg_target_list;

static void on_add_target_clicked(GtkButton *btn, gpointer user_data) {
    if (selected_candidate_index >= 0 && selected_candidate_index < candidate_count) {
        if (dlg_target_list) {
            target_list_add_target(dlg_target_list, candidates[selected_candidate_index].name, candidates[selected_candidate_index].ra, candidates[selected_candidate_index].dec, candidates[selected_candidate_index].mag);
            sky_view_redraw();
            elevation_view_redraw();
        }
    }
}

void show_source_selection_dialog(GtkWindow *parent, double ra, double dec, Location *loc, DateTime *dt, TargetList *target_list) {
    center_ra = ra;
    center_dec = dec;
    dlg_loc = loc;
    dlg_dt = dt;
    dlg_target_list = target_list;
    roi.active = 0;
    selected_candidate_index = -1;

    // Initial search radius based on zoom
    double zoom = sky_view_get_zoom();
    if (zoom > 0) search_fov = 10.0 / zoom;
    else search_fov = 10.0;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), "Select Source");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 500);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    // Header Info
    char buf[64];
    snprintf(buf, 64, "Search Center: RA %.2f, Dec %.2f", ra, dec);
    gtk_box_append(GTK_BOX(vbox), gtk_label_new(buf));

    // Controls
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_box_append(GTK_BOX(hbox), gtk_label_new("Radius (deg):"));
    GtkWidget *spin = gtk_spin_button_new_with_range(0.1, 90.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), search_fov);
    gtk_box_append(GTK_BOX(hbox), spin);

    GtkWidget *btn_search = gtk_button_new_with_label("Search");
    g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search_clicked), spin);
    gtk_box_append(GTK_BOX(hbox), btn_search);

    GtkWidget *btn_clear_roi = gtk_button_new_with_label("Clear ROI");
    g_signal_connect(btn_clear_roi, "clicked", G_CALLBACK(on_clear_roi_clicked), NULL);
    gtk_box_append(GTK_BOX(hbox), btn_clear_roi);

    GtkWidget *cb_plot_mode = gtk_check_button_new_with_label("Color vs Mag");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb_plot_mode), plot_mode);
    g_signal_connect(cb_plot_mode, "toggled", G_CALLBACK(on_plot_mode_toggled), NULL);
    gtk_box_append(GTK_BOX(hbox), cb_plot_mode);

    // Paned
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(vbox), paned);

    // List
    GtkWidget *list_scroll = gtk_scrolled_window_new();
    gtk_paned_set_start_child(GTK_PANED(paned), list_scroll);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_widget_set_size_request(list_scroll, 300, -1);

    list_view = GTK_COLUMN_VIEW(gtk_column_view_new(NULL));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), GTK_WIDGET(list_view));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(setup_cb), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(bind_cb), NULL);

    GtkColumnViewColumn *col = gtk_column_view_column_new("Candidate", factory);
    gtk_column_view_append_column(list_view, col);

    // Plot
    plot_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(plot_area), on_plot_draw, NULL, NULL);
    gtk_widget_set_size_request(plot_area, 400, -1);
    gtk_paned_set_end_child(GTK_PANED(paned), plot_area);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);

    // Plot Interactions
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_plot_click), NULL);
    gtk_widget_add_controller(plot_area, GTK_EVENT_CONTROLLER(click));

    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_plot_drag_begin), NULL);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_plot_drag_update), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_plot_drag_end), NULL);
    gtk_widget_add_controller(plot_area, GTK_EVENT_CONTROLLER(drag));

    // Add Button
    GtkWidget *btn_add = gtk_button_new_with_label("Add Selected to Targets");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_target_clicked), NULL);
    gtk_box_append(GTK_BOX(vbox), btn_add);

    // Initial Populate
    update_candidate_list();
    populate_list();

    gtk_window_present(GTK_WINDOW(dialog));
}
