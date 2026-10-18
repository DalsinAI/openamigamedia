/* Host tests for openmedia.library's parsers (library/om_probe.c).
 *
 *   test_probe FILE...                  what each file is, one line each
 *   test_probe --check LIST DIR         each file in LIST (samples.txt) against what it should be
 *   test_probe --fuzz DIR [ROUNDS]      every file in DIR cut short and with bytes changed, and
 *                                       made-up hostile headers: each probe must come back, within
 *                                       its read budget (run under AddressSanitizer by run.sh)
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#define _FILE_OFFSET_BITS 64
#define OPENMEDIA_CONSTANTS_ONLY
#include "../../include/openmedia/openmedia.h"
#include "../../library/om_probe.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char scratch[OM_SCRATCH];

/* ---- a file in memory, so it can be cut and changed ---- */

typedef struct mem { const unsigned char *p; uint64_t n; uint64_t read; } mem;

static long mem_read(void *user, uint64_t pos, void *buf, long n)
{
    mem *m = user;
    if (pos >= m->n)
        return 0;
    if ((uint64_t)n > m->n - pos)
        n = (long)(m->n - pos);
    memcpy(buf, m->p + pos, (size_t)n);
    m->read += (uint64_t)n;
    return n;
}

static int probe_mem(const unsigned char *p, uint64_t n, om_info *o, uint32_t budget, uint64_t *read)
{
    mem m = { p, n, 0 };
    om_src s;
    int r;
    memset(&s, 0, sizeof s);
    s.user = &m;
    s.read_at = mem_read;
    s.size = n;
    s.budget = budget;
    r = om_probe(&s, o, scratch);
    if (read)
        *read = m.read;
    return r;
}

static unsigned char *load(const char *path, uint64_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long len;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = malloc(len > 0 ? (size_t)len : 1);
    if (p && len > 0 && fread(p, 1, (size_t)len, f) != (size_t)len) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *n = (uint64_t)(len > 0 ? len : 0);
    return p;
}

static const char *kind_name(uint32_t k)
{
    static const char *const n[] = { "unknown", "picture", "sound", "video", "animation", "music", "text", "document", "other" };
    return k < 9 ? n[k] : "?";
}

static void cc(char *out, uint32_t v)
{
    int i;
    if (!v) {
        strcpy(out, "-");
        return;
    }
    for (i = 0; i < 4; i++) {
        char c = (char)(v >> (24 - 8 * i));
        out[i] = c == ' ' ? '_' : c;
    }
    out[4] = 0;
    for (i = 3; i > 0 && out[i] == '_'; i--)
        out[i] = 0;
}

static void show(const char *name, const om_info *o, int r)
{
    char c[8], v[8], a[8];
    cc(c, o->container);
    cc(v, o->vcodec);
    cc(a, o->acodec);
    printf("%-24s %s %-9s %-4s %-18s v=%-4s %-14s a=%-4s %-16s %ux%u d%u fps=%u/%u fr=%u ch=%u %uHz %ubit %ums %ubps tr=%u/%u/%u/%u fl=%x\n",
           name, r ? "ok" : "--", kind_name(o->kind), c, o->format, v, o->vcodec_name, a, o->acodec_name,
           o->width, o->height, o->depth, o->fps_num, o->fps_den, o->frames, o->channels, o->rate, o->bits,
           o->duration_ms, o->bitrate, o->vtracks, o->atracks, o->stracks, o->otracks, o->flags);
}

/* ---- --check: samples.txt says what each file is ---- */

static uint32_t code_of(const char *s)
{
    char b[4] = { ' ', ' ', ' ', ' ' };
    int i;
    if (!strcmp(s, "-"))
        return 0;
    for (i = 0; i < 4 && s[i]; i++)
        b[i] = s[i] == '_' ? ' ' : s[i];
    return OM_ID(b[0], b[1], b[2], b[3]);
}

