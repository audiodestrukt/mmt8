#include <string.h>
#include <strings.h>
#include "mmt8_keys.h"

const mmt8_key_t mmt8_keys[] = {
    /* transport (column 0) */
    { "REW",    "<<",        0, 0 },
    { "FF",     ">>",        0, 1 },
    { "ERASE",  "ERASE",     0, 2 },
    { "TRANS",  "TRANS",     0, 3 },
    { "PLAY",   "PLAY",      0, 4 },
    { "STOP",   "STOP",      0, 5 },
    { "COPY",   "COPY",      0, 6 },
    { "REC",    "REC",       0, 7 },
    /* tracks (column 1) */
    { "T1", "1", 1, 0 }, { "T2", "2", 1, 1 }, { "T3", "3", 1, 2 }, { "T4", "4", 1, 3 },
    { "T5", "5", 1, 4 }, { "T6", "6", 1, 5 }, { "T7", "7", 1, 6 }, { "T8", "8", 1, 7 },
    /* column 2 */
    { "TEMPO",  "TEMPO",     2, 0 },
    { "MINUS",  "-",         2, 1 },
    { "PLUS",   "+",         2, 2 },
    { "PGUP",   "PG UP",     2, 6 },
    { "PGDN",   "PG DN",     2, 7 },
    /* column 3 */
    { "CLICK",  "CLICK",     3, 0 },
    { "6", "6", 3, 1 }, { "7", "7", 3, 2 }, { "8", "8", 3, 3 }, { "9", "9", 3, 4 }, { "0", "0", 3, 5 },
    { "MIDICH", "MIDI CH",   3, 6 },
    { "TAPE",   "TAPE",      3, 7 },
    /* column 4 */
    { "CLOCK",  "CLOCK",     4, 0 },
    { "1", "1", 4, 1 }, { "2", "2", 4, 2 }, { "3", "3", 4, 3 }, { "4", "4", 4, 4 }, { "5", "5", 4, 5 },
    { "SONG",   "SONG",      4, 6 },
    { "MERGE",  "MERGE",     4, 7 },
    /* column 5 */
    { "FILTER", "FILTER",    5, 0 },
    { "ECHO",   "ECHO",      5, 1 },
    { "LOOP",   "LOOP",      5, 2 },
    { "QUANT",  "QUANT",     5, 3 },
    { "LENGTH", "LENGTH",    5, 4 },
    { "PART",   "PART",      5, 5 },
    { "EDIT",   "EDIT",      5, 6 },
    { "NAME",   "NAME",      5, 7 },
};

const int mmt8_num_keys = (int)(sizeof(mmt8_keys) / sizeof(mmt8_keys[0]));

const mmt8_key_t *mmt8_key_find(const char *name)
{
    for (int i = 0; i < mmt8_num_keys; i++)
        if (strcasecmp(mmt8_keys[i].name, name) == 0)
            return &mmt8_keys[i];
    return NULL;
}
