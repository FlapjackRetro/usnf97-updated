/* usnf97fix: draw distance options (opt-in).
 *
 * TerrainDetail=N (2..4): detailed terrain, ground textures and buildings reach N times
 * farther. The terrain is a quadtree rebuilt each frame by T_Make: nodes closer than a
 * size-dependent split distance are split; only nodes split down to the altitude's
 * texture size are textured, and buildings/airfields (T_SortTmapList) attach only to
 * nodes of size <= 4. Everything else is one flat polygon per node.
 *   - the split distance is multiplied by N (stub at "cmp ecx,80h" after it is computed)
 *   - node budget 499 -> 499*N*N: the node array is reached only through a pointer
 *     variable, which is pointed at our larger array; the add (cmp 1F3h) and split
 *     (mov esi,1F0h) limits are raised to match
 *   - the terrain command list (static 10000 bytes, set in T_InitDatabase), the main
 *     view's object list (VIEWInit size 10000) and render_3d's sort list / object
 *     buffer (350 objects) grow by N*N. All of these are bounds-checked by the game
 *     (excess is dropped), so the sizes only decide how much is drawn.
 * Nodes beyond the weather layer's visibility range are still not drawn (fog).
 *
 * ObjectRange=1: ships, aircraft and vehicles are drawn at any distance instead of
 * being dropped beyond their type's view range (T_AddYourObjs). */
#include "shim.h"

#define NODE_SIZE 0x16

static DWORD rd32(DWORD a) { DWORD v; memcpy(&v, (const void *)a, 4); return v; }

static DWORD find_in(DWORD lo, DWORD hi, const BYTE *pat, const BYTE *mask, int n)
{
    DWORD a;
    int i;
    for (a = lo; a + (DWORD)n <= hi; a++) {
        for (i = 0; i < n; i++)
            if ((!mask || mask[i]) && ((const BYTE *)a)[i] != pat[i]) break;
        if (i == n) return a;
    }
    return 0;
}

static DWORD fn_hi(DWORD fn, DWORD dflt)
{
    DWORD n = patches_symbol_size(fn);
    return fn + (n && n < 0x10000 ? n : dflt);
}

static void object_range(void)
{
    /* T_AddYourObjs: ... cmp eax,ebx; jg skip   (distance vs type range) */
    static const BYTE pat[] = { 0x3B, 0xC3, 0x0F, 0x8F };
    static const BYTE nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    DWORD fn = patches_symbol("_T_AddYourObjs@0"), a;
    if (!fn) { logf("object range: T_AddYourObjs not found"); return; }
    a = find_in(fn, fn + 0x120, pat, NULL, 4);
    /* must be the check against the type's range word: movzx/mov bx,[edx+37h] just before */
    if (!a || ((const BYTE *)a)[-4] != 0x66 || ((const BYTE *)a)[-1] != 0x37) { logf("object range: check not as expected; not patched"); return; }
    logf("object range: unlimited (%s)", write_code((void *)(a + 2), nops, 6) ? "ok" : "failed");
}

