// C4 slice 1 — zoneC GaiaMod module export.
#pragma once

extern const unsigned char zcmod_bin[];
extern const unsigned long zcmod_len;

extern const unsigned char zcsay_bin[];
extern const unsigned long zcsay_len;

extern const unsigned char zcaot_bin[];
extern const unsigned long zcaot_len;
extern const unsigned long zcaot_entry;
void zc_kputs(const char *s);
long zc_resolve(const char *name);
void zcaot_link(void);
extern void (*zcaot_entry_fn)(void);

// C4 slice 4 — GaiaPlot: HolyC draws magenta rect + yellow diagonals on ramfb.
extern const unsigned char zcplot_bin[];
extern const unsigned long zcplot_len;
void zcplot_link(void);
extern void (*zcplot_entry_fn)(void);
