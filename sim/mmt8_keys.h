#ifndef MMT8_KEYS_H
#define MMT8_KEYS_H

/*
 * Front-panel button names and their positions in the 6x8 keyboard matrix.
 * Shared by the GUI (button layout) and the script mode (press/release by
 * name). The positions were verified empirically; see sim/README.md.
 */
typedef struct {
    const char *name;    /* script name, e.g. "PLAY", "T1", "PGUP" */
    const char *label;   /* front-panel label for the GUI */
    int col, row;
} mmt8_key_t;

extern const mmt8_key_t mmt8_keys[];
extern const int mmt8_num_keys;

/* Case-insensitive lookup by script name; NULL if unknown. */
const mmt8_key_t *mmt8_key_find(const char *name);

#endif /* MMT8_KEYS_H */
