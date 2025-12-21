#ifndef TARGET_LIST_H
#define TARGET_LIST_H

#include <libnova/libnova.h>
#include <jansson.h>
#include <stdbool.h>

typedef struct {
    char name[64];
    double ra; // degrees
    double dec; // degrees
    double mag;
} Target;

typedef struct TargetList TargetList;

// Global Management
void target_list_init();
void target_list_cleanup();
int target_list_get_list_count();
TargetList *target_list_get_list_by_index(int index);
TargetList *target_list_create(const char *name);
void target_list_delete(TargetList *list);

// List Operations
const char *target_list_get_name(TargetList *list);
int target_list_get_count(TargetList *list);
Target *target_list_get_target(TargetList *list, int index);
void target_list_add_target(TargetList *list, const char *name, double ra, double dec, double mag);
void target_list_remove_target(TargetList *list, int index);
void target_list_clear(TargetList *list);

// Visibility
void target_list_set_visible(TargetList *list, bool visible);
bool target_list_is_visible(TargetList *list);

// Serialization
int target_list_save(TargetList *list, const char *filename);
TargetList *target_list_load(const char *filename);

// Clipboard helpers
char *target_list_serialize_targets(TargetList *list, int *indices, int count);
void target_list_deserialize_and_add(TargetList *list, const char *data);

// Callbacks
void target_list_set_change_callback(void (*cb)(void));

#endif
