/* usnf97fix: platform-neutral parts of the exe patcher. No libc: also built into the DLL. */
#include "patch_core.h"

static pc_u32 rd(const pc_u8 *p) { return (pc_u32)p[0] | (pc_u32)p[1] << 8 | (pc_u32)p[2] << 16 | (pc_u32)p[3] << 24; }

static int cstr_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int sms_parse(SmsMap *m, const pc_u8 *data, pc_u32 size)
{
    pc_u32 n, i;
    if (!data || size < 4) return 0;
    n = rd(data);
    if (n == 0 || n > 1000000u || 4u + n * 8u >= size) return 0;
    m->data = data; m->size = size; m->count = n; m->strBase = 4u + n * 8u;
    for (i = 0; i < n; i++)                       /* every name offset must land in the file */
        if (m->strBase + rd(data + 4 + i * 8) >= size) return 0;
    return 1;
}

pc_u32 sms_find(const SmsMap *m, const char *name)
{
    pc_u32 i;
    if (!m->data) return 0;
    for (i = 0; i < m->count; i++) {
        const pc_u8 *e = m->data + 4 + i * 8;
        pc_u32 off = m->strBase + rd(e);
        pc_u32 k;
        const char *s = (const char *)m->data + off;
        for (k = off; k < m->size && m->data[k]; k++) { }
        if (k >= m->size) continue;                /* unterminated: ignore */
        if (cstr_eq(s, name)) return rd(e + 4);
    }
    return 0;
}

pc_u32 sms_size(const SmsMap *m, pc_u32 addr)
{
    pc_u32 i, best = 0;
    if (!m->data || !addr) return 0;
    for (i = 0; i < m->count; i++) {
        pc_u32 a = rd(m->data + 4 + i * 8 + 4);
        if (a > addr && (!best || a < best)) best = a;
    }
    return best ? best - addr : 0;
}

/* Known layout (USNF '97 1.0):
 *   +00 53 56 57 55          push ebx/esi/edi/ebp
 *   +04 E8 rel32             call readClock          <- patched to call tick_wait
 *   +09 A1 [last]            mov  eax,[last]
 *   +0E 8B 0D [now]          mov  ecx,[now]
 *   +14 3B C1                cmp  eax,ecx
 *   +16 74 EC                je   +04                (spin until the clock moves)
 *   ...  66 83 F9 imm8       cmp  cx,imm8            (the minimum step clamp, 5)
 *   ...  66 8B 0D [comp] 66 81 F9 FF 7F   mov cx,[compression]; cmp cx,7FFFh */
int timeupdate_match(const pc_u8 *c, pc_u32 va, TimePatch *t)
{
    pc_u32 i;
    if (c[0] != 0x53 || c[1] != 0x56 || c[2] != 0x57 || c[3] != 0x55) return 0;
    if (c[4] != 0xE8) return 0;
    if (c[9] != 0xA1) return 0;
    if (c[0x0E] != 0x8B || c[0x0F] != 0x0D) return 0;
    if (c[0x14] != 0x3B || c[0x15] != 0xC1) return 0;
    if (c[0x16] != 0x74 || c[0x17] != 0xEC) return 0;
    t->callSite    = va + 4;
    t->readClockFn = va + 9 + rd(c + 5);
    t->lastAddr    = rd(c + 0x0A);
    t->nowAddr     = rd(c + 0x10);
    t->compressAddr = 0;
    t->hardMinStep = -1;
    for (i = 0x18; i < 0x100; i++) {
        if (t->hardMinStep < 0 && c[i] == 0x66 && c[i + 1] == 0x83 && c[i + 2] == 0xF9) t->hardMinStep = c[i + 3];
        if (!t->compressAddr && c[i] == 0x66 && c[i + 1] == 0x8B && c[i + 2] == 0x0D &&
            c[i + 7] == 0x66 && c[i + 8] == 0x81 && c[i + 9] == 0xF9 && c[i + 10] == 0xFF && c[i + 11] == 0x7F)
            t->compressAddr = rd(c + i + 3);
        if (c[i] == 0xC3) break;                   /* end of function */
    }
    return 1;
}
