/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <snickerbockers@washemu.org> wrote this file.  As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 *
 * snickerbockers
 * ----------------------------------------------------------------------------
 */

#include "pvr2_mem_test.h"

static int state;

#define SPG_HBLANK_INT (*(unsigned volatile*)0xa05f80c8)
#define SPG_VBLANK_INT (*(unsigned volatile*)0xa05f80cc)
#define SPG_CONTROL    (*(unsigned volatile*)0xa05f80d0)
#define SPG_HBLANK     (*(unsigned volatile*)0xa05f80d4)
#define SPG_LOAD       (*(unsigned volatile*)0xa05f80d8)
#define SPG_VBLANK     (*(unsigned volatile*)0xa05f80dc)
#define SPG_WIDTH      (*(unsigned volatile*)0xa05f80e0)

#define VO_CONTROL     (*(unsigned volatile*)0xa05f80e8)
#define VO_STARTX      (*(unsigned volatile*)0xa05f80ec)
#define VO_STARTY      (*(unsigned volatile*)0xa05f80f0)

#define FB_R_CTRL      (*(unsigned volatile*)0xa05f8044)
#define FB_R_SOF1      (*(unsigned volatile*)0xa05f8050)
#define FB_R_SOF2      (*(unsigned volatile*)0xa05f8054)
#define FB_R_SIZE      (*(unsigned volatile*)0xa05f805c)

// basic framebuffer parameters
#define LINESTRIDE_PIXELS  640
#define BYTES_PER_PIXEL      2
#define FRAMEBUFFER_WIDTH  640
#define FRAMEBUFFER_HEIGHT 476

// TODO: I want to implement double-buffering and VBLANK interrupts, but I don't have that yet.
#define FRAMEBUFFER_1 ((void volatile*)0xa5200000)
#define FRAMEBUFFER_2 ((void volatile*)0xa5600000)

#define FB_R_SOF1_FRAME1 0x00200000
#define FB_R_SOF2_FRAME1 0x00200500
#define FB_R_SOF1_FRAME2 0x00600000
#define FB_R_SOF2_FRAME2 0x00600500

void *get_romfont_pointer(void);

int test_single_addr_double(unsigned offs);

/*
 * Performs a write using the store-queues.
 * src must be 8-byte aligned.
 * dst must be 32-byte aligned.
 */
void write_sq(void const *src, void volatile *dst);

static unsigned get_controller_buttons(void);
static int check_controller(void);

static void volatile *cur_framebuffer;

static void configure_video(void) {
    // Hardcoded for 640x476i NTSC video
    SPG_HBLANK_INT = 0x03450000;
    SPG_VBLANK_INT = 0x00150104;
    SPG_CONTROL = 0x00000150;
    SPG_HBLANK = 0x007E0345;
    SPG_LOAD = 0x020C0359;
    SPG_VBLANK = 0x00240204;
    SPG_WIDTH = 0x07d6c63f;
    VO_CONTROL = 0x00160000;
    VO_STARTX = 0x000000a4;
    VO_STARTY = 0x00120012;
    FB_R_CTRL = 0x00000004;
    FB_R_SOF1 = FB_R_SOF1_FRAME1;
    FB_R_SOF2 = FB_R_SOF2_FRAME1;
    FB_R_SIZE = 0x1413b53f;

    cur_framebuffer = FRAMEBUFFER_1;
}

void disable_video(void) {
    FB_R_CTRL &= ~1;
}

void enable_video(void) {
    FB_R_CTRL |= 1;
}

void clear_screen(void volatile* fb, unsigned short color) {
    unsigned color_2pix = ((unsigned)color) | (((unsigned)color) << 16);

    unsigned volatile *row_ptr = (unsigned volatile*)fb;

    unsigned row, col;
    for (row = 0; row < FRAMEBUFFER_HEIGHT; row++)
        for (col = 0; col < (FRAMEBUFFER_WIDTH / 2); col++)
            *row_ptr++ = color_2pix;
}

unsigned short make_color(unsigned red, unsigned green, unsigned blue) {
    if (red > 255)
        red = 255;
    if (green > 255)
        green = 255;
    if (blue > 255)
        blue = 255;

    red >>= 3;
    green >>= 2;
    blue >>= 3;

    return blue | (green << 5) | (red << 11);
}

static void
create_font(unsigned short *font,
            unsigned short foreground, unsigned short background) {
    get_romfont_pointer();
    char const *romfont = get_romfont_pointer();

    unsigned glyph;
    for (glyph = 0; glyph < 288; glyph++) {
        unsigned short *glyph_out = font + glyph * 24 * 12;
        char const *glyph_in = romfont + (12 * 24 / 8) * glyph;

        unsigned row, col;
        for (row = 0; row < 24; row++) {
            for (col = 0; col < 12; col++) {
                unsigned idx = row * 12 + col;
                char const *inp = glyph_in + idx / 8;
                char mask = 0x80 >> (idx % 8);
                unsigned short *outp = glyph_out + idx;
                if (*inp & mask)
                    *outp = foreground;
                else
                    *outp = background;
            }
        }
    }
}

#define MAX_CHARS_X (FRAMEBUFFER_WIDTH / 12)
#define MAX_CHARS_Y (FRAMEBUFFER_HEIGHT / 24)

static void draw_glyph(void volatile *fb, unsigned short const *font,
                       unsigned glyph_no, unsigned x, unsigned y) {
    if (glyph_no > 287)
        glyph_no = 0;
    unsigned short volatile *outp = ((unsigned short volatile*)fb) +
        y * LINESTRIDE_PIXELS + x;
    unsigned short const *glyph = font + glyph_no * 24 * 12;

    unsigned row;
    for (row = 0; row < 24; row++) {
        unsigned col;
        for (col = 0; col < 12; col++) {
            outp[col] = glyph[row * 12 + col];
        }
        outp += LINESTRIDE_PIXELS;
    }
}

