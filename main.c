#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
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
    double elevation; // meters
    double timezone_offset;
} Site;

Site sites[] = {
    {"Maunakea Observatories", 19.8207, -155.4681, 4205.0, -10.0},
    {"La Palma (Roque de los Muchachos)", 28.7636, -17.8947, 2396.0, 0.0},
    {"Paranal Observatory", -24.6275, -70.4044, 2635.0, -4.0},
    {"Las Campanas Observatory", -29.0146, -70.6926, 2380.0, -4.0},
    {"New York City", 40.7128, -74.0060, 10.0, -5.0},
    {NULL, 0, 0, 0, 0}
};

// Global State
Location loc = {19.8207, -155.4681, 4205.0};
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
    .auto_star_settings = TRUE, // Default requested
    .font_scale = 1.0,
    .ephemeris_use_ut = FALSE
};

// UI Widgets for Target List
static GtkNotebook *target_notebook = NULL;
static TargetList *active_target_list = NULL;

// UI Widgets for Star Settings
static GtkLabel *lbl_site_info = NULL;
static GtkRange *range_mag = NULL;
static GtkRange *range_m0 = NULL;
static GtkRange *range_ma = NULL;
static GtkRange *range_sat = NULL;

// TargetObject Definition for ListModel
#define TYPE_TARGET_OBJECT (target_object_get_type())
G_DECLARE_FINAL_TYPE(TargetObject, target_object, APP, TARGET_OBJECT, GObject)

struct _TargetObject {
    GObject parent_instance;
    char *name;
    double ra;
    double dec;
    double mag;
    double bv;
};

G_DEFINE_TYPE(TargetObject, target_object, G_TYPE_OBJECT)

static void target_object_finalize(GObject *object) {
    TargetObject *self = APP_TARGET_OBJECT(object);
    g_free(self->name);
    G_OBJECT_CLASS(target_object_parent_class)->finalize(object);
}

static void target_object_class_init(TargetObjectClass *klass) {
    G_OBJECT_CLASS(klass)->finalize = target_object_finalize;
}

static void target_object_init(TargetObject *self) {}

static TargetObject *target_object_new(const char *name, double ra, double dec, double mag, double bv) {
    TargetObject *obj = g_object_new(TYPE_TARGET_OBJECT, NULL);
    obj->name = g_strdup(name);
    obj->ra = ra;
    obj->dec = dec;
    obj->mag = mag;
    obj->bv = bv;
    return obj;
}

// ---------------------------------------------------------

static void update_all_views() {
    sky_view_redraw();
    elevation_view_redraw();
}

TargetList *get_active_target_list() {
    return active_target_list;
}

static void on_list_visibility_toggled(GtkCheckButton *btn, gpointer user_data) {
    TargetList *list = (TargetList*)user_data;
    target_list_set_visible(list, gtk_check_button_get_active(btn));
    update_all_views();
}

static void on_target_selection_changed(GtkSelectionModel *model, guint position, guint n_items, gpointer user_data) {
    GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
    guint selected = gtk_single_selection_get_selected(sel);

    Target *target = NULL;
    if (selected != GTK_INVALID_LIST_POSITION && active_target_list) {
        // If sortable, we need to map back to original index, but here we assume filtered list matches?
        // Wait, GtkSortListModel reorders items.
        // We need to get the item from the model.
        // The model is GtkSingleSelection wrapping GtkSortListModel wrapping GListStore.
        // Getting the item at 'selected' gives us the TargetObject.
        // Then we can find it in the TargetList? Or just use the data in TargetObject.
        // Highlighting needs a pointer to the Target struct in TargetList memory (for comparison in sky_view).
        // Since TargetList reallocs, pointers are unstable.
        // But sky_view uses pointer comparison? "Target *highlighted_target".
        // If TargetList reallocs, highlighted_target becomes invalid.
        // This is a pre-existing flaw. We should use index or name/id.
        // However, assuming no add/remove while highlighting...
        // Let's match by name/coords?

        // Actually, we can just find the index in active_target_list that matches the name/coords.
        GObject *item = g_list_model_get_item(G_LIST_MODEL(model), selected);
        if (item) {
             TargetObject *tobj = APP_TARGET_OBJECT(item);
             // Find in active_target_list
             int cnt = target_list_get_count(active_target_list);
             for(int i=0; i<cnt; i++) {
                 Target *t = target_list_get_target(active_target_list, i);
                 if (strcmp(t->name, tobj->name) == 0 && t->ra == tobj->ra && t->dec == tobj->dec) {
                     target = t;
                     break;
                 }
             }
             g_object_unref(item);
        }
    }

    sky_view_set_highlighted_target(target);
    elevation_view_set_highlighted_target(target);
}

