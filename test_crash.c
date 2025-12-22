#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "target_list.h"

// Mocking main.c TargetObject logic to check for crashes there
typedef struct {
    char *name;
    double ra, dec, mag, bv;
} TargetObject;

TargetObject *target_object_new(const char *name, double ra, double dec, double mag, double bv) {
    TargetObject *obj = malloc(sizeof(TargetObject));
    obj->name = strdup(name);
    obj->ra = ra; obj->dec = dec; obj->mag = mag; obj->bv = bv;
    return obj;
}

void target_object_free(TargetObject *obj) {
    free(obj->name);
    free(obj);
}

// Mocking the listener
void on_change() {
    printf("List changed callback.\n");
    int count = target_list_get_list_count();
    for (int i=0; i<count; i++) {
        TargetList *tl = target_list_get_list_by_index(i);
        int t_count = target_list_get_count(tl);
        printf("List %d has %d targets.\n", i, t_count);
        for (int j=0; j<t_count; j++) {
            Target *t = target_list_get_target(tl, j);
            // Simulate main.c creating objects
            TargetObject *obj = target_object_new(t->name, t->ra, t->dec, t->mag, t->bv);
            // Simulate accessing data
            if (obj->ra < 0) printf("Negative RA\n");
            target_object_free(obj);
        }
    }
}

int main() {
    printf("Initializing...\n");
    target_list_init();
    target_list_set_change_callback(on_change);

    TargetList *list = target_list_create("Default");

    printf("Adding 1...\n");
    target_list_add_target(list, "T1", 10, 10, 1, 0);

    printf("Adding 2...\n");
    target_list_add_target(list, "T2", 20, 20, 2, 0);

    printf("Adding 3 (Crash reported here)...\n");
    target_list_add_target(list, "T3", 30, 30, 3, 0);

    printf("Adding 4...\n");
    target_list_add_target(list, "T4", 40, 40, 4, 0);

    printf("Success.\n");
    return 0;
}