static void draw_char(void volatile *fb, unsigned short const *font,
                      char ch, unsigned row, unsigned col) {
    if (row >= MAX_CHARS_Y || col >= MAX_CHARS_X)
        return;

    unsigned x = col * 12;
    unsigned y = row * 24;

    unsigned glyph;
    if (ch >= 33 && ch <= 126)
        glyph = ch - 33 + 1;
    else
        return;

    draw_glyph(fb, font, glyph, x, y);
}

void drawstring(void volatile *fb, unsigned short const *font,
                char const *msg, unsigned row, unsigned col) {
    while (*msg) {
        if (col >= MAX_CHARS_X) {
            col = 0;
            row++;
        }
        if (*msg == '\n') {
            col = 0;
            row++;
            msg++;
            continue;
        }
        draw_char(fb, font, *msg++, row, col++);
    }
}

#define REG_ISTNRM (*(unsigned volatile*)0xA05F6900)

void swap_buffers(void) {
    if (cur_framebuffer == FRAMEBUFFER_1) {
        FB_R_SOF1 = FB_R_SOF1_FRAME2;
        FB_R_SOF2 = FB_R_SOF2_FRAME2;
        cur_framebuffer = FRAMEBUFFER_2;
    } else {
        FB_R_SOF1 = FB_R_SOF1_FRAME1;
        FB_R_SOF2 = FB_R_SOF2_FRAME1;
        cur_framebuffer = FRAMEBUFFER_1;
    }
}

void volatile *get_backbuffer(void) {
    if (cur_framebuffer == FRAMEBUFFER_1)
        return FRAMEBUFFER_2;
    else
        return FRAMEBUFFER_1;
}

static void volatile *get_frontbuffer(void) {
    return cur_framebuffer;
}

int check_vblank(void) {
    int ret = (REG_ISTNRM & (1 << 3)) ? 1 : 0;
    if (ret)
        REG_ISTNRM = (1 << 3);
    return ret;
}

#define REG_MDSTAR (*(unsigned volatile*)0xa05f6c04)
#define REG_MDTSEL (*(unsigned volatile*)0xa05f6c10)
#define REG_MDEN   (*(unsigned volatile*)0xa05f6c14)
#define REG_MDST   (*(unsigned volatile*)0xa05f6c18)
#define REG_MSYS   (*(unsigned volatile*)0xa05f6c80)
#define REG_MDAPRO (*(unsigned volatile*)0xa05f6c8c)
#define REG_MMSEL  (*(unsigned volatile*)0xa05f6ce8)

static void volatile *align32(void volatile *inp) {
    char volatile *as_ch = (char volatile*)inp;
    while (((unsigned)as_ch) & 31)
        as_ch++;
    return (void volatile*)as_ch;
}
#define MAKE_PHYS(addr) ((void*)((((unsigned)addr) & 0x1fffffff) | 0xa0000000))

static void wait_maple(void) {
    while (!(REG_ISTNRM & (1 << 12)))
           ;

    // clear the interrupt
    REG_ISTNRM |= (1 << 12);
}

static int check_controller(void) {
    // clear any pending interrupts (there shouldn't be any but do it anyways)
    REG_ISTNRM |= (1 << 12);

    // disable maple DMA
    REG_MDEN = 0;

    // make sure nothing else is going on
    while (REG_MDST)
        ;

    // 2mpbs transfer, timeout after 1ms
    REG_MSYS = 0xc3500000;

    // trigger via CPU (as opposed to vblank)
    REG_MDTSEL = 0;

    // let it write wherever it wants, i'm not too worried about rogue DMA xfers
    REG_MDAPRO = 0x6155407f;

    // construct packet
    static char volatile devinfo0[1024];
    static unsigned volatile frame[36 + 31];

    unsigned volatile *framep = (unsigned*)MAKE_PHYS(align32(frame));
    char volatile *devinfo0p = (char*)MAKE_PHYS(align32(devinfo0));

    framep[0] = 0x80000000;
    framep[1] = ((unsigned)devinfo0p) & 0x1fffffff;
    framep[2] = 0x2001;

    // set SB_MDSTAR to the address of the packet
    REG_MDSTAR = ((unsigned)framep) & 0x1fffffff;

    // enable maple DMA
    REG_MDEN = 1;

    // begin the transfer
    REG_MDST = 1;

    wait_maple();

    // transfer is now complete, receive data
    if (devinfo0p[0] == 0xff || devinfo0p[4] != 0 || devinfo0p[5] != 0 ||
        devinfo0p[6] != 0 || devinfo0p[7] != 1)
        return 0;

    char const *expect = "Dreamcast Controller         ";
    char const volatile *devname = devinfo0p + 22;

    while (*expect)
        if (*devname++ != *expect++)
            return 0;
    return 1;
}

static unsigned controller_btns_prev, controller_btns;

