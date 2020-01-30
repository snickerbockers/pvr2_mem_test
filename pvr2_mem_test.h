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

enum {
    STATE_MENU,
    STATE_SH4_VRAM_TEST_SLOW,
    STATE_SH4_VRAM_TEST_FAST,
    STATE_SH4_VRAM_TEST_RESULTS,
    STATE_DMA_TEST,
    STATE_DMA_TEST_RESULTS
};

int run_dma_tests(void);
int show_dma_test_results(void);
void drawstring(void volatile *fb, unsigned short const *font,
                char const *msg, unsigned row, unsigned col);

void clear_screen(void volatile* fb, unsigned short color);
unsigned short make_color(unsigned red, unsigned green, unsigned blue);
void volatile *get_backbuffer(void);
char const *hexstr(unsigned val);

void update_controller(void);
int check_btn(int btn_no);

#define N_FONTS 6
extern unsigned short fonts[N_FONTS][288 * 24 * 12];

void swap_buffers(void);

int check_vblank(void);

void disable_video(void);

void enable_video(void);
