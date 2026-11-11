/* usnf97fix: platform-neutral parts of the exe patcher (unit-tested on the host). */
#ifndef PATCH_CORE_H
#define PATCH_CORE_H

typedef unsigned int  pc_u32;
typedef unsigned char pc_u8;

typedef struct {
    const pc_u8 *data;
    pc_u32       size;
    pc_u32       count;
    pc_u32       strBase;
} SmsMap;

/* USNF.SMS: u32 count; count x {u32 nameOffset, u32 address}; NUL-terminated names. */
int    sms_parse(SmsMap *m, const pc_u8 *data, pc_u32 size);
pc_u32 sms_find(const SmsMap *m, const char *name);      /* 0 if missing */
pc_u32 sms_size(const SmsMap *m, pc_u32 addr);           /* distance to next symbol, 0 if unknown */
/* Name of the closest symbol at or below addr (NULL if none); *base gets its address. */
const char *sms_nearest(const SmsMap *m, pc_u32 addr, pc_u32 *base);

typedef struct {
    pc_u32 callSite;      /* VA of "call readClock" inside TIMEUpdate */
    pc_u32 readClockFn;   /* VA of the clock-sampling helper */
    pc_u32 lastAddr;      /* VA of the "time at last frame" global */
    pc_u32 nowAddr;       /* VA of the "time now" global */
    pc_u32 compressAddr;  /* VA of the time-compression shift (0 if not found) */
    int    hardMinStep;   /* the game's hard-coded minimum step, -1 if not found */
} TimePatch;

/* code: bytes of TIMEUpdate; va: its address. Returns 1 if the layout is the known one. */
int timeupdate_match(const pc_u8 *code, pc_u32 va, TimePatch *out);

#endif