static unsigned get_controller_buttons(void) {
    if (!check_controller())
        return ~0;

    // clear any pending interrupts (there shouldn't be any but do it anyways)
    REG_ISTNRM |= (1 << 12);

    // disable maple DMA
    REG_MDEN = 0;

    // make sure nothing else is going on
    while (REG_MDST)
        ;

    // 2mpbs transfer, timeout after 1ms
    REG_MSYS = 0xc3500000;

    // trigger via CPU (as opposed to vblank)
    REG_MDTSEL = 0;

    // let it write wherever it wants, i'm not too worried about rogue DMA xfers
    REG_MDAPRO = 0x6155407f;

    // construct packet
    static char unsigned volatile cond[1024];
    static unsigned volatile frame[36 + 31];

    unsigned volatile *framep = (unsigned*)MAKE_PHYS(align32(frame));
    char unsigned volatile *condp = (char unsigned*)MAKE_PHYS(align32(cond));

    framep[0] = 0x80000001;
    framep[1] = ((unsigned)condp) & 0x1fffffff;
    framep[2] = 0x01002009;
    framep[3] = 0x01000000;

    // set SB_MDSTAR to the address of the packet
    REG_MDSTAR = ((unsigned)framep) & 0x1fffffff;

    // enable maple DMA
    REG_MDEN = 1;

    // begin the transfer
    REG_MDST = 1;

    wait_maple();

    // transfer is now complete, receive data
    return ((unsigned)condp[8]) | (((unsigned)condp[9]) << 8);
}

void update_controller(void) {
    controller_btns_prev = controller_btns;
    controller_btns = ~get_controller_buttons();
}

int check_btn(int btn_no) {
    int changed_btns = controller_btns_prev ^ controller_btns;

    if ((btn_no & changed_btns) && (btn_no & controller_btns))
        return 1;
    else
        return 0;
}

#define N_CHAR_ROWS MAX_CHARS_Y
#define N_CHAR_COLS MAX_CHARS_X

char const *hexstr(unsigned val) {
    static char txt[8];
    unsigned nib_no;
    for (nib_no = 0; nib_no < 8; nib_no++) {
        unsigned shift_amt = (7 - nib_no) * 4;
        unsigned nibble = (val >> shift_amt) & 0xf;
        switch (nibble) {
        case 0:
            txt[nib_no] = '0';
            break;
        case 1:
            txt[nib_no] = '1';
            break;
        case 2:
            txt[nib_no] = '2';
            break;
        case 3:
            txt[nib_no] = '3';
            break;
        case 4:
            txt[nib_no] = '4';
            break;
        case 5:
            txt[nib_no] = '5';
            break;
        case 6:
            txt[nib_no] = '6';
            break;
        case 7:
            txt[nib_no] = '7';
            break;
        case 8:
            txt[nib_no] = '8';
            break;
        case 9:
            txt[nib_no] = '9';
            break;
        case 10:
            txt[nib_no] = 'A';
            break;
        case 11:
            txt[nib_no] = 'B';
            break;
        case 12:
            txt[nib_no] = 'C';
            break;
        case 13:
            txt[nib_no] = 'D';
            break;
        case 14:
            txt[nib_no] = 'E';
            break;
        default:
            txt[nib_no] = 'F';
            break;
        }
    }
    txt[8] = '\0';
    return txt;
}

static char const *hexstr_no_leading_0(unsigned val) {
    char const *retstr = hexstr(val);
    while (*retstr == '0')
        retstr++;
    if (!*retstr)
        retstr--;
    return retstr;
}

unsigned short fonts[N_FONTS][288 * 24 * 12];

/*
TRANSLATE 32 BIT AREA to 64 BIT
if (addr32 > HALFWAY_POINT)
    return (addr32 - HALFWAY_POINT) * 2 + 4
else
    return addr32 * 2

TRANSLATE 64 BIT AREA to 32 BIT
if (addr32 % 8)
    return ((addr - 4) / 2) + HALFWAY_POINT
else
    return addr / 2
*/

#define PVR2_TEX_MEM_BANK_SIZE (4 << 20)
#define PVR2_TOTAL_VRAM_SIZE (8 << 20)

static struct sh4_vram_test_results {
    int test_results_32, test_results_16, test_results_8,
        test_results_float, test_results_double, test_results_sq;
} sh4_vram_test_results;

/*
 * Test that a write to a given address is correctly reflected across all 4
 * mirrors.
 *
 * PowerVR2 has a total of 8MB of VRAM on the Dreamcast.  The VRAM is split
 * between two banks which are both 4MB long.  Each mirror access VRAM through
 * either the 32-bit bus or the 64-bit bus.
 * The 64-bit bus optimizes bandwidth by interleaving the two banks together so
 * that it alternates between banks every four bytes.
 * The 32-bit bus stores the two banks sequentially, so that the first four
 * megabytes are the entirety of one bank and the second four bytes are the
 * entirety of the second bus.
 *
 * Although the two busses are referred to as "32-bit" and "64-bit", that has
 * no bearing on the size of reads and writes.  The names refer to the 64-bit
 * bus being able to access both banks at once due to the interleaving, and the
 * 32-bit bus being unable to do so.
 *
 * List of Mirrors:
 *     0x04000000 - 64 bit
 *     0x05000000 - 32 bit
 *     0x06000000 - 64 bit
 *     0x07000000 - 32 bit
 *
 * The following 8MB regions of addresses do *not* map to memory.  They can be
 * written to but the value will not be stored.  Reading them will always return
 * 0xffffffff (all ones):
 *     0x04800000
 *     0x05800000
 *     0x06800000
 *     0x07800000
 *
 * I think on Katana and NAOMI they actually map to more memory (those both had
 * 16MB), but on a retail dreamcast there's nothing there.
 */

