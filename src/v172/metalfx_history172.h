#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    BMFFXHistoryFirst172 = 1,
    BMFFXHistoryCameraOrMode172 = 2,
    BMFFXHistorySize172 = 4,
    BMFFXHistoryGap172 = 8,
    BMFFXHistoryJitter172 = 16,
};

typedef struct {
    bool initialized;
    uint64_t generation;
    double lastEncode;
    unsigned size[6];
    float signs[2];
} BMFFXHistory172;

// Called once at the real encode boundary, independently for each scaler.
static unsigned fxHistoryAdvance172(BMFFXHistory172 *history, uint64_t generation,
                                    double now, const unsigned size[6], const float signs[2]) {
    unsigned reasons = history->initialized ? 0 : BMFFXHistoryFirst172;
    if (history->initialized) {
        if (history->generation != generation) reasons |= BMFFXHistoryCameraOrMode172;
        if (memcmp(history->size, size, sizeof history->size)) reasons |= BMFFXHistorySize172;
        if (now - history->lastEncode > .5) reasons |= BMFFXHistoryGap172;
        if (memcmp(history->signs, signs, sizeof history->signs)) reasons |= BMFFXHistoryJitter172;
    }
    history->initialized = true;
    history->generation = generation;
    history->lastEncode = now;
    memcpy(history->size, size, sizeof history->size);
    memcpy(history->signs, signs, sizeof history->signs);
    return reasons;
}
