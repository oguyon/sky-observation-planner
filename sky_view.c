#include "sky_view.h"
#include <math.h>

static AppState *app_state;
static SelectionChangedCallback on_selection_changed = NULL;
static void *selection_changed_data = NULL;

// Coordinate transformation: Horizontal (Alt, Az) to Screen (x, y)
// Center (cx, cy), Radius R
// North Up (Az=180 -> Up), East Left (Az=270 -> Left)
// Azimuth in degrees: 0 South, 90 West, 180 North, 270 East
// Screen X: -R..R, Y: -R..R
static void hrz_to_screen(double alt, double az, double R, double *x, double *y) {
    if (alt < 0) {
        *x = -9999; *y = -9999; // Off screen
        return;
    }
    double r = R * (90.0 - alt) / 90.0;
    // Az=180 (North) -> Up (y negative). theta = -90 deg.
    // Az=270 (East) -> Left (x negative). theta = 180 deg.
    // theta = Az - 270?
    // 180 - 270 = -90. Correct.
    // 270 - 270 = 0 (Right). Incorrect. East should be Left.
    // Wait. North Up, East Left.
    // North (180) -> Up (0, -1)
    // East (270) -> Left (-1, 0)
    // South (0) -> Down (0, 1)
    // West (90) -> Right (1, 0)

    // Angle from positive X axis (Right):
    // Up: -90 deg (or 270)
    // Left: 180 deg
    // Down: 90 deg
    // Right: 0 deg

    // Mapping:
    // Az 180 -> -90
    // Az 270 -> 180
    // Az 0 -> 90
    // Az 90 -> 0

    // Linear?
    // 180 -> -90
    // 90 -> 0
    // Delta Az = -90 -> Delta Screen = 90. Slope = -1.
    // ScreenAngle = C - Az
    // 0 = C - 90 -> C = 90.
    // Check:
    // 180 -> 90 - 180 = -90. Correct.
    // 270 -> 90 - 270 = -180 (same as 180). Correct.
    // 0 -> 90 - 0 = 90. Correct.

    // So theta_rad = (90 - Az) * PI / 180.
    double theta = (90.0 - az) * M_PI / 180.0;

    *x = r * cos(theta);
    *y = r * sin(theta); // y grows down in GTK
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    AppState *app = (AppState *)data;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    double w = allocation.width;
    double h = allocation.height;
    double cx = w / 2.0;
    double cy = h / 2.0;
    double R = (w < h ? w : h) / 2.0 - 10; // Margin

    // Draw Background (Sky)
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.2); // Dark Blue
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

            // Size based on magnitude (roughly)
            // Mag -1.5 (Sirius) -> Large. Mag 6 -> Small.
            // Size = 3 - 0.5 * Mag?
            // Sirius: 3 - (-0.7) = 3.7
            // Mag 2: 3 - 1 = 2
            double size = 3.0 - 0.5 * app->stars[i].mag;
            if (size < 1) size = 1;

            cairo_arc(cr, cx + x, cy + y, size, 0, 2 * M_PI);
            cairo_fill(cr);

            // Check if selected
            // (Simple check by name if needed, or if coordinate matches selection)
             if (app->has_selection && strcmp(app->selection_name, app->stars[i].name) == 0) {
                cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
                cairo_arc(cr, cx + x, cy + y, size + 2, 0, 2 * M_PI);
                cairo_stroke(cr);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // Reset
            }
        }
    }

    free(star_pos);
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    AppState *app = (AppState *)data;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    double w = allocation.width;
    double h = allocation.height;
    double cx = w / 2.0;
    double cy = h / 2.0;
    double R = (w < h ? w : h) / 2.0 - 10;

    double mx = event->x - cx;
    double my = event->y - cy;
    double r_click = sqrt(mx*mx + my*my);

    if (r_click > R) return FALSE; // Clicked outside

    // Inverse Transform
    // r = R * (90 - Alt) / 90 -> Alt = 90 - 90 * r / R
    double alt = 90.0 - 90.0 * r_click / R;

    // x = r cos(theta), y = r sin(theta) -> theta = atan2(y, x)
    double theta = atan2(my, mx); // Radians
    // theta_deg = (90 - Az) -> Az = 90 - theta_deg
    double theta_deg = theta * 180.0 / M_PI;
    double az = 90.0 - theta_deg;
    if (az < 0) az += 360.0;
    if (az >= 360) az -= 360.0;

    // Find closest star?
    // For now, let's just set the selection to this coordinate (approx)
    // We need to convert back to RA/Dec
    struct ln_hrz_posn hrz = {az, alt};
    struct ln_equ_posn equ;
    double JD = ln_get_julian_day(&app->date);

    ln_get_equ_from_hrz(&hrz, &app->observer_location, JD, &equ);

    app->has_selection = true;
    app->selection_equ = equ;

    // Check if we clicked near a star
    // Simple closest star search
    double min_dist = 2.0; // degrees (approx click tolerance)
    int closest = -1;

    // Recompute star positions to match click
    // Optimization: we could store last computed positions in AppState or widget
    for (int i = 0; i < app->num_stars; i++) {
        struct ln_hrz_posn pos;
        ln_get_hrz_from_equ(&app->stars[i].pos, &app->observer_location, JD, &pos);
        if (pos.alt > 0) {
            double d_alt = pos.alt - alt;
            double d_az = pos.az - az; // This is rough on sphere, but okay for click
            // Handle Az wrap around 0/360
            if (d_az > 180) d_az -= 360;
            if (d_az < -180) d_az += 360;

            double dist = sqrt(d_alt*d_alt + d_az*d_az); // Rough
            if (dist < min_dist) {
                min_dist = dist;
                closest = i;
            }
        }
    }

    if (closest != -1) {
        strcpy(app->selection_name, app->stars[closest].name);
        app->selection_equ = app->stars[closest].pos; // Snap to star
    } else {
        sprintf(app->selection_name, "Sky Point");
    }

    gtk_widget_queue_draw(widget);

    if (on_selection_changed) {
        on_selection_changed(selection_changed_data);
    }

    return TRUE;
}

GtkWidget *sky_view_new(AppState *app, SelectionChangedCallback callback, void *callback_data) {
    app_state = app;
    on_selection_changed = callback;
    selection_changed_data = callback_data;

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 400);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(on_draw), app);

    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(drawing_area), "button-press-event", G_CALLBACK(on_button_press), app);

    return drawing_area;
}

void sky_view_redraw(GtkWidget *widget) {
    gtk_widget_queue_draw(widget);
}
