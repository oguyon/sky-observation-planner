#ifndef TARGET_LIST_H
#define TARGET_LIST_H

#include <libnova/libnova.h>

typedef struct {
    char name[64];
    double ra; // degrees
    double dec; // degrees
    double mag;
} Target;

void target_list_add(const char *name, double ra, double dec, double mag);
void target_list_remove(int index);
void target_list_clear();
int target_list_get_count();
Target *target_list_get(int index);

void target_list_set_change_callback(void (*cb)(void));

#endif