// return all 8 banked versions of the given offset
void get_all_banks_from32(unsigned out[8], unsigned offs) {
    unsigned offs32 = offs & 0x007ffffc;
    unsigned offs64;

    if (offs32 >= PVR2_TEX_MEM_BANK_SIZE)
        offs64 = (offs32 - PVR2_TEX_MEM_BANK_SIZE) * 2 + 4;
    else
        offs64 = offs32 * 2;

    out[0] = (offs64 | 0xa4000000) | (offs & 3);
    out[1] = (offs64 | 0xa4800000) | (offs & 3);
    out[2] = (offs32 | 0xa5000000) | (offs & 3);
    out[3] = (offs32 | 0xa5800000) | (offs & 3);
    out[4] = (offs64 | 0xa6000000) | (offs & 3);
    out[5] = (offs64 | 0xa6800000) | (offs & 3);
    out[6] = (offs32 | 0xa7000000) | (offs & 3);
    out[7] = (offs32 | 0xa7800000) | (offs & 3);
}

void get_all_banks_from64(unsigned out[8], unsigned offs) {
    unsigned offs64 = offs & 0x007ffffc;
    unsigned offs32;

    if (offs64 % 8)
        offs32 = ((offs64 - 4) / 2) + PVR2_TEX_MEM_BANK_SIZE;
    else
        offs32 = offs64 / 2;

    out[0] = (offs64 | 0xa4000000) | (offs & 3);
    out[1] = (offs64 | 0xa4800000) | (offs & 3);
    out[2] = (offs32 | 0xa5000000) | (offs & 3);
    out[3] = (offs32 | 0xa5800000) | (offs & 3);
    out[4] = (offs64 | 0xa6000000) | (offs & 3);
    out[5] = (offs64 | 0xa6800000) | (offs & 3);
    out[6] = (offs32 | 0xa7000000) | (offs & 3);
    out[7] = (offs32 | 0xa7800000) | (offs & 3);
}

int test_single_addr_float(unsigned offs) {
    unsigned addrs[8];

    get_all_banks_from32(addrs, offs);

    static unsigned const testvals[8] = {
        0xdeadbeef,
        0xdeadbabe,
        0xb16b00b5,
        0xfeedface,
        0xdeadc0de,
        0xaaaa5555,
        0x5555aaaa
    };

    int ret = 0;

#define CHECK_BANK_FLOAT(bank_no)                                       \
    __asm__ __volatile__ (                                              \
                          /* fix up fpscr */                            \
                          "sts fpscr, r1\n"                             \
                          "mov #3, r2\n"                                \
                          "shll8 r2\n"                                  \
                          "shll8 r2\n"                                  \
                          "shll2 r2\n"                                  \
                          "shll r2\n"                                   \
                          "not r2, r2\n"                                \
                          "and r1, r2\n"                                \
                          "lds r2, fpscr\n"                             \
                                                                        \
                          /* r0 = bank_no * sizeof(float) */            \
                          "mov #" #bank_no ", r0\n"                     \
                          "shll2 r0\n"                                  \
                                                                        \
                          /* write testvals[bank_no] to ptrs[bank_no] */ \
                          "mov.l @(r0, %[ptrs]), r5\n"                  \
                          "fmov.s @(r0, %[testvals]), fr0\n"            \
                          "fmov.s fr0, @r5\n"                           \
                                                                        \
                          /* clear bit 0 of bank_no */                  \
                          "mov #" #bank_no ", r0\n"                     \
                          "and #0xfe, r0\n"                             \
                          "shll2 r0\n"                                  \
                                                                        \
                          /* load testvals[bank_no & ~1] */             \
                          "mov.l @(r0, %[testvals]), r3\n"              \
                                                                        \
                          "xor r0, r0\n"                                \
                          "mov #0xff, r4\n"                             \
                                                                        \
                          /* if (*ptrs[0] != testvals[bank_no & ~1]) */ \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r3\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[1] != 0xffffffff) */             \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r4\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[2] != testvals[bank_no & ~1]) */ \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r3\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[3] != 0xffffffff) */             \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r4\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[4] != testvals[bank_no & ~1]) */ \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r3\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[5] != 0xffffffff) */             \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r4\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[6] != testvals[bank_no & ~1]) */ \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r3\n"                             \
                          "bf/s on_fail_"#bank_no"\n"                   \
                          "add #4, r0\n"                                \
                                                                        \
                          /* if (*ptrs[7] != 0xffffffff) */             \
                          "mov.l @(r0, %[ptrs]), r2\n"                  \
                          "fmov.s @r2, fr0\n"                           \
                          "flds fr0, fpul\n"                            \
                          "sts fpul, r2\n"                              \
                          "cmp/eq r2, r4\n"                             \
                          "bf on_fail_"#bank_no"\n"                     \
                                                                        \
                          "bra done_"#bank_no"\n"                       \
                          "nop\n"                                       \
                                                                        \
                          "on_fail_"#bank_no":\n"                       \
                          "mov r5, %[ret]\n"                            \
                                                                        \
                          "done_"#bank_no":\n"                          \
                          /* restore fpscr */                           \
                          "lds r1, fpscr\n"                             \
                                                                        \
                          : [ret] "+r" (ret)                            \
                          : [testvals] "r" (testvals), [ptrs] "r"(addrs) \
                          : "r0", "r1", "r2", "r3", "r4", "r5", "fr0", "fpul")

    CHECK_BANK_FLOAT(0);
    CHECK_BANK_FLOAT(1);
    CHECK_BANK_FLOAT(2);
    CHECK_BANK_FLOAT(3);
    CHECK_BANK_FLOAT(4);
    CHECK_BANK_FLOAT(5);
    CHECK_BANK_FLOAT(6);
    CHECK_BANK_FLOAT(7);

    return ret;
}

