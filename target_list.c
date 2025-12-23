#include "target_list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jansson.h>

struct TargetList {
    char name[128];
    Target **targets;
    int count;
    int capacity;
    bool visible;
};

static TargetList **lists = NULL;
static int list_count = 0;
static int list_capacity = 0;
static void (*change_cb)(void) = NULL;

static void notify_change() {
    if (change_cb) change_cb();
}

void target_list_init() {
    lists = NULL;
    list_count = 0;
    list_capacity = 0;
}

void target_list_cleanup() {
    for (int i=0; i<list_count; i++) {
        for (int j=0; j<lists[i]->count; j++) {
            free(lists[i]->targets[j]);
        }
        free(lists[i]->targets);
        free(lists[i]);
    }
    free(lists);
    lists = NULL;
    list_count = 0;
    list_capacity = 0;
}

int target_list_get_list_count() {
    return list_count;
}

TargetList *target_list_get_list_by_index(int index) {
    if (index < 0 || index >= list_count) return NULL;
    return lists[index];
}

TargetList *target_list_create(const char *name) {
    if (list_count == list_capacity) {
        list_capacity = list_capacity == 0 ? 4 : list_capacity * 2;
        lists = realloc(lists, list_capacity * sizeof(TargetList*));
    }
    TargetList *list = malloc(sizeof(TargetList));
    strncpy(list->name, name, 127);
    list->name[127] = '\0';
    list->targets = NULL;
    list->count = 0;
    list->capacity = 0;
    list->visible = true;

    lists[list_count++] = list;
    notify_change();
    return list;
}

void target_list_delete(TargetList *list) {
    int index = -1;
    for (int i=0; i<list_count; i++) {
        if (lists[i] == list) {
            index = i;
            break;
        }
    }
    if (index == -1) return;

    for (int j=0; j<list->count; j++) {
        free(list->targets[j]);
    }
    free(list->targets);
    free(list);

    for (int i=index; i<list_count-1; i++) {
        lists[i] = lists[i+1];
    }
    list_count--;
    notify_change();
}

const char *target_list_get_name(TargetList *list) {
    if (!list) return "";
    return list->name;
}

int target_list_get_count(TargetList *list) {
    if (!list) return 0;
    return list->count;
}

Target *target_list_get_target(TargetList *list, int index) {
    if (!list || index < 0 || index >= list->count) return NULL;
    if (!list->targets) return NULL;
    return list->targets[index];
}

void target_list_add_target(TargetList *list, const char *name, double ra, double dec, double mag, double bv) {
    if (!list) return;
    if (list->count == list->capacity) {
        int new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Target **new_targets = realloc(list->targets, new_capacity * sizeof(Target*));
        if (!new_targets) return; // Allocation failed
        list->targets = new_targets;
        list->capacity = new_capacity;
    }

    // Defensive check
    if (list->count >= list->capacity) {
        // Should not happen if realloc logic is correct
        return;
    }

    list->targets[list->count] = malloc(sizeof(Target));
    if (list->targets[list->count]) {
        // Zero init for safety
        memset(list->targets[list->count], 0, sizeof(Target));

        if (name) {
            strncpy(list->targets[list->count]->name, name, 63);
            list->targets[list->count]->name[63] = '\0';
        } else {
            strcpy(list->targets[list->count]->name, "Unknown");
        }
        list->targets[list->count]->ra = ra;
        list->targets[list->count]->dec = dec;
        list->targets[list->count]->mag = mag;
        list->targets[list->count]->bv = bv;

        list->count++;
        notify_change();
    }
}

void target_list_remove_target(TargetList *list, int index) {
    if (!list || index < 0 || index >= list->count) return;
    free(list->targets[index]);
    for (int i = index; i < list->count - 1; i++) {
        list->targets[i] = list->targets[i + 1];
    }
    list->count--;
    notify_change();
}

