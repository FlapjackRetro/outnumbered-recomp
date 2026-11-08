// AdLib/OPL2 (ymfm) + PC speaker mixer. See audio.h.
#include "audio.h"
#include "ymfm_opl.h"
#include <mutex>
#include <deque>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>

namespace {

static const uint32_t OPL_CLOCK = 3579545;

static double now_us()
{
    using namespace std::chrono;
    return (double)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// Guest code busy-waits on status reads (AdLib detection reads the status port ~100 times to wait out
// the 80 us timer); the recompiled code runs those loops far faster than ISA I/O, so every status read
// also advances a virtual clock by ~3 us.
static double g_skew_us = 0;
static double timer_now_us() { return now_us() + g_skew_us; }

// ymfm interface: the chip asks for two timers (80 us / 320 us ticks) that set the status flags the
// game's AdLib detection and some music drivers poll. Expiry is checked against the host clock.
class Iface : public ymfm::ymfm_interface
{
public:
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override
    {
        if (tnum > 1) return;
        m_deadline[tnum] = duration_in_clocks < 0 ? 0.0 : timer_now_us() + duration_in_clocks * 1e6 / OPL_CLOCK;
    }
    void poll_timers()
    {
        double t = timer_now_us();
        for (uint32_t i = 0; i < 2; i++)
            if (m_deadline[i] != 0.0 && t >= m_deadline[i]) { m_deadline[i] = 0.0; m_engine->engine_timer_expired(i); }
    }
private:
    double m_deadline[2] = { 0.0, 0.0 };
};

struct SpkEv { double t; bool on; unsigned div; uint8_t p61; };

// Tandy 1000 / PCjr sound: TI SN76496 on port C0h, 3.579545 MHz.
// Three square-wave voices (10-bit period, freq = clock / (32 * N)), one noise voice
// (15-bit LFSR, taps 0 and 1), 4-bit attenuation in 2 dB steps (15 = off).
struct Sn76496 {
    static constexpr double CLOCK = 3579545.0;
    unsigned period[4] = { 1024, 1024, 1024, 0x10 };
    unsigned count[4]  = { 1, 1, 1, 1 };
    unsigned vol[4]    = { 15, 15, 15, 15 };
    int      out[4]    = { 1, 1, 1, 1 };
    unsigned lfsr = 0x4000, noise_ctl = 0, latch = 0;
    double   acc = 0;
    int      amp[16];
    Sn76496() { for (int i = 0; i < 16; i++) amp[i] = i == 15 ? 0 : (int)(5400.0 * pow(10.0, -0.1 * i)); }
    void noise_period() { unsigned r = noise_ctl & 3; period[3] = r == 3 ? period[2] : (0x10u << r); }
    void write(uint8_t v)
    {
        if (v & 0x80) {
            latch = (v >> 4) & 7;
            unsigned ch = latch >> 1;
            if (latch & 1) vol[ch] = v & 15;
            else if (ch == 3) { noise_ctl = v & 7; lfsr = 0x4000; noise_period(); }
            else { period[ch] = (period[ch] & 0x3F0) | (v & 15); if (!period[ch]) period[ch] = 1024; if (ch == 2) noise_period(); }
        } else {
            unsigned ch = latch >> 1;
            if (latch & 1) vol[ch] = v & 15;
            else if (ch == 3) { noise_ctl = v & 7; lfsr = 0x4000; noise_period(); }
            else { period[ch] = (period[ch] & 15) | ((unsigned)(v & 0x3F) << 4); if (!period[ch]) period[ch] = 1024; if (ch == 2) noise_period(); }
        }
    }
    // one output sample: run the chip's /16 clock for the sample's duration, box-filtered
    int sample(int rate)
    {
        acc += CLOCK / 16.0 / rate;
        int ticks = (int)acc; acc -= ticks;
        if (ticks <= 0) ticks = 1;
        long sum = 0;
        for (int t = 0; t < ticks; t++) {
            for (int ch = 0; ch < 4; ch++) {
                if (--count[ch] == 0) {
                    count[ch] = period[ch] ? period[ch] : 1024;
                    if (ch < 3) out[ch] ^= 1;
                    else {
                        out[3] ^= 1;
                        if (out[3]) {                      // LFSR shifts on the rising edge of the noise clock
                            unsigned fb = (noise_ctl & 4) ? ((lfsr ^ (lfsr >> 1)) & 1) : (lfsr & 1);
                            lfsr = (lfsr >> 1) | (fb << 14);
                        }
                    }
                }
            }
            int s = 0;
            for (int ch = 0; ch < 3; ch++) if (period[ch] > 1) s += out[ch] ? amp[vol[ch]] : -amp[vol[ch]];   // N<=1: DC, inaudible
            s += (lfsr & 1) ? amp[vol[3]] : -amp[vol[3]];
            sum += s;
        }
        return (int)(sum / ticks);
    }
};
struct TgaEv { double t; uint8_t v; };

struct Audio {
    std::mutex mu;
    // PC speaker changes are timestamped (host clock) and applied at the exact sample, ~50 ms behind real time,
    // so notes that start/stop inside one audio block are not clipped or dropped.
    std::deque<SpkEv> spk_ev; bool spk_on_now = false; unsigned spk_div_now = 0; uint8_t p61_mux_now = 0;
    std::deque<TgaEv> tga_ev; Sn76496 tga; bool tga_used = false;
    int peak = 0; unsigned long rendered = 0;
    bool tandy = false;                  // machine has the Tandy audio mux (set when the game picks the SN76496)
    bool log = getenv("SSO_AUDIOLOG") != nullptr;
    double play_base = 0, played = 0; bool play_init = false;
    Iface iface;
    ymfm::ym3812 chip { iface };
    uint32_t chip_rate = 0;
    uint8_t  opl_addr = 0;
    // resampler state (linear interpolation from the chip rate)
    double   frac = 0; int32_t prev = 0, cur = 0;
    // PC speaker
    unsigned pit2_div = 0, pit2_lo = 0; int pit2_phase = 0, pit2_ctl = 0;
    uint8_t  p61 = 0; double spk_phase = 0;
    // WAV dump
    FILE *wav = nullptr; int wav_rate = 0; double wav_last = 0; uint32_t wav_bytes = 0;
    Audio() { chip.reset(); chip_rate = chip.sample_rate(OPL_CLOCK); }
};
Audio *g = nullptr;

inline int32_t chip_sample()
{
    ymfm::ym3812::output_data o;
    g->chip.generate(&o, 1);
    return o.data[0];
}

void spk_event()                                      // call (locked) after any change to p61 / pit2_div
{
    // Tandy 1000 / PCjr: port 61h bits 5-6 are the audio multiplexer (00 = 8253 timer
    // channel 2, 11 = SN76496). With the mux on the sound chip the PIT tone is not heard.
    bool mux_pit = !g->tandy || ((g->p61 >> 5) & 3) == 0;
    SpkEv e{ now_us(), mux_pit && (g->p61 & 3) == 3 && g->pit2_div > 1, g->pit2_div, g->p61 };
    if (g->log) fprintf(stderr, "[spk] p61=%02X div=%u -> %s\n", g->p61, g->pit2_div, e.on ? "ON" : "off");
    if (!g->spk_ev.empty() && g->spk_ev.back().on == e.on && g->spk_ev.back().div == e.div && ((g->spk_ev.back().p61 ^ e.p61) & 0x60) == 0) return;
    g->spk_ev.push_back(e);
    if (g->spk_ev.size() > 4096) g->spk_ev.pop_front();
}

void render_locked(int16_t *out, int frames, int rate)
{
    g->iface.poll_timers();
    double step = (double)g->chip_rate / rate;
    const double delay_us = 50000.0;
    double now = now_us();
    if (!g->play_init || fabs(g->play_base + g->played * 1e6 / rate - (now - delay_us)) > 60000.0) {
        g->play_base = now - delay_us; g->played = 0; g->play_init = true;       // (re)lock the playhead to the host clock
    }
    double t0 = g->play_base + g->played * 1e6 / rate;
    for (int i = 0; i < frames; i++) {
        double tt = t0 + i * 1e6 / rate;
        while (!g->spk_ev.empty() && g->spk_ev.front().t <= tt) { g->spk_on_now = g->spk_ev.front().on; g->spk_div_now = g->spk_ev.front().div; g->p61_mux_now = g->spk_ev.front().p61; g->spk_ev.pop_front(); }
        while (!g->tga_ev.empty() && g->tga_ev.front().t <= tt) { g->tga.write(g->tga_ev.front().v); g->tga_ev.pop_front(); }
        bool spk_on = g->spk_on_now;
        double spk_inc = spk_on ? (1193182.0 / g->spk_div_now) / rate : 0;
        g->frac += step;
        while (g->frac >= 1.0) { g->prev = g->cur; g->cur = chip_sample(); g->frac -= 1.0; }
        double s = g->prev + (g->cur - g->prev) * g->frac;           // OPL: ~+-32767 full scale
        s *= 0.8;
        if (spk_on) { g->spk_phase += spk_inc; if (g->spk_phase >= 1.0) g->spk_phase -= 1.0; s += g->spk_phase < 0.5 ? 5000 : -5000; }
        if (g->tga_used) { int ts = g->tga.sample(rate); if (((g->p61_mux_now >> 5) & 3) == 3) s += ts; }
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        { int a = s < 0 ? -(int)s : (int)s; if (a > g->peak) g->peak = a; }
        out[2 * i] = out[2 * i + 1] = (int16_t)s;
    }
    g->played += frames; g->rendered += frames;
}

} // namespace

extern "C" {

static int speaker_only(void)      // SSO_AUDIO=speaker: hide the AdLib so the game falls back to the PC speaker
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("SSO_AUDIO"); v = e && (e[0] == 's' || e[0] == 'S' || e[0] == 'p' || e[0] == 'P'); }
    return v;
}

