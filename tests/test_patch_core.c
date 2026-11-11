/* Host test: run the DLL's patch matcher against a real USNF.EXE + USNF.SMS pair. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/patch_core.h"

static unsigned char *slurp(const char *p, unsigned *n)
{
    FILE *f = fopen(p, "rb"); unsigned char *b; long s;
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); s = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(s + 1); fread(b, 1, s, f); fclose(f); *n = (unsigned)s; return b;
}
static unsigned rd(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* map a PE file to a flat image so VAs can be dereferenced */
static unsigned char *map_pe(unsigned char *f, unsigned *base, unsigned *entry)
{
    unsigned nt = rd(f + 0x3c), nsec = f[nt + 6] | f[nt + 7] << 8, opt = nt + 24;
    unsigned soh = f[nt + 20] | f[nt + 21] << 8, isz = rd(f + opt + 56), i;
    unsigned char *img = calloc(1, isz);
    *base = rd(f + opt + 28); *entry = *base + rd(f + opt + 16);
    memcpy(img, f, rd(f + opt + 60));
    for (i = 0; i < nsec; i++) {
        unsigned char *s = f + opt + soh + i * 40;
        unsigned va = rd(s + 12), rs = rd(s + 16), ro = rd(s + 20);
        memcpy(img + va, f + ro, rs);
    }
    return img;
}

int main(int argc, char **argv)
{
    unsigned en, sn, base, entry, fails = 0;
    unsigned char *exe = slurp(argv[1], &en), *sms = slurp(argv[2], &sn), *img;
    SmsMap m; TimePatch t;
    unsigned cd, tu, winmain;
    img = map_pe(exe, &base, &entry);
#define CHECK(c, ...) do { if (c) printf("ok   " __VA_ARGS__); else { printf("FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)
    CHECK(sms_parse(&m, sms, sn), "sms_parse (%u symbols)", m.count);
    winmain = sms_find(&m, "_WinMainCRTStartup");
    CHECK(winmain == entry, "SMS matches exe: _WinMainCRTStartup %08x vs entry %08x", winmain, entry);
    cd = sms_find(&m, "?CDPATH@@3PADA");
    CHECK(cd != 0 && img[cd - base] == 0, "CDPATH at %08x, size %u, initially empty", cd, sms_size(&m, cd));
    tu = sms_find(&m, "_TIMEUpdate@0");
    CHECK(tu && timeupdate_match(img + (tu - base), tu, &t), "TIMEUpdate at %08x matches", tu);
    printf("     callSite %08x readClock %08x last %08x now %08x compress %08x hardMinStep %d\n",
           t.callSite, t.readClockFn, t.lastAddr, t.nowAddr, t.compressAddr, t.hardMinStep);
    if (argc > 3) {   /* expected values for the 1.0 build, from the disassembly */
        CHECK(t.callSite == 0x40e624 && t.readClockFn == 0x40e770 && t.lastAddr == 0x4fde50 &&
              t.nowAddr == 0x4fddfc && t.compressAddr == 0x4fde08 && t.hardMinStep == 5, "1.0 expected addresses");
    }
    CHECK(sms_find(&m, "no_such_symbol") == 0, "missing symbol returns 0");
    { unsigned char bad[16] = {0}; CHECK(!sms_parse(&m, bad, sizeof bad), "rejects empty SMS"); }
    printf(fails ? "\n%u FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
