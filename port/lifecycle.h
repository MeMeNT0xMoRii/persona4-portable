#ifndef P4_LIFECYCLE_H
#define P4_LIFECYCLE_H

int p4_lifecycle_init(void);

/* Serialize card commands with HOME exit. A rejected command must not touch disk. */
#ifdef __psp__
int p4_card_io_begin(void);
void p4_card_io_end(void);
void p4_lifecycle_report(void);
#else
static inline int p4_card_io_begin(void) { return 1; }
static inline void p4_card_io_end(void) { }
#endif

#endif