static int kind_of(const char *s)
{
    int k;
    for (k = 0; k < 9; k++)
        if (!strcmp(s, kind_name((uint32_t)k)))
            return k;
    return -1;
}

static int failures, checks;

static void want_u(const char *file, const char *what, const char *field, uint32_t got, uint32_t tol)
{
    unsigned long w;
    char *e;
    if (!strcmp(field, "*"))
        return;
    checks++;
    w = strtoul(field, &e, 10);
    if (*e || (got + tol < w) || (got > w + tol)) {
        printf("FAIL %s: %s is %u, wanted %s%s\n", file, what, got, field, tol ? " (+/- tolerance)" : "");
        failures++;
    }
}

static void want_c(const char *file, const char *what, const char *field, uint32_t got)
{
    char g[8];
    if (!strcmp(field, "*"))
        return;
    checks++;
    if (code_of(field) != got) {
        cc(g, got);
        printf("FAIL %s: %s is %s, wanted %s\n", file, what, g, field);
        failures++;
    }
}

static int check(const char *list, const char *dir)
{
    FILE *f = fopen(list, "r");
    char line[512];
    int files = 0;
    if (!f) {
        perror(list);
        return 2;
    }
    while (fgets(line, sizeof line, f)) {
        /* file kind container vcodec acodec width height depth fps channels rate bits duration tolerance */
        char fld[14][64], path[1024];
        int n;
        unsigned char *p;
        uint64_t size;
        om_info o;
        int r, k;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        n = sscanf(line, "%63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s", fld[0], fld[1], fld[2],
                   fld[3], fld[4], fld[5], fld[6], fld[7], fld[8], fld[9], fld[10], fld[11], fld[12], fld[13]);
        if (n != 14) {
            printf("FAIL %s: a line with %d fields\n", list, n);
            failures++;
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, fld[0]);
        if (!(p = load(path, &size))) {
            printf("SKIP %s: not made here\n", fld[0]);
            continue;
        }
        files++;
        r = probe_mem(p, size, &o, OM_BUDGET_DEFAULT, NULL);
        show(fld[0], &o, r);
        checks++;
        if (!r || (k = kind_of(fld[1])) < 0 || (uint32_t)k != o.kind) {
            printf("FAIL %s: kind is %s, wanted %s\n", fld[0], kind_name(o.kind), fld[1]);
            failures++;
        }
        want_c(fld[0], "container", fld[2], o.container);
        want_c(fld[0], "video codec", fld[3], o.vcodec);
        want_c(fld[0], "audio codec", fld[4], o.acodec);
        want_u(fld[0], "width", fld[5], o.width, 0);
        want_u(fld[0], "height", fld[6], o.height, 0);
        want_u(fld[0], "depth", fld[7], o.depth, 0);
        if (strcmp(fld[8], "*")) {
            /* frames a second, as a thousandth: "25" or "29.97" */
            double want = atof(fld[8]);
            uint32_t got1000 = o.fps_den ? (uint32_t)((uint64_t)o.fps_num * 1000 / o.fps_den) : 0;
            checks++;
            if ((double)got1000 < want * 1000 - 20 || (double)got1000 > want * 1000 + 20) {
                printf("FAIL %s: frames a second %u/%u, wanted %s\n", fld[0], o.fps_num, o.fps_den, fld[8]);
                failures++;
            }
        }
        want_u(fld[0], "channels", fld[9], o.channels, 0);
        want_u(fld[0], "rate", fld[10], o.rate, 0);
        want_u(fld[0], "bits", fld[11], o.bits, 0);
        want_u(fld[0], "duration", fld[12], o.duration_ms, (uint32_t)strtoul(fld[13], NULL, 10));
        /* With the least budget (64 KB, all of it the first read), a format whose details are
         * near its start still gives them all: the parsers' rereads of the first 64 KB are free. */
        if (size >= OM_BUDGET_MIN) {
            static const char *const head_formats[] = { "WAVE", "AIFF", "AIFC", "8SVX", "16SV", "ILBM", "PNG",
                                                        "JPEG", "GIF", "BMP", "FLAC", "MP3", "ADTS", "WEBP" };
            om_info m;
            unsigned j;
            for (j = 0; j < sizeof head_formats / sizeof head_formats[0]; j++)
                if (code_of(head_formats[j]) == o.container)
                    break;
            if (j < sizeof head_formats / sizeof head_formats[0]) {
                probe_mem(p, size, &m, OM_BUDGET_MIN, NULL);
                checks++;
                if (m.kind != o.kind || m.container != o.container || m.width != o.width || m.height != o.height ||
                    m.depth != o.depth || m.channels != o.channels || m.rate != o.rate || m.bits != o.bits) {
                    printf("FAIL %s: with a 64 KB budget it says less:\n", fld[0]);
                    show(fld[0], &m, 1);
                    failures++;
                }
            }
        }
        free(p);
    }
    fclose(f);
    printf("%d files, %d checks, %d failed\n", files, checks, failures);
    return failures ? 1 : 0;
}