int test_single_addr_32(unsigned offs) {
    unsigned bank;
    unsigned addrs[8];

    get_all_banks_from32(addrs, offs);

    unsigned volatile *ptrs[8];
    for (bank = 0; bank < 8; bank++)
        ptrs[bank] = (unsigned volatile*)addrs[bank];

    static unsigned const testvals[8] = {
        0xdeadbeef,
        0xdeadbabe,
        0xb16b00b5,
        0xfeedface,
        0xdeadc0de,
        0xaaaa5555,
        0x5555aaaa
    };

#define CHECK_BANK_32BIT(bank_no)               \
    *ptrs[bank_no] = testvals[bank_no];         \
    if (*ptrs[0] != testvals[bank_no & ~1] ||   \
        *ptrs[1] != 0xffffffff ||               \
        *ptrs[2] != testvals[bank_no & ~1] ||   \
        *ptrs[3] != 0xffffffff ||               \
        *ptrs[4] != testvals[bank_no & ~1] ||   \
        *ptrs[5] != 0xffffffff ||               \
        *ptrs[6] != testvals[bank_no & ~1] ||   \
        *ptrs[7] != 0xffffffff) {               \
        return (int)ptrs[bank_no];              \
    }

    CHECK_BANK_32BIT(0)
    CHECK_BANK_32BIT(1)
    CHECK_BANK_32BIT(2)
    CHECK_BANK_32BIT(3)
    CHECK_BANK_32BIT(4)
    CHECK_BANK_32BIT(5)
    CHECK_BANK_32BIT(6)
    CHECK_BANK_32BIT(7)

    return 0;
}

int test_single_addr_16(unsigned offs) {
    unsigned bank;
    unsigned addrs[8];

    get_all_banks_from32(addrs, offs);

    unsigned short volatile *ptrs[8];
    for (bank = 0; bank < 8; bank++)
        ptrs[bank] = (unsigned short volatile*)addrs[bank];

    static unsigned short const testvals[8] = {
        0xdead,
        0xbabe,
        0xb00b,
        0xface,
        0xa55a,
        0x5aa5,
        0xa5a5,
        0x5a5a
    };

#define CHECK_BANK_16BIT(bank_no)               \
    *ptrs[bank_no] = testvals[bank_no];         \
    if (*ptrs[0] != testvals[bank_no & ~1] ||   \
        *ptrs[1] != 0xffff ||                   \
        *ptrs[2] != testvals[bank_no & ~1] ||   \
        *ptrs[3] != 0xffff ||                   \
        *ptrs[4] != testvals[bank_no & ~1] ||   \
        *ptrs[5] != 0xffff ||                   \
        *ptrs[6] != testvals[bank_no & ~1] ||   \
        *ptrs[7] != 0xffff) {                   \
        return (int)ptrs[bank_no];              \
    }

    CHECK_BANK_16BIT(0)
    CHECK_BANK_16BIT(1)
    CHECK_BANK_16BIT(2)
    CHECK_BANK_16BIT(3)
    CHECK_BANK_16BIT(4)
    CHECK_BANK_16BIT(5)
    CHECK_BANK_16BIT(6)
    CHECK_BANK_16BIT(7)

    return 0;
}

/*
 * test four sequential one-byte writes.  The kicker here is that one-byte
 * writes never work so we don't expect the value to change.
 */
int test_single_addr_8(unsigned offs) {
    unsigned bank;
    unsigned addrs[8];

    get_all_banks_from32(addrs, offs);

    unsigned char volatile *ptrs[8];
    for (bank = 0; bank < 8; bank++)
        ptrs[bank] = (unsigned char volatile*)addrs[bank];

    static unsigned char const testvals[8] = {
        0xde,
        0xba,
        0xb0,
        0xfa,
        0xa5,
        0x5a,
        0xad,
        0x0b
    };

#define CHECK_BANK_8BIT(bank_no)                        \
    *ptrs[bank_no] = testvals[bank_no];                 \
    if (*ptrs[0] != testvals[bank_no & ~1] ||           \
        *ptrs[1] != 0xff ||                             \
        *ptrs[2] != testvals[bank_no & ~1] ||           \
        *ptrs[3] != 0xff ||                             \
        *ptrs[4] != testvals[bank_no & ~1] ||           \
        *ptrs[5] != 0xff ||                             \
        *ptrs[6] != testvals[bank_no & ~1] ||           \
        *ptrs[7] != 0xff) {                             \
        return (int)ptrs[bank_no];                      \
    }

    CHECK_BANK_8BIT(0)
    CHECK_BANK_8BIT(1)
    CHECK_BANK_8BIT(2)
    CHECK_BANK_8BIT(3)
    CHECK_BANK_8BIT(4)
    CHECK_BANK_8BIT(5)
    CHECK_BANK_8BIT(6)
    CHECK_BANK_8BIT(7)

    return 0;
}

