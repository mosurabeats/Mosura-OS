/* Mosura OS core: 1-bit graphics for the SSD1309 and a tiny libc. MIT. */
#include "internal.h"
#include "font3x5.h"

uint8_t g_fb[MOS_FB_BYTES];

/* ---- libc --------------------------------------------------------------- */
void *mos_memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
void *mos_memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d; const uint8_t *q = s;
    while (n--) *p++ = *q++;
    return d;
}
/* The compiler may emit calls to these for struct copies. */
void *memset(void *d, int c, size_t n) { return mos_memset(d, c, n); }
void *memcpy(void *d, const void *s, size_t n) { return mos_memcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *p = d; const uint8_t *q = s;
    if (p < q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
int mos_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
void mos_strcpy(char *d, const char *s, int max)
{
    int i = 0;
    for (; s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = 0;
}

/* printf subset: %d %u %x %s %c %%, flags '-' '0', width. */
int mos_vfmt(char *out, int max, const char *f, va_list ap)
{
    int n = 0;
#define PUT(ch) do { if (n < max - 1) out[n] = (ch); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int left = 0, zero = 0, width = 0, plus = 0;
        for (;; f++) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else if (*f == '+') plus = 1;
            else break;
        }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        char buf[16]; int len = 0; const char *s = buf;
        switch (*f) {
        case 'd': case 'u': case 'x': {
            long v = (*f == 'u' || *f == 'x') ? (long)va_arg(ap, unsigned)
                                               : (long)va_arg(ap, int);
            int base = *f == 'x' ? 16 : 10, neg = v < 0;
            unsigned long u = neg ? -v : v;
            char tmp[16]; int t = 0;
            do { tmp[t++] = "0123456789ABCDEF"[u % base]; u /= base; } while (u);
            if (neg) buf[len++] = '-'; else if (plus) buf[len++] = '+';
            if (zero && !left) while (len + t < width) buf[len++] = '0';
            while (t) buf[len++] = tmp[--t];
            break;
        }
        case 's': s = va_arg(ap, const char *); if (!s) s = ""; len = mos_strlen(s); break;
        case 'c': buf[0] = (char)va_arg(ap, int); len = 1; break;
        case '%': buf[0] = '%'; len = 1; break;
        default: buf[0] = '?'; len = 1; break;
        }
        if (!left) while (width-- > len) PUT(' ');
        for (int i = 0; i < len; i++) PUT(s[i]);
        if (left) while (width-- > len) PUT(' ');
        if (!*f) break;
    }
    if (max > 0) out[n < max ? n : max - 1] = 0;
    return n;
#undef PUT
}
int mos_fmt(char *out, int max, const char *f, ...)
{
    va_list ap; va_start(ap, f);
    int n = mos_vfmt(out, max, f, ap);
    va_end(ap);
    return n;
}

/* ---- pixels ------------------------------------------------------------- */
void g_clear(void) { mos_memset(g_fb, 0, sizeof g_fb); }

void g_px(int x, int y, int c)
{
    if ((unsigned)x >= MOS_W || (unsigned)y >= MOS_H) return;
    uint8_t *b = &g_fb[(y >> 3) * MOS_W + x], m = (uint8_t)(1 << (y & 7));
    if (c == C_ON) *b |= m; else if (c == C_OFF) *b &= (uint8_t)~m; else *b ^= m;
}
void g_hline(int x0, int x1, int y, int c) { for (int x = x0; x <= x1; x++) g_px(x, y, c); }
void g_vline(int x, int y0, int y1, int c) { for (int y = y0; y <= y1; y++) g_px(x, y, c); }
void g_dots(int x0, int x1, int y, int step) { for (int x = x0; x <= x1; x += step) g_px(x, y, C_ON); }
void g_vdots(int x, int y0, int y1, int step) { for (int y = y0; y <= y1; y += step) g_px(x, y, C_ON); }

void g_line(int x0, int y0, int x1, int y1, int c)
{
    int dx = ABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -ABS(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        g_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
void g_fill(int x, int y, int w, int h, int c)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) g_px(x + i, y + j, c);
}
void g_frame(int x, int y, int w, int h, int c)
{
    g_hline(x, x + w - 1, y, c); g_hline(x, x + w - 1, y + h - 1, c);
    g_vline(x, y + 1, y + h - 2, c); g_vline(x + w - 1, y + 1, y + h - 2, c);
}
void g_rframe(int x, int y, int w, int h, int c)
{
    g_hline(x + 1, x + w - 2, y, c); g_hline(x + 1, x + w - 2, y + h - 1, c);
    g_vline(x, y + 1, y + h - 2, c); g_vline(x + w - 1, y + 1, y + h - 2, c);
}
void g_rfill(int x, int y, int w, int h, int c)
{
    g_fill(x + 1, y, w - 2, h, c);
    g_vline(x, y + 1, y + h - 2, c); g_vline(x + w - 1, y + 1, y + h - 2, c);
}
void g_dither(int x, int y, int w, int h, int level)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int X = x + i, Y = y + j, on;
            if (level == 1) on = ((X & 1) == 0) && ((Y & 1) == 0);
            else if (level == 2) on = ((X + Y) & 1) == 0;
            else on = !(((X & 1) == 0) && ((Y & 1) == 0));
            if (on) g_px(X, Y, C_ON);
        }
}