void target_list_clear(TargetList *list) {
    if (!list) return;
    for (int i=0; i<list->count; i++) {
        free(list->targets[i]);
    }
    free(list->targets);
    list->targets = NULL;
    list->count = 0;
    list->capacity = 0;
    notify_change();
}

int target_list_save(TargetList *list, const char *filename) {
    if (!list) return -1;
    json_t *root = json_object();
    json_object_set_new(root, "name", json_string(list->name));
    json_t *arr = json_array();
    for (int i=0; i<list->count; i++) {
        json_t *t = json_object();
        json_object_set_new(t, "name", json_string(list->targets[i]->name));
        json_object_set_new(t, "ra", json_real(list->targets[i]->ra));
        json_object_set_new(t, "dec", json_real(list->targets[i]->dec));
        json_object_set_new(t, "mag", json_real(list->targets[i]->mag));
        json_object_set_new(t, "bv", json_real(list->targets[i]->bv));
        json_array_append_new(arr, t);
    }
    json_object_set_new(root, "targets", arr);

    int ret = json_dump_file(root, filename, JSON_INDENT(4));
    json_decref(root);
    return ret;
}

TargetList *target_list_load(const char *filename) {
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root) return NULL;

    const char *name = json_string_value(json_object_get(root, "name"));
    if (!name) name = "Loaded List";

    TargetList *list = target_list_create(name);

    json_t *arr = json_object_get(root, "targets");
    if (json_is_array(arr)) {
        size_t index;
        json_t *value;
        json_array_foreach(arr, index, value) {
            const char *t_name = json_string_value(json_object_get(value, "name"));
            double ra = json_real_value(json_object_get(value, "ra"));
            double dec = json_real_value(json_object_get(value, "dec"));
            double mag = json_real_value(json_object_get(value, "mag"));
            double bv = json_real_value(json_object_get(value, "bv")); // Defaults to 0 if missing
            if (t_name) {
                target_list_add_target(list, t_name, ra, dec, mag, bv);
            }
        }
    }
    json_decref(root);
    return list;
}

char *target_list_serialize_targets(TargetList *list, int *indices, int count) {
    if (!list || !indices || count <= 0) return NULL;
    json_t *arr = json_array();
    for (int i=0; i<count; i++) {
        int idx = indices[i];
        if (idx >= 0 && idx < list->count) {
            json_t *t = json_object();
            json_object_set_new(t, "name", json_string(list->targets[idx]->name));
            json_object_set_new(t, "ra", json_real(list->targets[idx]->ra));
            json_object_set_new(t, "dec", json_real(list->targets[idx]->dec));
            json_object_set_new(t, "mag", json_real(list->targets[idx]->mag));
            json_object_set_new(t, "bv", json_real(list->targets[idx]->bv));
            json_array_append_new(arr, t);
        }
    }
    char *res = json_dumps(arr, 0);
    json_decref(arr);
    return res;
}

void target_list_deserialize_and_add(TargetList *list, const char *data) {
    if (!list || !data) return;
    json_error_t error;
    json_t *arr = json_loads(data, 0, &error);
    if (!arr || !json_is_array(arr)) {
        if(arr) json_decref(arr);
        return;
    }

    size_t index;
    json_t *value;
    json_array_foreach(arr, index, value) {
        const char *name = json_string_value(json_object_get(value, "name"));
        double ra = json_real_value(json_object_get(value, "ra"));
        double dec = json_real_value(json_object_get(value, "dec"));
        double mag = json_real_value(json_object_get(value, "mag"));
        double bv = json_real_value(json_object_get(value, "bv"));
        if (name) {
            target_list_add_target(list, name, ra, dec, mag, bv);
        }
    }
    json_decref(arr);
    notify_change();
}

void target_list_set_visible(TargetList *list, bool visible) {
    if (list) {
        list->visible = visible;
        notify_change();
    }
}

bool target_list_is_visible(TargetList *list) {
    if (list) return list->visible;
    return false;
}

void target_list_set_change_callback(void (*cb)(void)) {
    change_cb = cb;
}