// Callback from target_list module
static void on_target_list_changed() {
    update_all_views();

    int pages = gtk_notebook_get_n_pages(target_notebook);
    for (int i=0; i<pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(target_notebook, i);

        TargetList *tl = g_object_get_data(G_OBJECT(page), "target_list");
        if (tl) {
            // Find the column view
            GtkWidget *child = gtk_widget_get_first_child(page);
            GtkWidget *sc = NULL;
            while (child) {
                 if (GTK_IS_SCROLLED_WINDOW(child)) {
                     sc = child;
                     break;
                 }
                 child = gtk_widget_get_next_sibling(child);
            }
            if (!sc) continue;

            GtkWidget *col_view = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(sc));

            if (GTK_IS_COLUMN_VIEW(col_view)) {
                // Get existing selection model or create new?
                // Better to replace the store.
                // Assuming we created it as GtkSingleSelection(GtkSortListModel(GListStore)).
                // If we replace the model on ColumnView, we lose the Sorter connections if not careful?
                // No, Sorter is on Columns.
                // But GtkSortListModel needs the Sorter.

                GListStore *store = g_list_store_new(TYPE_TARGET_OBJECT);
                int cnt = target_list_get_count(tl);
                for (int k=0; k<cnt; k++) {
                    Target *t = target_list_get_target(tl, k);
                    TargetObject *obj = target_object_new(t->name, t->ra, t->dec, t->mag, t->bv);
                    g_list_store_append(store, obj);
                    g_object_unref(obj);
                }

                GtkSorter *sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(col_view));
                GtkSortListModel *sort_model = gtk_sort_list_model_new(G_LIST_MODEL(store), sorter); // sorter ref?
                GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(sort_model));
                gtk_single_selection_set_autoselect(sel, FALSE);

                g_signal_connect(sel, "selection-changed", G_CALLBACK(on_target_selection_changed), NULL);

                gtk_column_view_set_model(GTK_COLUMN_VIEW(col_view), GTK_SELECTION_MODEL(sel));
                g_object_unref(store);
                // g_object_unref(sort_model); // sel holds ref
            }
        }
    }
}

// Column Bind Functions
static void bind_name(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    TargetObject *item = APP_TARGET_OBJECT(gtk_list_item_get_item(list_item));
    gtk_label_set_text(GTK_LABEL(label), item->name);
}
static void bind_ra(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    TargetObject *item = APP_TARGET_OBJECT(gtk_list_item_get_item(list_item));
    char buf[32]; snprintf(buf, 32, "%.5f", item->ra);
    gtk_label_set_text(GTK_LABEL(label), buf);
}
static void bind_dec(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    TargetObject *item = APP_TARGET_OBJECT(gtk_list_item_get_item(list_item));
    char buf[32]; snprintf(buf, 32, "%.5f", item->dec);
    gtk_label_set_text(GTK_LABEL(label), buf);
}
static void bind_mag(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    TargetObject *item = APP_TARGET_OBJECT(gtk_list_item_get_item(list_item));
    char buf[32]; snprintf(buf, 32, "%.2f", item->mag);
    gtk_label_set_text(GTK_LABEL(label), buf);
}
static void bind_bv(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_list_item_get_child(list_item);
    TargetObject *item = APP_TARGET_OBJECT(gtk_list_item_get_item(list_item));
    char buf[32]; snprintf(buf, 32, "%.2f", item->bv);
    gtk_label_set_text(GTK_LABEL(label), buf);
}
static void setup_label(GtkSignalListItemFactory *self, GtkListItem *list_item, gpointer user_data) {
    GtkWidget *label = gtk_label_new(NULL);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_item_set_child(list_item, label);
}

