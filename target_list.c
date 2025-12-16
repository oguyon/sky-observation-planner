#include "target_list.h"
#include <stdlib.h>
#include <string.h>

static Target *targets = NULL;
static int target_count = 0;
static int target_capacity = 0;

void target_list_add(const char *name, double ra, double dec, double mag) {
    if (target_count == target_capacity) {
        target_capacity = target_capacity == 0 ? 4 : target_capacity * 2;
        targets = realloc(targets, target_capacity * sizeof(Target));
    }
    strncpy(targets[target_count].name, name, 63);
    targets[target_count].name[63] = '\0';
    targets[target_count].ra = ra;
    targets[target_count].dec = dec;
    targets[target_count].mag = mag;
    target_count++;
}

void target_list_remove(int index) {
    if (index < 0 || index >= target_count) return;
    for (int i = index; i < target_count - 1; i++) {
        targets[i] = targets[i + 1];
    }
    target_count--;
}

void target_list_clear() {
    free(targets);
    targets = NULL;
    target_count = 0;
    target_capacity = 0;
}

int target_list_get_count() {
    return target_count;
}

Target *target_list_get(int index) {
    if (index < 0 || index >= target_count) return NULL;
    return &targets[index];
}
