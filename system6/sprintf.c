#include <stdarg.h>
#include <stdio.h>

static char *put_num(
    char *out,
    unsigned long val,
    int base,
    int neg,
    int width,
    char pad
)
{
    char tmp[12];
    int len = 0;

    do {
        tmp[len++] = "0123456789abcdef"[val % base];
        val /= base;
    } while (val);

    if (neg && pad == '0') {
        *out++ = '-';
        width--;
    } else if (neg) {
        tmp[len++] = '-';
    }

    while (width-- > len) {
        *out++ = pad;
    }
    while (len) {
        *out++ = tmp[--len];
    }
    return out;
}

// minimal non-float replacement sprintf
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    char *out = buf;

    va_start(ap, fmt);
    while (*fmt) {
        char pad = ' ';
        int width = 0;
        const char *s;
        long sval;

        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }
        fmt++;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + *fmt++ - '0';
        }
        if (*fmt == 'l') {
            fmt++;
        }

        switch (*fmt) {
        case 's':
            s = va_arg(ap, const char *);
            while (*s) {
                *out++ = *s++;
            }
            break;
        case 'd':
            sval = va_arg(ap, long);
            if (sval < 0) {
                out = put_num(out, -(unsigned long) sval, 10, 1, width, pad);
            } else {
                out = put_num(out, sval, 10, 0, width, pad);
            }
            break;
        case 'u':
            out = put_num(out, va_arg(ap, unsigned long), 10, 0, width, pad);
            break;
        case 'x':
            out = put_num(out, va_arg(ap, unsigned long), 16, 0, width, pad);
            break;
        case '%':
            *out++ = '%';
            break;
        default:
            // unsupported
            goto done;
        }
        fmt++;
    }

done:
    va_end(ap);
    *out = '\0';
    return out - buf;
}
