static void sys_exit(int status)
{
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(1), "b"(status)
        : "memory");
}

static int sys_write(int fd, const void *buf, unsigned len)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(4), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static int sys_read(int fd, void *buf, unsigned len)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(3), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static void print_hex(unsigned x)
{
    char buf[11]; /* "0x" + 8 hex + "\n" */
    const char *hex = "0123456789abcdef";
    int pos = 10;

    buf[pos] = '\n';
    if (x == 0)
    {
        buf[--pos] = '0';
    }
    else
    {
        while (x)
        {
            buf[--pos] = hex[x & 0xF];
            x >>= 4;
        }
    }
    buf[--pos] = 'x';
    buf[--pos] = '0';

    sys_write(1, buf + pos, (unsigned)(11 - pos));
}

int main(void)
{
    char buf[64]; // read buffer
    unsigned num = 0; // current number being read
    int digit_present = 0; // flag to indicate if at least one digit has been found

    for (;;) {
        int r = sys_read(0, buf, sizeof buf);
        if (r < 0)
            return 1;          // read error
        if (r == 0)
            break;             // EOF

        for (int i = 0; i < r; i++) {
            char c = buf[i];

            if (c >= '0' && c <= '9') {
                // add digit to number
                num = num * 10u + (unsigned)(c - '0');
                digit_present = 1;
            } else {
                // any non-digit = separator
                if (digit_present) {
                    print_hex(num);
                    num = 0;
                    digit_present = 0;
                }
            }
        }
    }

    if (digit_present)
        print_hex(num);

    return 0;
}

// Entry point for the program
void _start(void)
{
    int ret = main();
    sys_exit(ret);
}
