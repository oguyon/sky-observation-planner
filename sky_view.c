#include "sky_view.h"
#include "catalog.h"
#include <math.h>

static Location *current_loc;
static DateTime *current_dt;
static gboolean *current_show_constellations;
static GtkWidget *drawing_area;
static void (*click_callback)(double, double) = NULL;

// Helper to project Alt/Az to X/Y (0-1 range from center)
static void project(double alt, double az, double *x, double *y) {
    double r = 1.0 - alt / 90.0;
    if (r < 0) r = 0;
    // Az in degrees. Convert to radians.
    // Standard Math: 0 is Right (East), 90 is Up (North).
    // Azimuth: 0 is North, 90 is East.
    // Screen: Y is down.
    // We want North (Az=0) to be Up (Y=-1).
    // East (Az=90) to be Left (X=-1) or Right?
    // Looking up at sky: North Up, East is LEFT. West is RIGHT.
    // But usually sky maps have East on Left if you hold it overhead.
    // Let's stick to standard:
    // x = -r * sin(az) (East is Left)
    // y = -r * cos(az) (North is Top)

    double az_rad = az * M_PI / 180.0;
    *x = -r * sin(az_rad);
    *y = -r * cos(az_rad);
}

// Inverse projection for click
static void unproject(double x, double y, double *alt, double *az) {
    double r = sqrt(x*x + y*y);
    if (r > 1.0) {
        *alt = -1; // Invalid
        return;
    }
    *alt = 90.0 * (1.0 - r);

    // x = -r * sin(az) => sin(az) = -x/r
    // y = -r * cos(az) => cos(az) = -y/r
    double angle = atan2(-x, -y);
    *az = angle * 180.0 / M_PI;
    if (*az < 0) *az += 360.0;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);
    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    // Background
    cairo_set_source_rgb(cr, 0, 0, 0.1); // Dark blue/black
    cairo_paint(cr);

    // Horizon
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Directions
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_move_to(cr, cx, cy - radius); cairo_show_text(cr, "N");
    cairo_move_to(cr, cx, cy + radius); cairo_show_text(cr, "S");
    cairo_move_to(cr, cx - radius, cy); cairo_show_text(cr, "E");
    cairo_move_to(cr, cx + radius, cy); cairo_show_text(cr, "W");

    // Draw Stars
    cairo_set_source_rgb(cr, 1, 1, 1);
    for (int i = 0; i < num_stars; i++) {
        double alt, az;
        get_horizontal_coordinates(stars[i].ra, stars[i].dec, *current_loc, *current_dt, &alt, &az);
        if (alt >= 0) {
            double x, y;
            project(alt, az, &x, &y);
            // Magnitude size
            double size = 2.0 - (stars[i].mag / 3.0);
            if (size < 0.5) size = 0.5;

            cairo_arc(cr, cx + x * radius, cy + y * radius, size, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    // Draw Constellations
    if (*current_show_constellations) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.8, 0.5);
        cairo_set_line_width(cr, 1.0);
        for (int i = 0; i < num_constellations; i++) {
            for(int j=0; j<constellations[i].num_lines; j++) {
                int len = constellations[i].line_lengths[j];
                int first = 1;
                for (int k=0; k<len; k++) {
                    double ra = constellations[i].lines[j][k*2];
                    double dec = constellations[i].lines[j][k*2+1];
                    double alt, az;
                    get_horizontal_coordinates(ra, dec, *current_loc, *current_dt, &alt, &az);

                    if (alt >= 0) {
                        double x, y;
                        project(alt, az, &x, &y);
                        if (first) {
                            cairo_move_to(cr, cx + x * radius, cy + y * radius);
                            first = 0;
                        } else {
                            cairo_line_to(cr, cx + x * radius, cy + y * radius);
                        }
                    } else {
                        first = 1; // Break line if goes below horizon
                    }
                }
                cairo_stroke(cr);
            }
        }
    }

    // Draw Sun
    double s_alt, s_az;
    get_sun_position(*current_loc, *current_dt, &s_alt, &s_az);
    if (s_alt >= 0) {
        double x, y;
        project(s_alt, s_az, &x, &y);
        cairo_set_source_rgb(cr, 1, 1, 0);
        cairo_arc(cr, cx + x * radius, cy + y * radius, 5, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    // Draw Moon
    double m_alt, m_az;
    get_moon_position(*current_loc, *current_dt, &m_alt, &m_az);
    if (m_alt >= 0) {
        double x, y;
        project(m_alt, m_az, &x, &y);
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_arc(cr, cx + x * radius, cy + y * radius, 4, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (click_callback) {
        guint width = gtk_widget_get_allocated_width(widget);
        guint height = gtk_widget_get_allocated_height(widget);
        double radius = (width < height ? width : height) / 2.0 - 10;
        double cx = width / 2.0;
        double cy = height / 2.0;

        double x = (event->x - cx) / radius;
        double y = (event->y - cy) / radius;

        double alt, az;
        unproject(x, y, &alt, &az);
        if (alt >= 0) {
            click_callback(alt, az);
        }
    }
    return TRUE;
}

GtkWidget *create_sky_view(Location *loc, DateTime *dt, gboolean *show_constellations, void (*on_sky_click)(double, double)) {
    current_loc = loc;
    current_dt = dt;
    current_show_constellations = show_constellations;
    click_callback = on_sky_click;

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 400);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(on_draw), NULL);

    gtk_widget_add_events(drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(drawing_area), "button-press-event", G_CALLBACK(on_button_press), NULL);

    return drawing_area;
}

void sky_view_redraw() {
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
}
