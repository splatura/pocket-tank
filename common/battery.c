/* battery.c — the battery page's history and estimates (battery.h). */
#include "battery.h"
#include <stdio.h>
#include <string.h>

#define DRAIN_MIN_AWAKE_S (20 * 60)     /* a stretch teaches the drain rate after 20 awake min ... */
#define DRAIN_MIN_DROP    4             /* ... and 4% lost (the gauge moves in 1% steps) */
#define CHARGE_MIN_S      (10 * 60)     /* a charging run teaches the charge rate after 10 min ... */
#define CHARGE_MIN_RISE   5             /* ... and 5% gained */
#define SAVE_EVERY_S      600.0f

static void begin(bat_t *b, int64_t now, int pct, bool pw) {
    bat_hist_t *h = &b->h;
    h->since_unix = now; h->since_pct = (int16_t)pct; h->on_power = pw;
    h->awake_s = 0; h->drop_pct = 0;
    b->low = -1; b->dirty = true;
}

/* a new measurement folds into the learned one half and half: a single
   odd stretch (a bright afternoon, a night of re-asks) moves it, never owns it */
static uint16_t learn(uint16_t old_x10, float measured, float lo, float hi) {
    if (measured < lo) measured = lo;
    if (measured > hi) measured = hi;
    float v = old_x10 ? (old_x10 / 10.0f + measured) * 0.5f : measured;
    return (uint16_t)(v * 10 + 0.5f);
}

void battery_init(bat_t *b, const bat_hist_t *saved) {
    memset(b, 0, sizeof *b);
    if (saved && saved->magic == BAT_HIST_MAGIC) b->h = *saved;
    else { b->h.magic = BAT_HIST_MAGIC; b->h.since_pct = -1; }   /* -1: no stretch begun yet */
    b->low = -1; b->state = -1; b->chg_pct = -1;
}

void battery_woke(bat_t *b) { b->low = -1; }

int battery_tick(bat_t *b, int64_t now, float dt, int pct, int state) {
    if (pct < 0 || pct > 100) return 0;
    bat_hist_t *h = &b->h;
    bool pw = BAT_ON_POWER(state);
    int edge = 0;
    if (h->since_pct < 0) {                                          /* a fresh history: this tick starts the first stretch */
        begin(b, now, pct, pw);
        edge = pw ? 1 : 0;
    } else if (pw != (bool)h->on_power) {                            /* the cable moved (or moved while the tank slept) */
        if (!h->on_power && h->awake_s >= DRAIN_MIN_AWAKE_S && h->drop_pct >= DRAIN_MIN_DROP)
            h->drain_x10 = learn(h->drain_x10, h->drop_pct / (h->awake_s / 3600.0f), 5, 300);
        begin(b, now, pct, pw);
        edge = pw ? 1 : -1;
    }
    /* charging runs: from the first CHARGING tick to the last (full, or unplugged) */
    if (state == BAT_CHARGING) {
        if (b->chg_pct < 0 && now > 0) { b->chg_pct = pct; b->chg_unix = now; }
    } else if (b->chg_pct >= 0) {
        if (now - b->chg_unix >= CHARGE_MIN_S && pct - b->chg_pct >= CHARGE_MIN_RISE)
            { h->charge_x10 = learn(h->charge_x10, (pct - b->chg_pct) / ((now - b->chg_unix) / 3600.0f), 10, 400); b->dirty = true; }
        b->chg_pct = -1;
    }
    /* screen-on time, and what the gauge lost during it */
    if (dt > 0) {
        b->save_s += dt;
        b->awake_frac += dt;                                         /* whole seconds in the blob */
        int whole = (int)b->awake_frac; b->awake_frac -= whole; h->awake_s += (uint32_t)whole;
    }
    if (!pw) {
        if (b->low < 0) b->low = pct;
        else if (pct < b->low) { h->drop_pct = (uint16_t)(h->drop_pct + b->low - pct); b->low = pct; }
    } else b->low = -1;
    if (b->save_s >= SAVE_EVERY_S) { b->save_s = 0; b->dirty = true; }
    b->state = state;
    return edge;
}

bool battery_take_save(bat_t *b) { bool d = b->dirty; b->dirty = false; if (d) b->save_s = 0; return d; }

static int round5(float m) { int v = (int)(m + 2.5f) / 5 * 5; return v < 0 ? 0 : v; }

