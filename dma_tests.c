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

#define C2DSTAT  (*(unsigned volatile*)0xa05f6800)
#define C2DLEN   (*(unsigned volatile*)0xa05f6804)
#define C2DST    (*(unsigned volatile*)0xa05f6808)

#define SAR2     (*(unsigned volatile*)0xffa00020)
#define DAR2     (*(unsigned volatile*)0xffa00024)
#define DMATCR2  (*(unsigned volatile*)0xffa00028)
#define CHCR2    (*(unsigned volatile*)0xffa0002c)
#define DMAOR    (*(unsigned volatile*)0xffa00040)

#define ISTNRM  (*(unsigned volatile*)0xa05f6900)
#define IML2NRM (*(unsigned volatile*)0xa05f6910)
#define IML4NRM (*(unsigned volatile*)0xa05f6920)
#define IML6NRM (*(unsigned volatile*)0xa05f6930)

#define LMMODE0 (*(unsigned volatile*)0xa05f6884)
#define LMMODE1 (*(unsigned volatile*)0xa05f6888)

// source address increment
enum addr_inc_mode {
    SAR_INC_FIXED = 0,
    SAR_INC_POS = 1,
    SAR_INC_NEG = 2
};

#define ERROR_INVAL 1

static int spin_count;

int pvr2_dma_xfer(void *src, void *dst, unsigned n_units,
                  enum addr_inc_mode src_inc_mode,
                  enum addr_inc_mode dst_inc_mode,
                  int burst_mode, int tx_unit_sz_bytes, int use_irq) {
    if (((unsigned)dst) & 0x1f)
        return ERROR_INVAL;

    if (use_irq)
        return ERROR_INVAL; // not implemented yet

    int tx_sz;
    switch (tx_unit_sz_bytes) {
    case 8:
        tx_sz = 0;
        break;
    case 1:
        tx_sz = 1;
        break;
    case 2:
        tx_sz = 2;
        break;
    case 4:
        tx_sz = 3;
        break;
    case 32:
        tx_sz = 4;
        break;
    default:
        return ERROR_INVAL;
    }

    while (C2DST)
        ;

    // disable DMA, and make sure transfer-end bit is clear
    CHCR2 &= ~3;

    SAR2 = (unsigned)src;
    DMATCR2 = n_units;

    CHCR2 = ((dst_inc_mode & 3) << 14) |
        ((src_inc_mode & 3) << 12) |
        (2 << 8) | // resource select
        ((burst_mode & 1) << 7) |
        (tx_sz << 4) |
        1; // DMA enable

    unsigned dmaor_tmp = DMAOR;
    dmaor_tmp |= (1 << 15) | 1;
    dmaor_tmp &= ~((1 << 1) | (1 << 2));
    DMAOR = dmaor_tmp;

    C2DSTAT = (unsigned)dst;
    C2DLEN = n_units * tx_unit_sz_bytes;

    IML2NRM &= ~(1 << 19);
    IML4NRM &= ~(1 << 19);
    IML6NRM &= ~(1 << 19);

    /* IML2NRM |= (1 << 19); */
    ISTNRM |= (1<<19);

    C2DST = 1;

    spin_count = 0;
    while (!(ISTNRM & (1<<19)))
        spin_count++;

    if (DMATCR2)
        return 1;

    if (SAR2 != ((unsigned)src) + tx_unit_sz_bytes * n_units)
        return 1;

    // TODO: test C2DSTAT, other registers too

    return 0;
}

#define N_TEST_VALS (8*1024*1024/4)
__attribute__((aligned (32))) static unsigned test_data[N_TEST_VALS];
/* static int test_results;  */

static void cache_flush(void *addr) {
    __asm__ __volatile__(
                         "ocbwb @%[ptr]\n"
                         :
                         : [ptr] "r" (addr));
}

static int run_single_test(unsigned n_dwords) {
    unsigned idx;
    unsigned offs = (8*1024*1024) - n_dwords*4;

    if (pvr2_dma_xfer(test_data, (void*)(0x11000000+offs),
                      (n_dwords*4)/32, SAR_INC_POS, SAR_INC_FIXED,
                      1, 32, 0) != 0) {
        return -1;
    }

    int volatile *res = (int volatile*)(0xa4000000+offs);
    if (res[0] != 1 || res[1] != 1)
        return -1;
    for (idx = 2; idx < n_dwords; idx++)
        if (res[idx] != res[idx-2] + res[idx-1])
            return -1;

    return 0;
}

#define N_TRIALS 19
static int trial_results[N_TRIALS];

#define RESULTS_PER_PAGE (476 / 24)

int run_dma_tests(void) {
    int idx;

    disable_video();
    /* test_results = -1; */
    for (idx = 0; idx < N_TRIALS; idx++)
        trial_results[idx] = -1;

    test_data[0] = 1;
    test_data[1] = 1;
    for (idx = 2; idx < N_TEST_VALS; idx++)
        test_data[idx] = test_data[idx - 2] + test_data[idx - 1];

    for (idx = 0; idx < N_TEST_VALS; idx++)
        cache_flush(test_data + idx);

    LMMODE0 = 0;
    LMMODE1 = 0;

    unsigned n_dwords = 8;
    unsigned trial_no;
    unsigned cur_trial;
    for (trial_no = 0; trial_no < N_TRIALS; trial_no++) {
        trial_results[trial_no] = run_single_test(n_dwords);
        n_dwords *= 2;
    }

 the_end:
    return STATE_DMA_TEST_RESULTS;
}

int show_dma_test_results(void) {
    for (;;) {
        void volatile *fb = get_backbuffer();
        clear_screen(fb, make_color(0, 0, 0));

        unsigned row;
        for (row = 0; row < RESULTS_PER_PAGE && row < N_TRIALS; row++) {
            drawstring(fb, fonts[4], hexstr(32 << row), row, 12);
            if (trial_results[row] == 0)
                drawstring(fb, fonts[1], "SUCCESS", row, 21);
            else
                drawstring(fb, fonts[2], "FAILURE", row, 21);
        }

        /* if (test_results != 0) { */
        /*     drawstring(fb, fonts[2], "DMA TEST FAILS", 7, 21); */
        /* } else { */
        /*     drawstring(fb, fonts[1], "DMA TEST SUCCEEDS", 7, 21); */
        /* } */
        /* drawstring(fb, fonts[4], hexstr(n_trials), 9, 21); */

        while (!check_vblank())
            ;
        enable_video();
        swap_buffers();

        update_controller();

        if (check_btn(1 << 2)) // a button
            return STATE_MENU;
    }
}