void audio_init(void) { if (!g) g = new Audio(); }

int audio_port_out(uint16_t port, uint8_t v)
{
    if (!g) return 0;
    if (speaker_only() && (port == 0x388 || port == 0x389)) return 1;      // no AdLib present
    std::lock_guard<std::mutex> l(g->mu);
    switch (port) {
    case 0x388: { static int lg = -1; if (lg < 0) lg = getenv("SSO_AUDIOLOG") != NULL; if (lg) fprintf(stderr, "[opl] addr %02X\n", v); } g->opl_addr = v; g->chip.write_address(v); return 1;
    case 0x389: { static int lg = -1; if (lg < 0) lg = getenv("SSO_AUDIOLOG") != NULL; if (lg) fprintf(stderr, "[opl] data %02X\n", v); } g->chip.write_data(v); return 1;
    case 0x43:
        if ((v >> 6) == 2) { g->pit2_ctl = 1; g->pit2_phase = 0; } else if ((v >> 6) != 3) g->pit2_ctl = 0;
        return 0;                                      // the PIT model in the runtime still sees channel 0
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:   // Tandy SN76496
        g->tga_used = true;
        if (g->log) fprintf(stderr, "[tga] %.1f %02X\n", now_us() / 1000.0, v);
        g->tga_ev.push_back(TgaEv{ now_us(), v });
        if (g->tga_ev.size() > 16384) g->tga_ev.pop_front();
        return 1;
    case 0x42:
        if (!g->pit2_ctl) return 1;
        if (g->pit2_phase == 0) { g->pit2_lo = v; g->pit2_phase = 1; }
        else { unsigned d = g->pit2_lo | ((unsigned)v << 8); g->pit2_div = d ? d : 65536; g->pit2_phase = 0; spk_event(); }
        return 1;
    }
    return 0;
}

