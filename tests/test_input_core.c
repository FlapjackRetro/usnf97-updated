#include <stdio.h>
#include <string.h>
#include "../src/input_core.h"
static int fails;
#define CHECK(c, ...) do { if (c) printf("ok   " __VA_ARGS__); else { printf("FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)
int main(void)
{
    InDevInfo d[3]; InDevState st; InSrc s; KeySpec k;
    memset(d, 0, sizeof d); memset(&st, 0, sizeof st);
    strcpy(d[0].name, "Honeycomb Echo Aviation Controller"); d[0].axisMask = 0xff; d[0].nButtons = 32; d[0].nPovs = 1; d[0].connected = 1;
    strcpy(d[1].name, "Thrustmaster T.Flight Rudder Pedals"); d[1].axisMask = 0x23; d[1].connected = 1;
    strcpy(d[2].name, "XInput pad 1"); d[2].isPad = 1; d[2].padIndex = 1; d[2].axisMask = 0x3f; d[2].nButtons = 10; d[2].nPovs = 1; d[2].connected = 1;

    CHECK(insrc_parse("Echo:X", &s) && insrc_resolve(&s, d, 3) && s.dev == 0 && s.kind == SRC_AXIS && s.idx == 0, "Echo:X");
    CHECK(insrc_parse("echo : s1~", &s) && insrc_resolve(&s, d, 3) && s.idx == 6 && s.invert, "echo : s1~ (spaces, case, invert)");
    CHECK(insrc_parse("pedal:RZ", &s) && insrc_resolve(&s, d, 3) && s.dev == 1 && s.idx == 5, "pedal:RZ");
    CHECK(insrc_parse("#2:Z", &s) && insrc_resolve(&s, d, 3) && s.dev == 1, "#2 = second DirectInput device");
    CHECK(insrc_parse("pad:RT-LT", &s) && insrc_resolve(&s, d, 3) && s.kind == SRC_AXISPAIR && s.idx == 5 && s.idx2 == 2, "pad:RT-LT pair");
    CHECK(insrc_parse("pad:X", &s) && insrc_resolve(&s, d, 3) && s.kind == SRC_BUTTON && s.idx == 2, "pad:X is a button on pads");
    CHECK(insrc_parse("pad1:DPADL", &s) && insrc_resolve(&s, d, 3) && s.kind == SRC_POVDIR && s.dir == 3, "pad1:DPADL");
    CHECK(insrc_parse("Echo:B12", &s) && insrc_resolve(&s, d, 3) && s.kind == SRC_BUTTON && s.idx == 11, "Echo:B12");
    CHECK(insrc_parse("Echo:POV1U", &s) && insrc_resolve(&s, d, 3) && s.kind == SRC_POVDIR && s.dir == 0, "Echo:POV1U");
    CHECK(insrc_parse("Echo:S2>80", &s) && insrc_resolve(&s, d, 3) && s.kind == SRC_AXIS_GT && s.threshPct == 80, "Echo:S2>80");
    CHECK(!insrc_parse("nocolon", &s), "rejects missing ':'");
    CHECK(insrc_parse("Saitek:X", &s) && !insrc_resolve(&s, d, 3), "absent device does not resolve");
    CHECK(insrc_parse("Echo:B200", &s) && !insrc_resolve(&s, d, 3), "button out of range");

    /* evaluation */
    insrc_parse("pad:RT-LT", &s); insrc_resolve(&s, d, 3);
    st.ax[5] = 65535; st.ax[2] = 0; CHECK(insrc_axis(&s, &st) > 65000, "RT only -> full right");
    st.ax[5] = 0; st.ax[2] = 65535; CHECK(insrc_axis(&s, &st) < 600, "LT only -> full left");
    st.ax[5] = st.ax[2] = 0; CHECK(insrc_axis(&s, &st) == 32768, "both released -> centre");
    insrc_parse("Echo:S2>80", &s); insrc_resolve(&s, d, 3);
    st.ax[7] = 60000; CHECK(insrc_pressed(&s, &st), "axis 92%% > 80");
    st.ax[7] = 40000; CHECK(!insrc_pressed(&s, &st), "axis 61%% not > 80");
    insrc_parse("Echo:POV1R", &s); insrc_resolve(&s, d, 3);
    st.pov[0] = 4500; CHECK(insrc_pressed(&s, &st), "POV 45 deg presses right");
    st.pov[0] = 0xFFFFFFFFu; CHECK(!insrc_pressed(&s, &st), "centred POV");
    CHECK(in_deadzone(32768 + 1000, 5) == 32768, "inside deadzone -> centre");
    CHECK(in_deadzone(65535, 5) == 65535 && in_deadzone(0, 5) == 0, "ends still reachable (%d)", in_deadzone(0, 5));

    /* keys */
    CHECK(keyspec_parse("g", &k) && k.scan == 0x22 && k.mods == 0, "g");
    CHECK(keyspec_parse("E", &k) && k.scan == 0x12 && k.mods == 1, "E = Shift+e");
    CHECK(keyspec_parse("Gear", &k) && k.scan == 0x22, "alias Gear");
    CHECK(keyspec_parse("Eject", &k) && k.scan == 0x12 && k.mods == 1, "alias Eject");
    CHECK(keyspec_parse("Ctrl+P", &k) && k.scan == 0x19 && k.mods == 3, "Ctrl+P (uppercase P adds shift: %d)", k.mods);
    CHECK(keyspec_parse("Ctrl+p", &k) && k.scan == 0x19 && k.mods == 2, "Ctrl+p");
    CHECK(keyspec_parse("Pause", &k) && k.mods == 2, "alias Pause = Ctrl+p");
    CHECK(keyspec_parse("Shift+[", &k) && k.scan == 0x1a && k.mods == 1, "Shift+[");
    CHECK(keyspec_parse("{", &k) && k.scan == 0x1a && k.mods == 1, "{ = Shift+[");
    CHECK(keyspec_parse("Num+", &k) && k.scan == 0x4e && k.mods == 0, "Num+");
    CHECK(keyspec_parse("+", &k) && k.scan == 0x0d && k.mods == 1, "+ = Shift+=");
    CHECK(keyspec_parse("PgDn", &k) && k.scan == 0x51, "PgDn");
    CHECK(keyspec_parse("F12", &k) && k.scan == 0x58, "F12");
    CHECK(!keyspec_parse("Hyper+x", &k), "unknown modifier rejected");
    CHECK(!keyspec_parse("NoSuchKey", &k), "unknown key rejected");
    { int i, bad = 0; for (i = 0; g_inAliases[i].name; i++) if (!keyspec_parse(g_inAliases[i].key, &k)) { printf("     bad alias %s\n", g_inAliases[i].name); bad++; }
      CHECK(!bad, "all %d aliases parse", i); }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