/* ---- --fuzz ---- */

static uint32_t seed = 12345;
static uint32_t rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static int fuzz_one(const char *what, const unsigned char *p, uint64_t n, uint32_t budget)
{
    om_info o;
    uint64_t read;
    probe_mem(p, n, &o, budget, &read);
    if (read > (budget < OM_BUDGET_MIN ? OM_BUDGET_MIN : budget)) {
        printf("FAIL %s: read %llu bytes with a budget of %u\n", what, (unsigned long long)read, budget);
        return 1;
    }
    return 0;
}

static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

/* Headers made to hurt: sizes that point past the end or nowhere, nesting as deep as it goes,
 * counts in the millions, lengths of zero. */
static int hostile(void)
{
    static unsigned char b[1 << 20];
    int bad = 0, i;
    /* MP4: moov inside moov inside moov..., then a box of size 1 with a 64-bit size of 2^63 */
    memset(b, 0, sizeof b);
    for (i = 0; i < 4000; i++) {
        put32(b + i * 8, (uint32_t)(sizeof b - i * 8));
        memcpy(b + i * 8 + 4, i % 2 ? "trak" : "moov", 4);
    }
    bad += fuzz_one("nested moov", b, sizeof b, OM_BUDGET_DEFAULT);
    memset(b, 0, 64);
    put32(b, 24);
    memcpy(b + 4, "ftypisom", 8);
    put32(b + 24, 1);
    memcpy(b + 28, "moov", 4);
    put32(b + 32, 0x80000000u);
    bad += fuzz_one("mp4 64-bit size", b, 4096, OM_BUDGET_DEFAULT);
    /* MP4: thousands of zero-length-data boxes (size 8) */
    for (i = 0; i < 100000; i++) {
        put32(b + i * 8, 8);
        memcpy(b + i * 8 + 4, "free", 4);
    }
    bad += fuzz_one("mp4 many boxes", b, 800000, OM_BUDGET_DEFAULT);
    /* Matroska: EBML header, a segment of unknown size, then elements of size 0 forever */
    memset(b, 0, sizeof b);
    memcpy(b, "\x1A\x45\xDF\xA3\x84\x42\x82\x81\x77\x18\x53\x80\x67\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 21);
    for (i = 21; i + 2 < (int)sizeof b; i += 2) {
        b[i] = 0xEC;                            /* Void, size 0 */
        b[i + 1] = 0x80;
    }
    bad += fuzz_one("mkv voids", b, sizeof b, OM_BUDGET_DEFAULT);
    /* Matroska: Tracks inside Tracks (the parser treats nested IDs by parent, not by recursion) */
    memset(b, 0, sizeof b);
    memcpy(b, "\x1A\x45\xDF\xA3\x80\x18\x53\x80\x67\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 17);
    for (i = 17; i + 8 < (int)sizeof b; i += 8)
        memcpy(b + i, "\x16\x54\xAE\x6B\x01\xFF\xFF\xFF", 8);
    bad += fuzz_one("mkv tracks", b, sizeof b, OM_BUDGET_DEFAULT);
    /* Ogg: pages with 255 segments of 255 bytes, every one a BOS page */
    memset(b, 0, sizeof b);
    for (i = 0; i + 27 + 255 < 300000; i += 27 + 255) {
        memcpy(b + i, "OggS", 4);
        b[i + 5] = 2;
        b[i + 26] = 255;
        memset(b + i + 27, 255, 255);
    }
    bad += fuzz_one("ogg lacing", b, 300000, OM_BUDGET_DEFAULT);
    /* RIFF: a chunk of size 0xFFFFFFFF, and AVI lists inside lists */
    memset(b, 0, sizeof b);
    memcpy(b, "RIFF\xFF\xFF\xFF\xFF" "AVI ", 12);
    for (i = 12; i + 12 < (int)sizeof b; i += 12)
        memcpy(b + i, "LIST\xFF\xFF\xFF\x7Fhdrl", 12);
    bad += fuzz_one("avi lists", b, sizeof b, OM_BUDGET_DEFAULT);
    /* ID3 tag saying it is bigger than the file, then nothing */
    memset(b, 0, sizeof b);
    memcpy(b, "ID3\x04\x00\x00\x7F\x7F\x7F\x7F", 10);
    bad += fuzz_one("id3 huge", b, 100, OM_BUDGET_DEFAULT);
    /* MPEG-TS: every packet a PAT with a pointer field of 255 and section length 4095 */
    memset(b, 0, sizeof b);
    for (i = 0; i + 188 <= (int)sizeof b; i += 188) {
        b[i] = 0x47;
        b[i + 1] = 0x40;
        b[i + 3] = 0x10;
        b[i + 4] = (i / 188) % 2 ? 255 : 0;
        b[i + 5] = 0;
        b[i + 6] = 0x0F;
        b[i + 7] = 0xFF;
    }
    bad += fuzz_one("ts pat", b, sizeof b, OM_BUDGET_DEFAULT);
    /* MPEG-PS: pack headers and PES lengths of 0xFFFF */
    memset(b, 0, sizeof b);
    for (i = 0; i + 20 <= (int)sizeof b; i += 20)
        memcpy(b + i, "\x00\x00\x01\xBA\x44\x00\x04\x00\x04\x01\x00\x00\x03\xF8\x00\x00\x01\xC0\xFF\xFF", 20);
    bad += fuzz_one("ps", b, sizeof b, OM_BUDGET_DEFAULT);
    /* IFF ANIM with 100000 empty FORMs */
    memset(b, 0, sizeof b);
    memcpy(b, "FORM\x00\x0F\xFF\xF0" "ANIM", 12);
    for (i = 12; i + 12 <= (int)sizeof b; i += 12)
        memcpy(b + i, "FORM\x00\x00\x00\x04ILBM", 12);
    bad += fuzz_one("anim", b, sizeof b, OM_BUDGET_DEFAULT);
    /* H.264: an SPS of 0xFF bytes (exp-Golomb codes that never end) */
    memset(b, 0xFF, 4096);
    memcpy(b, "\x00\x00\x00\x01\x67", 5);
    bad += fuzz_one("sps", b, 4096, OM_BUDGET_DEFAULT);
    memset(b, 0, 4096);
    memcpy(b, "\x00\x00\x00\x01\x67\x64\x00\x1f", 8);
    bad += fuzz_one("sps zeros", b, 4096, OM_BUDGET_DEFAULT);
    /* random bytes */
    for (i = 0; i < 400; i++) {
        uint32_t n = rnd() % 70000, k;
        for (k = 0; k < n; k++)
            b[k] = (unsigned char)rnd();
        bad += fuzz_one("random", b, n, OM_BUDGET_DEFAULT);
    }
    return bad;
}

static void on_alarm(int sig)
{
    (void)sig;
    static const char m[] = "FAIL: a probe took more than 20 seconds (a hang)\n";
    if (write(1, m, sizeof m - 1) < 0)
        _exit(3);
    _exit(3);
}

static int fuzz(const char *dir, int rounds)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int bad = 0, files = 0, probes = 0;
    if (!d) {
        perror(dir);
        return 2;
    }
    signal(SIGALRM, on_alarm);
    alarm(20);
    bad += hostile();
    probes += 420;
    while ((e = readdir(d))) {
        char path[1024], what[1100];
        uint64_t n, cut;
        unsigned char *p, *q;
        int r;
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (!(p = load(path, &n)) || !n)
            continue;
        files++;
        q = malloc((size_t)n);
        /* cut short: every length up to 64 bytes, then every 61st, then a few near the end */
        for (cut = 0; cut <= n; cut = cut < 64 ? cut + 1 : cut + 61 + (cut / 4096) * 61) {
            alarm(20);
            snprintf(what, sizeof what, "%s cut to %llu", e->d_name, (unsigned long long)cut);
            bad += fuzz_one(what, p, cut, OM_BUDGET_DEFAULT);
            probes++;
        }
        for (cut = n > 64 ? n - 64 : 0; cut < n; cut++) {
            bad += fuzz_one(e->d_name, p, cut, OM_BUDGET_DEFAULT);
            probes++;
        }
        /* bytes changed: a few at random, in the first 4 KB mostly, where the headers are */
        for (r = 0; r < rounds; r++) {
            int k, flips = 1 + (int)(rnd() % 8);
            memcpy(q, p, (size_t)n);
            for (k = 0; k < flips; k++) {
                uint64_t at = rnd() % 4 ? rnd() % (n < 4096 ? n : 4096) : rnd() % n;
                q[at] = rnd() % 3 ? (unsigned char)(q[at] ^ (1u << (rnd() % 8))) : (unsigned char)rnd();
            }
            alarm(20);
            snprintf(what, sizeof what, "%s changed (round %d)", e->d_name, r);
            bad += fuzz_one(what, q, n, r % 5 ? OM_BUDGET_DEFAULT : OM_BUDGET_MIN);
            probes++;
        }
        /* every AC-3 and E-AC-3 sync frame near the start with the reserved sample-rate
         * codes (fscod 3, and for E-AC-3 fscod2 3 too) */
        memcpy(q, p, (size_t)n);
        for (cut = 0, r = 0; cut + 8 <= n && cut < 65536; cut++)
            if (q[cut] == 0x0B && q[cut + 1] == 0x77) {
                q[cut + 4] |= 0xF0;
                r++;
            }
        if (r) {
            alarm(20);
            snprintf(what, sizeof what, "%s with reserved AC-3 rates", e->d_name);
            bad += fuzz_one(what, q, n, OM_BUDGET_DEFAULT);
            probes++;
        }
        free(q);
        free(p);
    }
    closedir(d);
    alarm(0);
    printf("fuzz: %d files, %d probes, %d failed\n", files, probes, bad);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    int i;
    if (argc >= 4 && !strcmp(argv[1], "--check"))
        return check(argv[2], argv[3]);
    if (argc >= 3 && !strcmp(argv[1], "--fuzz"))
        return fuzz(argv[2], argc >= 4 ? atoi(argv[3]) : 300);
    for (i = 1; i < argc; i++) {
        uint64_t n, read;
        unsigned char *p = load(argv[i], &n);
        om_info o;
        int r;
        const char *base = strrchr(argv[i], '/');
        if (!p) {
            perror(argv[i]);
            continue;
        }
        r = probe_mem(p, n, &o, OM_BUDGET_DEFAULT, &read);
        show(base ? base + 1 : argv[i], &o, r);
        free(p);
    }
    return 0;
}