int audio_port_in(uint16_t port, uint8_t *v)
{
    if (!g) return 0;
    if (port == 0x388 && speaker_only()) { *v = 0xFF; return 1; }          // floating bus: detection fails
    if (port == 0x388) {
        std::lock_guard<std::mutex> l(g->mu);
        g_skew_us += 3.0;
        g->iface.poll_timers();
        *v = g->chip.read_status(); return 1;
    }
    return 0;
}

void audio_set_tandy(int on) { if (g) { std::lock_guard<std::mutex> l(g->mu); g->tandy = on != 0; } }
int audio_take_peak(unsigned long *rendered)
{
    if (!g) return 0;
    std::lock_guard<std::mutex> l(g->mu);
    int p = g->peak; g->peak = 0;
    if (rendered) { *rendered = g->rendered; }
    return p;
}
void audio_set_port61(uint8_t v) { if (g) { std::lock_guard<std::mutex> l(g->mu); g->p61 = v; spk_event(); } }

void audio_render(int16_t *stereo, int frames, int rate)
{
    if (!g) { memset(stereo, 0, (size_t)frames * 4); return; }
    std::lock_guard<std::mutex> l(g->mu);
    render_locked(stereo, frames, rate);
}

// ---- WAV dump (SSO_WAV=file): audio for the elapsed host time, appended from recomp_tick ----
void audio_wav_open(const char *path, int rate)
{
    if (!g) return;
    g->wav = fopen(path, "wb"); g->wav_rate = rate; g->wav_last = now_us(); g->wav_bytes = 0;
    if (g->wav) { uint8_t h[44] = { 0 }; fwrite(h, 1, 44, g->wav); }
}
void audio_wav_feed(void)
{
    if (!g || !g->wav) return;
    double t = now_us(); double dt = t - g->wav_last;
    if (dt < 20000) return;
    int frames = (int)(dt * g->wav_rate / 1e6); g->wav_last += frames * 1e6 / g->wav_rate;
    static int16_t buf[48000 * 2];
    if (frames > 48000) frames = 48000;
    std::lock_guard<std::mutex> l(g->mu);
    render_locked(buf, frames, g->wav_rate);
    fwrite(buf, 4, (size_t)frames, g->wav); g->wav_bytes += (uint32_t)frames * 4;
}
void audio_wav_close(void)
{
    if (!g || !g->wav) return;
    uint8_t h[44]; uint32_t rate = (uint32_t)g->wav_rate, br = rate * 4, n = g->wav_bytes, riff = 36 + n, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    memcpy(h, "RIFF", 4); memcpy(h + 4, &riff, 4); memcpy(h + 8, "WAVEfmt ", 8); memcpy(h + 16, &fmt, 4);
    memcpy(h + 20, &pcm, 2); memcpy(h + 22, &ch, 2); memcpy(h + 24, &rate, 4); memcpy(h + 28, &br, 4);
    memcpy(h + 32, &ba, 2); memcpy(h + 34, &bits, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &n, 4);
    fseek(g->wav, 0, SEEK_SET); fwrite(h, 1, 44, g->wav); fclose(g->wav); g->wav = nullptr;
}

} // extern "C"