void battery_info(const bat_t *b, int64_t now, int pct, int mv, int state, bat_info_t *o) {
    const bat_hist_t *h = &b->h;
    bool pw = BAT_ON_POWER(state);
    o->pct = pct; o->mv = mv; o->state = state;
    /* the drain: this cell's learned rate (or the default), and once this
       stretch has run long enough to say, mostly this stretch - it knows
       today's brightness and today's fish */
    float drain = h->drain_x10 ? h->drain_x10 / 10.0f : BAT_DRAIN_DEFAULT;
    o->measured = h->drain_x10 != 0;
    if (!pw && !h->on_power && h->awake_s >= 15 * 60 && h->drop_pct >= 3) {
        float cur = h->drop_pct / (h->awake_s / 3600.0f);
        drain = o->measured ? (drain + 2 * cur) / 3 : cur;
        o->measured = true;
    }
    o->life_min = round5(100 / drain * 60);
    o->left_min = !pw && pct >= 0 ? round5(pct / drain * 60) : -1;
    if (state == BAT_CHARGING && pct >= 0) {
        float chg = h->charge_x10 ? h->charge_x10 / 10.0f : BAT_CHARGE_DEFAULT;
        if (b->chg_pct >= 0 && now - b->chg_unix >= 10 * 60 && pct - b->chg_pct >= 3) {
            float cur = (pct - b->chg_pct) / ((now - b->chg_unix) / 3600.0f);
            chg = h->charge_x10 ? (chg + 2 * cur) / 3 : cur;
        }
        int m = round5((100 - pct) / chg * 60);
        o->left_min = m < 5 ? 5 : m;                                 /* the last percent or two take their time */
    }
    o->since_min = now > 0 && h->since_unix > 0 && now >= h->since_unix ? (int)((now - h->since_unix) / 60) : -1;
    o->awake_min = !pw && !h->on_power ? (int)(h->awake_s / 60) : -1;
}

void battery_fmt_dur(char *buf, int n, int min) {
    if (min < 0) snprintf(buf, n, "-");
    else if (min < 60) snprintf(buf, n, "%dM", min);
    else if (min < 24 * 60) {
        if (min % 60) snprintf(buf, n, "%dH %dM", min / 60, min % 60);
        else snprintf(buf, n, "%dH", min / 60);
    } else {
        int d = min / 1440, hh = min % 1440 / 60;
        if (hh) snprintf(buf, n, "%dD %dH", d, hh); else snprintf(buf, n, "%dD", d);
    }
}

static const struct { int mv; float f; } LIPO[] = {           /* a single cell at rest, 0..100% (a common 1C-rested table) */
    { 3300, 0.00f }, { 3610, 0.05f }, { 3690, 0.10f }, { 3710, 0.15f }, { 3730, 0.20f }, { 3750, 0.25f },
    { 3770, 0.30f }, { 3790, 0.35f }, { 3800, 0.40f }, { 3820, 0.45f }, { 3840, 0.50f }, { 3850, 0.55f },
    { 3870, 0.60f }, { 3910, 0.65f }, { 3950, 0.70f }, { 3980, 0.75f }, { 4020, 0.80f }, { 4080, 0.85f },
    { 4110, 0.90f }, { 4150, 0.95f }, { 4200, 1.00f },
};
float battery_lipo_frac(int mv) {
    const int n = (int)(sizeof LIPO / sizeof LIPO[0]);
    if (mv <= LIPO[0].mv) return 0.0f;
    if (mv >= LIPO[n - 1].mv) return 1.0f;
    for (int i = 1; i < n; i++)
        if (mv <= LIPO[i].mv) return LIPO[i - 1].f + (LIPO[i].f - LIPO[i - 1].f) * (float)(mv - LIPO[i - 1].mv) / (float)(LIPO[i].mv - LIPO[i - 1].mv);
    return 1.0f;
}
int battery_median_mv(const int *mv, int n) {
    int s[16]; if (n > 16) n = 16;
    for (int i = 0; i < n; i++) s[i] = mv[i];
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && s[j - 1] > s[j]; j--) { int t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
    return n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
}
int battery_hyst_pct(int shown_pct, float frac) {
    int p = (int)(frac * 100.0f + 0.5f);
    if (shown_pct < 0 || p >= shown_pct + 2 || p <= shown_pct - 2) return p;
    return shown_pct;
}