int test_single_addr_sq(unsigned offs) {
    unsigned bank;
    unsigned addrs[8];

    unsigned write_bank;
    for (write_bank = 0; write_bank < 8; write_bank++) {
        if (write_bank & 2)
            get_all_banks_from32(addrs, offs);
        else
            get_all_banks_from64(addrs, offs);

        unsigned  volatile *ptrs[8];
        for (bank = 0; bank < 8; bank++)
            ptrs[bank] = (unsigned volatile*)addrs[bank];

        __attribute__((aligned(8)))
            static unsigned const testvals[8*8] = {
            0xdeadbeef,
            0xbabeface,
            0xdeadbabe,
            0xbeefb00b,
            0xb16b00b5,
            0xfeedbabe,
            0xfeedface,
            0xaaaa5555,

            0xdeadc0de,
            0x5555aaaa,
            0xaaaa5555,
            0xbabeb00b,
            0x5555aaaa,
            0xb16babe5,
            0x12345678,
            0x90abcdef,

            0xdeadbeef,
            0xbabeface,
            0xdeadbabe,
            0xbeefb00b,
            0xdeadc0de,
            0x5555aaaa,
            0xaaaa5555,
            0xbabeb00b,

            0xb16b00b5,
            0xfeedbabe,
            0xfeedface,
            0xaaaa5555,
            0xdeadc0de,
            0x5555aaaa,
            0xaaaa5555,
            0xbabeb00b,

            0x5555aaaa,
            0xb16babe5,
            0x12345678,
            0x90abcdef,
            0xdeadbeef,
            0xbabeface,
            0xdeadbabe,
            0xbeefb00b,

            0xaaaa5555,
            0xbabeb00b,
            0x5555aaaa,
            0xb16babe5,
            0xdeadbabe,
            0xbeefb00b,
            0xb16b00b5,
            0xfeedbabe,

            0xdeadbeef,
            0xdeadbabe,
            0xb16b00b5,
            0xfeedface,
            0xdeadc0de,
            0xaaaa5555,
            0x5555aaaa,
            0x12345678,

            0xbabeface,
            0xbeefb00b,
            0xfeedbabe,
            0xaaaa5555,
            0x5555aaaa,
            0xbabeb00b,
            0xb16babe5,
            0x90abcdef
        };

        int ret = 0;

        unsigned const *src = testvals + (write_bank & ~1) * 8 ;
        write_sq(src, ptrs[write_bank]);

        unsigned idx;
        for (idx = 0; idx < 8; idx++) {
            unsigned chk_bank;
            for (chk_bank = 0; chk_bank < 8; chk_bank++) {
                unsigned chk_bank_addrs[8];
                if (write_bank & 2)
                    get_all_banks_from32(chk_bank_addrs, addrs[write_bank] + idx * 4);
                else
                    get_all_banks_from64(chk_bank_addrs, addrs[write_bank] + idx * 4);

                unsigned volatile *chk_bank_ptr = (unsigned volatile*)(chk_bank_addrs[chk_bank]);
                if ((chk_bank & 1) == 0) {
                    if (*chk_bank_ptr != src[idx])
                        return (int)ptrs[write_bank];
                } else {
                    if (*chk_bank_ptr != (unsigned)-1)
                        return (int)ptrs[write_bank];
                }
            }
        }
    }
    return 0;
}

static int run_sh4_vram_slow_test(void) {
    unsigned offs;

    // this test will overwrite the fb, so temporarily disable video
    disable_video();

    sh4_vram_test_results.test_results_32 = 0;
    sh4_vram_test_results.test_results_16 = 0;
    sh4_vram_test_results.test_results_8 = 0;
    sh4_vram_test_results.test_results_float = 0;
    sh4_vram_test_results.test_results_double = 0;
    sh4_vram_test_results.test_results_sq = 0;

    for (offs = 0; offs < PVR2_TOTAL_VRAM_SIZE; offs += 4)
        if ((sh4_vram_test_results.test_results_32 = test_single_addr_32(offs)))
            break;

    for (offs = 0; offs < PVR2_TOTAL_VRAM_SIZE; offs += 2)
        if ((sh4_vram_test_results.test_results_16 = test_single_addr_16(offs)))
            break;

    for (offs = 0; offs < PVR2_TOTAL_VRAM_SIZE; offs++)
        if ((sh4_vram_test_results.test_results_8  = test_single_addr_8(offs)))
            break;

    for (offs = 0; offs < PVR2_TOTAL_VRAM_SIZE; offs += 4)
        if ((sh4_vram_test_results.test_results_float = test_single_addr_float(offs)))
            break;

    for (offs = 0; offs < PVR2_TOTAL_VRAM_SIZE; offs += 8)
        if ((sh4_vram_test_results.test_results_double = test_single_addr_double(offs)))
            break;

    for (offs = 0; offs < PVR2_TOTAL_VRAM_SIZE; offs += 32)
        if ((sh4_vram_test_results.test_results_sq = test_single_addr_sq(offs)))
            break;

    clear_screen(get_backbuffer(), make_color(0, 0, 0));
    clear_screen(get_frontbuffer(), make_color(0, 0, 0));
    enable_video();

    return STATE_SH4_VRAM_TEST_RESULTS;
}

/*
 * This is exactly the same as the slow test but we don't test every single
 * address.
 */