/* ---- text --------------------------------------------------------------- */
static int glyph(int x, int y, int ch, int flags)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 0 || ch >= 96) ch = '?';
    uint16_t g = font3x5[ch];
    int s = (flags & T_X2) ? 2 : 1;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (g & (1 << (r * 3 + c))) {
                if (s == 1) g_px(x + c, y + r, (flags & T_INV) ? C_OFF : C_ON);
                else g_fill(x + c * 2, y + r * 2, 2, 2, (flags & T_INV) ? C_OFF : C_ON);
            }
    return 4 * s;
}
int g_textw(const char *s, int flags)
{
    int n = mos_strlen(s);
    return n ? n * ((flags & T_X2) ? 8 : 4) - ((flags & T_X2) ? 2 : 1) : 0;
}
int g_text(int x, int y, const char *s, int flags)
{
    int w = g_textw(s, flags);
    if (flags & T_RIGHT) x -= w;
    if (flags & T_CENTER) x -= w / 2;
    if (flags & T_INV) g_fill(x - 1, y - 1, w + 2, (flags & T_X2) ? 12 : 7, C_ON);
    for (; *s; s++) x += glyph(x, y, (unsigned char)*s, flags);
    return w;
}
int g_textf(int x, int y, int flags, const char *f, ...)
{
    char b[48]; va_list ap; va_start(ap, f);
    mos_vfmt(b, sizeof b, f, ap); va_end(ap);
    return g_text(x, y, b, flags);
}

/* Seven-segment numerals, h pixels tall (odd, >= 9). dp = digit index
 * after which a decimal point is drawn (-1 none). */
static void seg_digit(int x, int y, int d, int w, int h)
{
    static const uint8_t segs[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    uint8_t s = segs[d % 10];
    int m = h / 2;
    if (s & 0x01) g_hline(x + 1, x + w - 2, y, C_ON), g_hline(x + 2, x + w - 3, y + 1, C_ON);
    if (s & 0x40) g_hline(x + 1, x + w - 2, y + m, C_ON);
    if (s & 0x08) g_hline(x + 1, x + w - 2, y + h - 1, C_ON), g_hline(x + 2, x + w - 3, y + h - 2, C_ON);
    if (s & 0x20) g_vline(x, y + 1, y + m - 1, C_ON), g_vline(x + 1, y + 2, y + m - 1, C_ON);
    if (s & 0x02) g_vline(x + w - 1, y + 1, y + m - 1, C_ON), g_vline(x + w - 2, y + 2, y + m - 1, C_ON);
    if (s & 0x10) g_vline(x, y + m + 1, y + h - 2, C_ON), g_vline(x + 1, y + m + 1, y + h - 3, C_ON);
    if (s & 0x04) g_vline(x + w - 1, y + m + 1, y + h - 2, C_ON), g_vline(x + w - 2, y + m + 1, y + h - 3, C_ON);
}
void g_bignum(int x, int y, int value, int digits, int dp, int h)
{
    int w = h / 2 + 1, d[8];
    for (int i = digits - 1; i >= 0; i--) { d[i] = value % 10; value /= 10; }
    int lead = 1;
    for (int i = 0; i < digits; i++) {
        if (d[i] == 0 && lead && i < digits - 1 && (dp < 0 || i < dp)) { x += w + 2; continue; }
        lead = 0;
        seg_digit(x, y, d[i], w, h);
        x += w + 2;
        if (i == dp) { g_fill(x - 1, y + h - 2, 2, 2, C_ON); x += 3; }
    }
}

/* A soft-key label cell in the footer. */
void g_box_label(int x, int y, int w, const char *s, int on)
{
    if (on) g_rfill(x, y, w, 8, C_ON);
    else g_rframe(x, y, w, 8, C_ON);
    g_text(x + w / 2, y + 2, s, T_CENTER | (on ? T_INV : 0));
    if (on) { /* g_text's inverse box is one pixel wider; restore rounding */
        g_px(x, y, C_OFF); g_px(x + w - 1, y, C_OFF);
        g_px(x, y + 7, C_OFF); g_px(x + w - 1, y + 7, C_OFF);
    }
}
