#include <stdio.h>
#include <stdlib.h>
#include "target_list.h"

void on_change() {
    printf("List changed.\n");
}

int main() {
    printf("Initializing target list...\n");
    target_list_init();
    target_list_set_change_callback(on_change);

    printf("Creating list...\n");
    TargetList *list = target_list_create("Test List");

    printf("Adding Target 1...\n");
    target_list_add_target(list, "T1", 10, 10, 1, 0.5);

    printf("Adding Target 2...\n");
    target_list_add_target(list, "T2", 20, 20, 2, 0.6);

    printf("Adding Target 3...\n");
    target_list_add_target(list, "T3", 30, 30, 3, 0.7);

    printf("Adding Target 4...\n");
    target_list_add_target(list, "T4", 40, 40, 4, 0.8);

    printf("Adding Target 5...\n");
    target_list_add_target(list, "T5", 50, 50, 5, 0.9);

    printf("Verifying targets...\n");
    for (int i=0; i<target_list_get_count(list); i++) {
        Target *t = target_list_get_target(list, i);
        printf("Target %d: %s (%.2f, %.2f)\n", i, t->name, t->ra, t->dec);
    }

    printf("Cleanup...\n");
    target_list_cleanup();
    printf("Done.\n");
    return 0;
}