static int run_sh4_vram_fast_test(void) {
    unsigned offs;

    // this test will overwrite the fb, so temporarily disable video
    disable_video();

    sh4_vram_test_results.test_results_32 = 0;
    sh4_vram_test_results.test_results_16 = 0;
    sh4_vram_test_results.test_results_8 = 0;
    sh4_vram_test_results.test_results_float = 0;
    sh4_vram_test_results.test_results_double = 0;
    sh4_vram_test_results.test_results_sq = 0;

    for (offs = 0; offs < 2048; offs += 4)
        if ((sh4_vram_test_results.test_results_32 = test_single_addr_32(offs)))
            break;
    for (offs = (PVR2_TOTAL_VRAM_SIZE/2)-1024; offs < (PVR2_TOTAL_VRAM_SIZE/2)+1024; offs += 4)
        if ((sh4_vram_test_results.test_results_32 = test_single_addr_32(offs)))
            break;
    for (offs = PVR2_TOTAL_VRAM_SIZE-2048; offs < PVR2_TOTAL_VRAM_SIZE; offs += 4)
        if ((sh4_vram_test_results.test_results_32 = test_single_addr_32(offs)))
            break;

    for (offs = 0; offs < 2048; offs += 2)
        if ((sh4_vram_test_results.test_results_16 = test_single_addr_16(offs)))
            break;
    for (offs = (PVR2_TOTAL_VRAM_SIZE/2)-1024; offs < (PVR2_TOTAL_VRAM_SIZE/2)+1024; offs += 2)
        if ((sh4_vram_test_results.test_results_16 = test_single_addr_16(offs)))
            break;
    for (offs = PVR2_TOTAL_VRAM_SIZE-2048; offs < PVR2_TOTAL_VRAM_SIZE; offs += 2)
        if ((sh4_vram_test_results.test_results_16 = test_single_addr_16(offs)))
            break;

    for (offs = 0; offs < 2048; offs++)
        if ((sh4_vram_test_results.test_results_8 = test_single_addr_8(offs)))
            break;
    for (offs = (PVR2_TOTAL_VRAM_SIZE/2)-1024; offs < (PVR2_TOTAL_VRAM_SIZE/2)+1024; offs++)
        if ((sh4_vram_test_results.test_results_8 = test_single_addr_8(offs)))
            break;
    for (offs = PVR2_TOTAL_VRAM_SIZE-2048; offs < PVR2_TOTAL_VRAM_SIZE; offs++)
        if ((sh4_vram_test_results.test_results_8 = test_single_addr_8(offs)))
            break;

    for (offs = 0; offs < 2048; offs += 4)
        if ((sh4_vram_test_results.test_results_float = test_single_addr_float(offs)))
            break;
    for (offs = (PVR2_TOTAL_VRAM_SIZE/2)-1024; offs < (PVR2_TOTAL_VRAM_SIZE/2)+1024; offs += 4)
        if ((sh4_vram_test_results.test_results_float = test_single_addr_float(offs)))
            break;
    for (offs = PVR2_TOTAL_VRAM_SIZE-2048; offs < PVR2_TOTAL_VRAM_SIZE; offs += 4)
        if ((sh4_vram_test_results.test_results_float = test_single_addr_float(offs)))
            break;

    for (offs = 0; offs < 2048; offs += 8)
        if ((sh4_vram_test_results.test_results_double = test_single_addr_double(offs)))
            break;
    for (offs = (PVR2_TOTAL_VRAM_SIZE/2)-1024; offs < (PVR2_TOTAL_VRAM_SIZE/2)+1024; offs += 8)
        if ((sh4_vram_test_results.test_results_double = test_single_addr_double(offs)))
            break;
    for (offs = PVR2_TOTAL_VRAM_SIZE-2048; offs < PVR2_TOTAL_VRAM_SIZE; offs += 8)
        if ((sh4_vram_test_results.test_results_double = test_single_addr_double(offs)))
            break;

    for (offs = 0; offs < 2048; offs += 32)
        if ((sh4_vram_test_results.test_results_sq = test_single_addr_sq(offs)))
            break;
    for (offs = (PVR2_TOTAL_VRAM_SIZE/2)-1024; offs < (PVR2_TOTAL_VRAM_SIZE/2)+1024; offs += 32)
        if ((sh4_vram_test_results.test_results_sq = test_single_addr_sq(offs)))
            break;
    for (offs = PVR2_TOTAL_VRAM_SIZE-2048; offs < PVR2_TOTAL_VRAM_SIZE; offs += 32)
        if ((sh4_vram_test_results.test_results_sq = test_single_addr_sq(offs)))
            break;

    clear_screen(get_backbuffer(), make_color(0, 0, 0));
    clear_screen(get_frontbuffer(), make_color(0, 0, 0));
    enable_video();

    return STATE_SH4_VRAM_TEST_RESULTS;
}

static int disp_results(void) {
    for (;;) {
        void volatile *fb = get_backbuffer();
        clear_screen(fb, make_color(0, 0, 0));
        drawstring(fb, fonts[4], "*****************************************************", 0, 0);
        drawstring(fb, fonts[4], "*                                                   *", 1, 0);
        drawstring(fb, fonts[4], "*                   SH4 VRAM TEST                   *", 2, 0);
        drawstring(fb, fonts[4], "*                   RESULT SCREEN                   *", 3, 0);
        drawstring(fb, fonts[4], "*                                                   *", 4, 0);
        drawstring(fb, fonts[4], "*****************************************************", 5, 0);

        drawstring(fb, fonts[4], "     SH4 VRAM 32-BIT", 7, 0);
        if (sh4_vram_test_results.test_results_32 == 0) {
            drawstring(fb, fonts[1], "SUCCESS", 7, 21);
        } else {
            drawstring(fb, fonts[2], "FAILURE - ", 7, 21);
            drawstring(fb, fonts[2],
                       hexstr(sh4_vram_test_results.test_results_32), 7, 31);
        }

        drawstring(fb, fonts[4], "     SH4 VRAM 16-BIT", 8, 0);
        if (sh4_vram_test_results.test_results_16 == 0) {
            drawstring(fb, fonts[1], "SUCCESS", 8, 21);
        } else {
            drawstring(fb, fonts[2], "FAILURE - ", 8, 21);
            drawstring(fb, fonts[2],
                       hexstr(sh4_vram_test_results.test_results_32), 7, 31);
        }

        drawstring(fb, fonts[4], "     SH4 VRAM 8-BIT", 9, 0);
        if (sh4_vram_test_results.test_results_8 == 0) {
            drawstring(fb, fonts[1], "SUCCESS", 9, 21);
        } else {
            drawstring(fb, fonts[2], "FAILURE - ", 9, 21);
            drawstring(fb, fonts[2],
                       hexstr(sh4_vram_test_results.test_results_32), 7, 31);
        }

        drawstring(fb, fonts[4], "     SH4 VRAM FLOAT", 10, 0);
        if (sh4_vram_test_results.test_results_float == 0) {
            drawstring(fb, fonts[1], "SUCCESS", 10, 21);
        } else {
            drawstring(fb, fonts[2], "FAILURE - ", 10, 21);
            drawstring(fb, fonts[2],
                       hexstr(sh4_vram_test_results.test_results_32), 7, 31);
        }

        drawstring(fb, fonts[4], "     SH4 VRAM DOUBLE", 11, 0);
        if (sh4_vram_test_results.test_results_double == 0) {
            drawstring(fb, fonts[1], "SUCCESS", 11, 21);
        } else {
            drawstring(fb, fonts[2], "FAILURE - ", 11, 21);
            drawstring(fb, fonts[2],
                       hexstr(sh4_vram_test_results.test_results_32), 7, 31);
        }

        drawstring(fb, fonts[4], "     SH4 VRAM SQ", 12, 0);
        if (sh4_vram_test_results.test_results_sq == 0) {
            drawstring(fb, fonts[1], "SUCCESS", 12, 21);
        } else {
            drawstring(fb, fonts[2], "FAILURE - ", 12, 21);
            drawstring(fb, fonts[2],
                       hexstr(sh4_vram_test_results.test_results_32), 7, 31);
        }

        while (!check_vblank())
            ;
        swap_buffers();
        update_controller();

        if (check_btn(1 << 2)) // a button
            return STATE_MENU;
    }
}

