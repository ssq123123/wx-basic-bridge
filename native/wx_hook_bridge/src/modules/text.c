#include "../include/wx_bridge_text.h"

void wxb_trim_trailing_crlf(char *s) {
    int n;
    if (!s) return;
    n = lstrlenA(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

void wxb_i64toa10(int64_t v, char *out) {
    char tmp[32];
    int neg = 0, i = 0, j = 0;
    uint64_t x;
    if (v < 0) { neg = 1; x = (uint64_t)(-(v + 1)) + 1; }
    else x = (uint64_t)v;
    if (x == 0) tmp[i++] = '0';
    while (x) {
        tmp[i++] = (char)('0' + (x % 10));
        x /= 10;
    }
    if (neg) out[j++] = '-';
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

void wxb_append_raw(char **p, char *end, const char *s) {
    while (*s && *p < end) *(*p)++ = *s++;
}

void wxb_append_escaped(char **p, char *end, const char *s) {
    const unsigned char *u = (const unsigned char *)s;
    while (*u && *p < end - 8) {
        unsigned char c = *u++;
        if (c == '\\' || c == '"') {
            *(*p)++ = '\\';
            *(*p)++ = (char)c;
        } else if (c == '\n') {
            *(*p)++ = '\\'; *(*p)++ = 'n';
        } else if (c == '\r') {
            *(*p)++ = '\\'; *(*p)++ = 'r';
        } else if (c == '\t') {
            *(*p)++ = '\\'; *(*p)++ = 't';
        } else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            *(*p)++ = '\\'; *(*p)++ = 'u'; *(*p)++ = '0'; *(*p)++ = '0';
            *(*p)++ = hex[(c >> 4) & 0xF];
            *(*p)++ = hex[c & 0xF];
        } else {
            *(*p)++ = (char)c;
        }
    }
}

int wxb_str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int contains(const char *s, const char *needle) {
    const char *p, *n, *h;
    if (!s || !needle || !*needle) return 0;
    for (p = s; *p; p++) {
        h = p;
        n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

int wxb_is_chatroom_id(const char *s) {
    return contains(s, "@chatroom");
}

int wxb_is_wxid_like(const char *s) {
    return s && s[0] == 'w' && s[1] == 'x' && s[2] == 'i' && s[3] == 'd' && s[4] == '_';
}

void wxb_safe_copy(char *dst, int max, const char *src) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) src = "";
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int wxb_extract_group_sender(const char *content, char *sender, int max) {
    int i = 0;
    if (!content || !sender || max <= 0) return 0;
    sender[0] = 0;
    if (!wxb_is_wxid_like(content)) return 0;
    while (content[i] && content[i] != ':' && content[i] != '\r' && content[i] != '\n' && i < max - 1) {
        sender[i] = content[i];
        i++;
    }
    if (content[i] != ':') {
        sender[0] = 0;
        return 0;
    }
    sender[i] = 0;
    return i > 0;
}