// Sorters
static int compare_name(gconstpointer a, gconstpointer b, gpointer user_data) {
    TargetObject *oa = APP_TARGET_OBJECT((GObject*)a);
    TargetObject *ob = APP_TARGET_OBJECT((GObject*)b);
    return strcmp(oa->name, ob->name);
}
static int compare_ra(gconstpointer a, gconstpointer b, gpointer user_data) {
    TargetObject *oa = APP_TARGET_OBJECT((GObject*)a);
    TargetObject *ob = APP_TARGET_OBJECT((GObject*)b);
    if (oa->ra < ob->ra) return -1;
    if (oa->ra > ob->ra) return 1;
    return 0;
}
static int compare_dec(gconstpointer a, gconstpointer b, gpointer user_data) {
    TargetObject *oa = APP_TARGET_OBJECT((GObject*)a);
    TargetObject *ob = APP_TARGET_OBJECT((GObject*)b);
    if (oa->dec < ob->dec) return -1;
    if (oa->dec > ob->dec) return 1;
    return 0;
}
static int compare_mag(gconstpointer a, gconstpointer b, gpointer user_data) {
    TargetObject *oa = APP_TARGET_OBJECT((GObject*)a);
    TargetObject *ob = APP_TARGET_OBJECT((GObject*)b);
    if (oa->mag < ob->mag) return -1;
    if (oa->mag > ob->mag) return 1;
    return 0;
}
static int compare_bv(gconstpointer a, gconstpointer b, gpointer user_data) {
    TargetObject *oa = APP_TARGET_OBJECT((GObject*)a);
    TargetObject *ob = APP_TARGET_OBJECT((GObject*)b);
    if (oa->bv < ob->bv) return -1;
    if (oa->bv > ob->bv) return 1;
    return 0;
}


static gboolean on_list_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    if (keyval == GDK_KEY_Escape) {
        GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
        if (GTK_IS_COLUMN_VIEW(widget)) {
            GtkSelectionModel *model = gtk_column_view_get_model(GTK_COLUMN_VIEW(widget));
            if (GTK_IS_SINGLE_SELECTION(model)) {
                gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(model), GTK_INVALID_LIST_POSITION);
                return TRUE;
            }
        }
    }
    return FALSE;
}

// Helper to create a view for a list
static GtkWidget *create_view_for_list(TargetList *list) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(box), "target_list", list); // Store reference

    // Visibility Toggle
    GtkWidget *check_visible = gtk_check_button_new_with_label("Show on Map");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check_visible), target_list_is_visible(list));
    g_signal_connect(check_visible, "toggled", G_CALLBACK(on_list_visibility_toggled), list);
    gtk_box_append(GTK_BOX(box), check_visible);

    GtkWidget *scrolled_list = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_list, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled_list);

    GtkColumnView *col_view = GTK_COLUMN_VIEW(gtk_column_view_new(NULL));
    gtk_widget_set_vexpand(GTK_WIDGET(col_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(col_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_list), GTK_WIDGET(col_view));

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_list_key_pressed), NULL);
    gtk_widget_add_controller(GTK_WIDGET(col_view), key_controller);

    // Columns
    struct { char *title; void (*bind)(GtkSignalListItemFactory*, GtkListItem*, gpointer); GtkSorter *(*sorter_func)(); } cols[] = {
        {"Name", bind_name, NULL}, // Need separate sorter creation
        {"RA", bind_ra, NULL},
        {"Dec", bind_dec, NULL},
        {"Mag", bind_mag, NULL},
        {"Color", bind_bv, NULL}
    };

    // Name
    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(setup_label), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(bind_name), NULL);
        GtkColumnViewColumn *col = gtk_column_view_column_new("Name", factory);
        GtkSorter *sorter = gtk_custom_sorter_new(compare_name, NULL, NULL);
        gtk_column_view_column_set_sorter(col, sorter);
        g_object_unref(sorter);
        gtk_column_view_append_column(col_view, col);
    }
    // RA
    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(setup_label), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(bind_ra), NULL);
        GtkColumnViewColumn *col = gtk_column_view_column_new("RA", factory);
        GtkSorter *sorter = gtk_custom_sorter_new(compare_ra, NULL, NULL);
        gtk_column_view_column_set_sorter(col, sorter);
        g_object_unref(sorter);
        gtk_column_view_append_column(col_view, col);
    }
    // Dec
    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(setup_label), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(bind_dec), NULL);
        GtkColumnViewColumn *col = gtk_column_view_column_new("Dec", factory);
        GtkSorter *sorter = gtk_custom_sorter_new(compare_dec, NULL, NULL);
        gtk_column_view_column_set_sorter(col, sorter);
        g_object_unref(sorter);
        gtk_column_view_append_column(col_view, col);
    }
    // Mag
    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(setup_label), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(bind_mag), NULL);
        GtkColumnViewColumn *col = gtk_column_view_column_new("Mag", factory);
        GtkSorter *sorter = gtk_custom_sorter_new(compare_mag, NULL, NULL);
        gtk_column_view_column_set_sorter(col, sorter);
        g_object_unref(sorter);
        gtk_column_view_append_column(col_view, col);
    }
    // Color
    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup", G_CALLBACK(setup_label), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(bind_bv), NULL);
        GtkColumnViewColumn *col = gtk_column_view_column_new("Color", factory);
        GtkSorter *sorter = gtk_custom_sorter_new(compare_bv, NULL, NULL);
        gtk_column_view_column_set_sorter(col, sorter);
        g_object_unref(sorter);
        gtk_column_view_append_column(col_view, col);
    }

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

    // This 'pos' is in the sorted model. We need original index?
    // TargetList is simple array.
    // If we sort, we need to map back.
    // The TargetObject has the Name/RA/Dec.
    // We can search for the target in the list.

    if (pos != GTK_INVALID_LIST_POSITION) {
        GObject *item = g_list_model_get_item(G_LIST_MODEL(model), pos);
        if (item) {
             TargetObject *tobj = APP_TARGET_OBJECT(item);
             int cnt = target_list_get_count(active_target_list);
             for(int i=0; i<cnt; i++) {
                 Target *t = target_list_get_target(active_target_list, i);
                 // Match by unique combination? Name might be dupes.
                 // Assuming distinct enough or first match.
                 if (strcmp(t->name, tobj->name) == 0 && t->ra == tobj->ra && t->dec == tobj->dec) {
                     target_list_remove_target(active_target_list, i);
                     break;
                 }
             }
             g_object_unref(item);
        }
    }
}

