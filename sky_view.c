#include "sky_view.h"
#include <math.h>

static AppState *app_state;
static SelectionChangedCallback on_selection_changed = NULL;
static void *selection_changed_data = NULL;

static void hrz_to_screen(double alt, double az, double R, double *x, double *y) {
    if (alt < 0) {
        *x = -9999; *y = -9999;
        return;
    }
    double r = R * (90.0 - alt) / 90.0;
    double theta = (90.0 - az) * M_PI / 180.0;

    *x = r * cos(theta);
    *y = r * sin(theta);
}

// Drawing callback for GtkDrawingArea in GTK4
static void on_draw(GtkDrawingArea *drawing_area, cairo_t *cr, int width, int height, gpointer data) {
    AppState *app = (AppState *)data;

    double w = width;
    double h = height;
    double cx = w / 2.0;
    double cy = h / 2.0;
    double R = (w < h ? w : h) / 2.0 - 10; // Margin

    // Draw Background
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.2);
    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI);
    cairo_fill(cr);

    // Draw Horizon
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr, 2);
    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Cardinal Points
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);

    cairo_move_to(cr, cx - 5, cy - R + 15); cairo_show_text(cr, "N");
    cairo_move_to(cr, cx - 5, cy + R - 5); cairo_show_text(cr, "S");
    cairo_move_to(cr, cx + R - 15, cy + 5); cairo_show_text(cr, "W");
    cairo_move_to(cr, cx - R + 5, cy + 5); cairo_show_text(cr, "E");

    // Calculate Star Positions
    struct ln_hrz_posn *star_pos = malloc(sizeof(struct ln_hrz_posn) * app->num_stars);
    double JD = ln_get_julian_day(&app->date);

    for (int i = 0; i < app->num_stars; i++) {
        ln_get_hrz_from_equ(&app->stars[i].pos, &app->observer_location, JD, &star_pos[i]);
    }

    // Draw Constellations
    if (app->show_constellations) {
        cairo_set_source_rgb(cr, 0.4, 0.6, 0.8);
        cairo_set_line_width(cr, 1);
        for (int i = 0; i < app->num_constellations; i++) {
            int *idx = app->constellations[i].star_indices;
            while (*idx != -1) {
                int i1 = *idx;
                int i2 = *(idx+1);
                idx += 2;

                if (star_pos[i1].alt > 0 && star_pos[i2].alt > 0) {
                    double x1, y1, x2, y2;
                    hrz_to_screen(star_pos[i1].alt, star_pos[i1].az, R, &x1, &y1);
                    hrz_to_screen(star_pos[i2].alt, star_pos[i2].az, R, &x2, &y2);

                    cairo_move_to(cr, cx + x1, cy + y1);
                    cairo_line_to(cr, cx + x2, cy + y2);
                    cairo_stroke(cr);
                }
            }
        }
    }

    // Draw Stars
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    for (int i = 0; i < app->num_stars; i++) {
        if (star_pos[i].alt > 0) {
            double x, y;
            hrz_to_screen(star_pos[i].alt, star_pos[i].az, R, &x, &y);

            double size = 3.0 - 0.5 * app->stars[i].mag;
            if (size < 1) size = 1;

            cairo_arc(cr, cx + x, cy + y, size, 0, 2 * M_PI);
            cairo_fill(cr);

             if (app->has_selection && strcmp(app->selection_name, app->stars[i].name) == 0) {
                cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
                cairo_arc(cr, cx + x, cy + y, size + 2, 0, 2 * M_PI);
                cairo_stroke(cr);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // Reset
            }
        }
    }

    free(star_pos);
}

// In GTK4, use GtkGestureClick for clicks
static void on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    AppState *app = (AppState *)data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    int w = gtk_widget_get_width(widget);
    int h = gtk_widget_get_height(widget);
    double cx = w / 2.0;
    double cy = h / 2.0;
    double R = (w < h ? w : h) / 2.0 - 10;

    double mx = x - cx;
    double my = y - cy;
    double r_click = sqrt(mx*mx + my*my);

    if (r_click > R) return;

    double alt = 90.0 - 90.0 * r_click / R;
    double theta = atan2(my, mx);
    double theta_deg = theta * 180.0 / M_PI;
    double az = 90.0 - theta_deg;
    if (az < 0) az += 360.0;
    if (az >= 360) az -= 360.0;

    struct ln_hrz_posn hrz = {az, alt};
    struct ln_equ_posn equ;
    double JD = ln_get_julian_day(&app->date);

    ln_get_equ_from_hrz(&hrz, &app->observer_location, JD, &equ);

    app->has_selection = true;
    app->selection_equ = equ;

    double min_dist = 2.0;
    int closest = -1;

    for (int i = 0; i < app->num_stars; i++) {
        struct ln_hrz_posn pos;
        ln_get_hrz_from_equ(&app->stars[i].pos, &app->observer_location, JD, &pos);
        if (pos.alt > 0) {
            double d_alt = pos.alt - alt;
            double d_az = pos.az - az;
            if (d_az > 180) d_az -= 360;
            if (d_az < -180) d_az += 360;

            double dist = sqrt(d_alt*d_alt + d_az*d_az);
            if (dist < min_dist) {
                min_dist = dist;
                closest = i;
            }
        }
    }

    if (closest != -1) {
        strcpy(app->selection_name, app->stars[closest].name);
        app->selection_equ = app->stars[closest].pos;
    } else {
        sprintf(app->selection_name, "Sky Point");
    }

    gtk_widget_queue_draw(widget);

    if (on_selection_changed) {
        on_selection_changed(selection_changed_data);
    }
}

GtkWidget *sky_view_new(AppState *app, SelectionChangedCallback callback, void *callback_data) {
    app_state = app;
    on_selection_changed = callback;
    selection_changed_data = callback_data;

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 400);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, app, NULL);

    GtkGesture *gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_click), app);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(gesture));

    return drawing_area;
}

void sky_view_redraw(GtkWidget *widget) {
    gtk_widget_queue_draw(widget);
}