#ifndef NULL
#define NULL ((void*)0x0)
#endif

static struct menu_entry {
    char const *txt;
    unsigned state;
} const menu_entries[] = {
    { "SH4 VRAM TEST (FAST)", STATE_SH4_VRAM_TEST_FAST },
    { "SH4 VRAM TEST (SLOW)", STATE_SH4_VRAM_TEST_SLOW },
    { "DMA TEST (64-BIT)", STATE_DMA_TEST_64BIT },
    { "DMA TEST (32-BIT)", STATE_DMA_TEST_32BIT },

    { NULL }
};

static int main_menu(void) {
    int menupos = 0;

    enable_video();
    clear_screen(get_backbuffer(), make_color(0, 0, 0));

    int curs = 0;

    while (!check_vblank())
        ;
    controller_btns_prev = ~get_controller_buttons();

    for (;;) {
        void volatile *fb = get_backbuffer();
        clear_screen(fb, make_color(0, 0, 0));
        drawstring(fb, fonts[4], "*****************************************************", 0, 0);
        drawstring(fb, fonts[4], "*                                                   *", 1, 0);
        drawstring(fb, fonts[4], "*           Dreamcast PowerVR2 Memory Test          *", 2, 0);
        drawstring(fb, fonts[4], "*             snickerbockers 2020, 2022             *", 3, 0);
        drawstring(fb, fonts[4], "*                                                   *", 4, 0);
        drawstring(fb, fonts[4], "*****************************************************", 5, 0);

        unsigned row = 7;
        unsigned n_ents = 0;
        struct menu_entry const *cur_ent = menu_entries;
        while (cur_ent->txt) {
            drawstring(fb, fonts[4], cur_ent->txt, row++, 9);
            cur_ent++;
            n_ents++;
        }

        drawstring(fb, fonts[4], " ======>", curs + 7, 0);

        while (!check_vblank())
            ;
        swap_buffers();

        update_controller();

        if (check_btn(1 << 5)) {
            // d-pad down
            if (curs < (n_ents-1))
                curs++;
        }

        if (check_btn(1 << 4)) {
            // d-pad up
            if (curs > 0)
                curs--;
        }

        // a button
        if (check_btn(1 << 2)) {
            cur_ent = menu_entries;
            while (curs) {
                curs--;
                cur_ent++;
            }

            return cur_ent->state;
        }
    }
}

/*
 * our entry point (after _start).
 *
 * I had to call this dcmain because the linker kept wanting to put main at the
 * entry instead of _start, and this was the only thing I tried that actually
 * fixed it.
 */
int dcmain(int argc, char **argv) {
    /*
     * The reason why these big arrays are static is to save on stack space.
     * We only have 4KB!
     */
    /* static char txt_buf[N_CHAR_ROWS][N_CHAR_COLS]; */
    /* static int txt_colors[N_CHAR_ROWS][N_CHAR_COLS]; */

    configure_video();

    create_font(fonts[0], make_color(0, 0, 0), make_color(0, 0, 0));
    create_font(fonts[1], make_color(0, 197, 0), make_color(0, 0, 0));
    create_font(fonts[2], make_color(197, 0, 9), make_color(0, 0, 0));
    create_font(fonts[3], make_color(255, 255, 255), make_color(0, 0, 0));
    create_font(fonts[4], make_color(197, 197, 230), make_color(0, 0, 0));
    create_font(fonts[5], make_color(255, 131, 24), make_color(0, 0, 0));

    state = STATE_MENU;

    for (;;) {
        switch (state) {
        case STATE_SH4_VRAM_TEST_SLOW:
            state = run_sh4_vram_slow_test();
            break;
        case STATE_SH4_VRAM_TEST_FAST:
            state = run_sh4_vram_fast_test();
            break;
        case STATE_SH4_VRAM_TEST_RESULTS:
            state = disp_results();
            break;
        case STATE_DMA_TEST_32BIT:
            state = run_dma_tests(1);
            break;
        case STATE_DMA_TEST_64BIT:
            state = run_dma_tests(0);
            break;
        case STATE_DMA_TEST_RESULTS:
            state = show_dma_test_results();
            break;
        default:
        case STATE_MENU:
            state = main_menu();
            break;
        }
    }

    return 0;
}