// Clear Selection
static void on_clear_selection_clicked(GtkButton *btn, gpointer user_data) {
    if (!active_target_list) return;

    GtkWidget *page = gtk_notebook_get_nth_page(target_notebook, gtk_notebook_get_current_page(target_notebook));
    if (!page) return;
    GtkWidget *scrolled = gtk_widget_get_first_child(page);
    while (scrolled && !GTK_IS_SCROLLED_WINDOW(scrolled)) scrolled = gtk_widget_get_next_sibling(scrolled);
    if (!scrolled) return;

    GtkWidget *col_view = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scrolled));
    if (!col_view || !GTK_IS_COLUMN_VIEW(col_view)) return;

    GtkSelectionModel *model = gtk_column_view_get_model(GTK_COLUMN_VIEW(col_view));
    if (GTK_IS_SINGLE_SELECTION(model)) {
        GtkSingleSelection *sel = GTK_SINGLE_SELECTION(model);
        gtk_single_selection_set_selected(sel, GTK_INVALID_LIST_POSITION);
    }

    // Explicitly clear views just in case signal doesn't trigger (e.g. if already considered invalid)
    sky_view_set_highlighted_target(NULL);
    elevation_view_set_highlighted_target(NULL);
}

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
        // Need to map pos to original index or serialize the item
        GObject *item = g_list_model_get_item(G_LIST_MODEL(model), pos);
        if (item) {
             TargetObject *tobj = APP_TARGET_OBJECT(item);
             // Find original index
             int cnt = target_list_get_count(active_target_list);
             int original_idx = -1;
             for(int i=0; i<cnt; i++) {
                 Target *t = target_list_get_target(active_target_list, i);
                 if (strcmp(t->name, tobj->name) == 0 && t->ra == tobj->ra && t->dec == tobj->dec) {
                     original_idx = i;
                     break;
                 }
             }
             if (original_idx != -1) {
                 char *data = target_list_serialize_targets(active_target_list, &original_idx, 1);
                 if (data) {
                     GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(btn));
                     gdk_clipboard_set_text(clipboard, data);
                     free(data);
                 }
             }
             g_object_unref(item);
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

static void on_elevation_hover(int active, DateTime time, double elev) {
    sky_view_set_hover_state(active, time, elev);
}