void terrain_apply(void)
{
    int N = (int)GetPrivateProfileIntA("usnf97fix", "TerrainDetail", 1, g_iniPath);
    DWORD tm, tmHi, addSite, splitSite, distSite, arrVar, cntVar, idb, um, r3d, rHi, gab, gabHi;
    DWORD nodes, cmdSize, objList, sortBytes, objBytes;
    BYTE *nodeBuf, *cmdBuf, *sortBuf, *objBuf, *stub;
    int n = 0;

    if (GetPrivateProfileIntA("usnf97fix", "ObjectRange", 0, g_iniPath)) object_range();
    if (N <= 1) return;
    if (N > 4) N = 4;

    tm = patches_symbol("_T_Make@12");
    idb = patches_symbol("_T_InitDatabase@0");
    um = patches_symbol("?usnfmain@@YAXXZ");
    r3d = patches_symbol("render_3d");
    gab = patches_symbol("_GRAddBrentObj@40");
    if (!tm || !idb || !um || !r3d || !gab) { logf("terrain detail: symbols missing; off"); return; }
    tmHi = fn_hi(tm, 0x1000); rHi = fn_hi(r3d, 0x800); gabHi = fn_hi(gab, 0x400);

    /* add-node: mov eax,[count]; cmp eax,1F3h ... mov eax,[nodeArrayPtr] */
    {
        static const BYTE p[] = { 0xA1, 0,0,0,0, 0x3D, 0xF3, 0x01, 0x00, 0x00 };
        static const BYTE m[] = { 1, 0,0,0,0, 1, 1, 1, 1, 1 };
        DWORD a;
        if (!(a = find_in(tm, tmHi, p, m, 10))) { logf("terrain detail: node add not found; off"); return; }
        cntVar = rd32(a + 1);
        addSite = a + 6;
        for (arrVar = 0, a += 10; a < addSite + 0x30; a++)
            if (((const BYTE *)a)[0] == 0xA1 && rd32(a + 1) != cntVar) { arrVar = rd32(a + 1); break; }
        if (!arrVar) { logf("terrain detail: node array pointer not found; off"); return; }
    }
    {
        static const BYTE p[] = { 0xBE, 0xF0, 0x01, 0x00, 0x00 };                  /* mov esi,1F0h */
        static const BYTE q[] = { 0x81, 0xF9, 0x80, 0x00, 0x00, 0x00, 0x7C };      /* cmp ecx,80h; jl */
        if (!(splitSite = find_in(tm, tmHi, p, NULL, 5)) || find_in(splitSite + 1, tmHi, p, NULL, 5) ||
            !(distSite = find_in(splitSite, splitSite + 0x100, q, NULL, 7))) {
            logf("terrain detail: split code not as expected; off"); return;
        }
    }
    /* T_InitDatabase: mov [cmdStart],imm; mov [cmdEnd],imm */
    {
        static const BYTE p[] = { 0xC7, 0x05, 0,0,0,0, 0,0,0,0, 0xC7, 0x05 };
        static const BYTE m[] = { 1, 1, 0,0,0,0, 0,0,0,0, 1, 1 };
        DWORD a = find_in(idb, idb + 0x20, p, m, 12);
        if (!a || rd32(a + 16) - rd32(a + 6) != 10000) { logf("terrain detail: command list setup not as expected; off"); return; }
        idb = a;
    }
    /* usnfmain: mov edx,2710h; push ecx; mov ecx,offset _mainV */
    {
        BYTE p[11] = { 0xBA, 0x10, 0x27, 0x00, 0x00, 0x51, 0xB9, 0,0,0,0 };
        DWORD mv = patches_symbol("_mainV");
        memcpy(p + 7, &mv, 4);
        if (!mv || !(um = find_in(um, fn_hi(um, 0x2000), p, NULL, 11))) { logf("terrain detail: main view init not found; off"); return; }
    }
    /* render_3d: mov [obj_ptr],objStart; mov [list],sortStart x3   GRAddBrentObj: two limits */
    {
        static const BYTE p[] = { 0xC7, 0x05, 0,0,0,0, 0x50, 0x46, 0x4F, 0x00, 0xC7, 0x05, 0,0,0,0, 0xD8, 0x40, 0x4F, 0x00 };
        static const BYTE m[] = { 1, 1, 0,0,0,0, 1, 1, 1, 1, 1, 1, 0,0,0,0, 1, 1, 1, 1 };
        static const BYTE q[] = { 0x81, 0x3D, 0,0,0,0, 0x50, 0x46, 0x4F, 0x00 };
        static const BYTE qm[] = { 1, 1, 0,0,0,0, 1, 1, 1, 1 };
        static const BYTE w[] = { 0x81, 0xFF, 0xC0, 0x87, 0x4F, 0x00 };
        DWORD a = find_in(r3d, rHi, p, m, 20), b = find_in(gab, gabHi, q, qm, 10), c = find_in(gab, gabHi, w, NULL, 6);
        if (!a || !b || !c || rd32(a + 26) != 0x4F40D8 || rd32(a + 36) != 0x4F40D8) {
            logf("terrain detail: render_3d object lists not as expected; off"); return;
        }
        r3d = a; gab = b; gabHi = c;
    }

    nodes = 0x1F3u * (DWORD)(N * N);
    cmdSize = 10000u * (DWORD)(N * N);
    objList = 10000u * (DWORD)(N * N);
    sortBytes = (0x4F4650u - 0x4F40D8u) * (DWORD)(N * N);
    objBytes = (0x4F87C0u - 0x4F4650u) * (DWORD)(N * N) + 0x40;
    nodeBuf = mem_alloc(nodes * NODE_SIZE + 64);
    cmdBuf = mem_alloc(cmdSize);
    sortBuf = mem_alloc(sortBytes + 16);
    objBuf = mem_alloc(objBytes);
    stub = mem_alloc(32);
    if (!nodeBuf || !cmdBuf || !sortBuf || !objBuf || !stub) { logf("terrain detail: out of memory; off"); return; }

    /* split distance x N:  imul ebp,ebp,N; jno +5; mov ebp,7FFFFFFFh; cmp ecx,80h; ret */
    {
        DWORD old;
        int k = 0;
        stub[k++] = 0x6B; stub[k++] = 0xED; stub[k++] = (BYTE)N;
        stub[k++] = 0x71; stub[k++] = 0x05;
        stub[k++] = 0xBD; stub[k++] = 0xFF; stub[k++] = 0xFF; stub[k++] = 0xFF; stub[k++] = 0x7F;
        stub[k++] = 0x81; stub[k++] = 0xF9; stub[k++] = 0x80; stub[k++] = 0; stub[k++] = 0; stub[k++] = 0;
        stub[k++] = 0xC3;
        if (!VirtualProtect(stub, 32, PAGE_EXECUTE_READWRITE, &old)) { logf("terrain detail: VirtualProtect failed; off"); return; }
        FlushInstructionCache(GetCurrentProcess(), stub, 32);
    }

    /* ---- apply ---- */
    {
        DWORD v;
        BYTE b[6];
        v = (DWORD)nodeBuf; n += write_code((void *)arrVar, &v, 4);
        v = nodes;         n += write_code((void *)addSite, &v, 4);
        v = nodes - 3;     n += write_code((void *)(splitSite + 1), &v, 4);
        b[0] = 0xE8; v = (DWORD)stub - (distSite + 5); memcpy(b + 1, &v, 4); b[5] = 0x90;
        n += write_code((void *)distSite, b, 6);
        v = (DWORD)cmdBuf;           n += write_code((void *)(idb + 6), &v, 4);
        v = (DWORD)cmdBuf + cmdSize; n += write_code((void *)(idb + 16), &v, 4);
        v = objList;                 n += write_code((void *)(um + 1), &v, 4);
        v = (DWORD)objBuf;           n += write_code((void *)(r3d + 6), &v, 4);
        v = (DWORD)sortBuf;          n += write_code((void *)(r3d + 16), &v, 4) + write_code((void *)(r3d + 26), &v, 4) + write_code((void *)(r3d + 36), &v, 4);
        v = (DWORD)sortBuf + sortBytes;        n += write_code((void *)(gab + 6), &v, 4);
        v = (DWORD)objBuf + objBytes - 0x40;   n += write_code((void *)(gabHi + 2), &v, 4);
    }
    logf("terrain detail: %d x split distance, %lu nodes, lists x%d (%d/14 patches)", N, nodes, N * N, n);
}
