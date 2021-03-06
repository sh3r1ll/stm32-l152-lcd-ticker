/*
 * Copyright (C) 2014 Nikolay Merinov <nikolay.merinov@member.fsf.org>
 * Copyright (C) 2016 Pyotr Ermishkin <quasiyoke@gmail.com>
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/l1/lcd.h>
#include <string.h>
#include "main.h"

static void lcd_init(void)
{
    // Move all needed GPIO pins to LCD alternative mode
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_peripheral_enable_clock(&RCC_AHBLPENR, RCC_AHBLPENR_GPIOALPEN |
        RCC_AHBLPENR_GPIOBLPEN | RCC_AHBLPENR_GPIOCLPEN);
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO1 | GPIO2 |
        GPIO3 | GPIO8 | GPIO9 | GPIO10 | GPIO15);
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE,
        GPIO3 | GPIO4 | GPIO5 | GPIO8 | GPIO9 | GPIO10 | GPIO11 |
        GPIO12 | GPIO13 | GPIO14 | GPIO15);
    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO0 | GPIO1 |
        GPIO2 | GPIO3 | GPIO6 | GPIO7 | GPIO8 | GPIO9 |
        GPIO10 | GPIO11);

    gpio_set_af(GPIOA, GPIO_AF11, GPIO1 | GPIO2 | GPIO3 | GPIO8 | GPIO9 |
        GPIO10 | GPIO15);
    gpio_set_af(GPIOB, GPIO_AF11, GPIO3 | GPIO4 | GPIO5 | GPIO8 | GPIO9 |
        GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO14 | GPIO15);
    gpio_set_af(GPIOC, GPIO_AF11, GPIO0 | GPIO1 | GPIO2 | GPIO3 | GPIO6 |
        GPIO7 | GPIO8 | GPIO9 | GPIO10 | GPIO11);

    // Enable LCD and use LSE clock as RTC/LCD clock.
    rcc_periph_clock_enable(RCC_PWR);
    rcc_periph_clock_enable(RCC_LCD);
    pwr_disable_backup_domain_write_protect();
    rcc_osc_on(RCC_LSE);
    rcc_wait_for_osc_ready(RCC_LSE);
    rcc_rtc_select_clock(RCC_CSR_RTCSEL_LSE);
    RCC_CSR |= RCC_CSR_RTCEN; // Enable RTC clock
    pwr_enable_backup_domain_write_protect();

    // Map SEG[43:40] to SEG[31:28], use 4 LCD commons, use 3 voltage levels
    // when driving LCD display
    lcd_enable_segment_multiplexing();
    lcd_set_duty(LCD_CR_DUTY_1_4);
    lcd_set_bias(LCD_CR_BIAS_1_3);

    // Set screen redraw frequency to 100Hz
    lcd_set_refresh_frequency(100);
    // And increase contrast
    lcd_set_contrast(LCD_FCR_CC_5);

    lcd_enable();
    do {} while (!lcd_is_enabled());
    do {} while (!lcd_is_step_up_ready());
}

/**
 *         LCD MAPPING:
 *              A
 *      _  ----------
 * COL |_| |\   |J  /|
 *        F| H  |  K |B
 *      _  |  \ | /  |
 * COL |_| --G-- --M--
 *         |   /| \  |
 *        E|  Q |  N |C
 *      _  | /  |P  \|
 * DP  |_| -----------
 *              D
 *
 * @param position LCD letter number from 0 to 5
 * @param mask `mask' corresponds to bits in lexicographic order:
 *  mask & 1 == A, mask & 2 == B and so on.
 * @param clear_before Should we clear memory before writing segments' bits
 */
static void write_mask_to_lcd_ram(int position, uint16_t mask, bool clear_before)
{
    // Every pixel of character at position can be accessed
    // as LCD_RAM_COMx & (1 << Px)
    int P1,P2,P3,P4;
    if (position < 2) P1 = 2*position;
    else P1 = 2*position+4;
    
    if (position == 1) P2 = P1+5;
    else P2 = P1+1;
    
    if (position < 3) P3 = (23-2*position)+6;
    else P3 = (23-2*position)+4;

    if (position == 5) {
        P4 = P3;
        P3 -= 1;
    } else {
        P4 = P3 - 1;
    }

    uint32_t COM0,COM1,COM2,COM3;
    COM0 = LCD_RAM_COM0;
    COM1 = LCD_RAM_COM1;
    COM2 = LCD_RAM_COM2;
    COM3 = LCD_RAM_COM3;

    if (clear_before) {
        COM0&= ~(1<<P1 | 1<<P2 | 1<<P3 | 1<<P4);
        COM1&= ~(1<<P1 | 1<<P2 | 1<<P3 | 1<<P4);
        COM2&= ~(1<<P1 | 1<<P2 | 1<<P3 | 1<<P4);
        COM3&= ~(1<<P1 | 1<<P2 | 1<<P3 | 1<<P4);
    }
    
    COM0 |= ((mask >> 0x1) & 1) << P4 | ((mask >> 0x4) & 1) << P1 |
        ((mask >> 0x6) & 1) << P3 | ((mask >> 0xA) & 1) << P2;
    COM1 |= ((mask >> 0x0) & 1) << P4 | ((mask >> 0x2) & 1) << P2 |
        ((mask >> 0x3) & 1) << P1 | ((mask >> 0x5) & 1) << P3;
    COM3 |= ((mask >> 0x7) & 1) << P3 | ((mask >> 0x8) & 1) << P4 |
        ((mask >> 0xB) & 1) << P1 | ((mask >> 0xE) & 1) << P2;
    COM2 |= ((mask >> 0x9) & 1) << P4 | ((mask >> 0xC) & 1) << P1 |
        ((mask >> 0xD) & 1) << P3 | ((mask >> 0xF) & 1) << P2;

    LCD_RAM_COM0 = COM0;
    LCD_RAM_COM1 = COM1;
    LCD_RAM_COM2 = COM2;
    LCD_RAM_COM3 = COM3;
}

/**
 * @param position LCD letter number from 0 to 5
 * @param symbol ASCII character
 * @param clear_before Should we clear memory before writing segments' bits
 */
static void write_char_to_lcd_ram(int position, uint8_t symbol, bool clear_before)
{
    uint16_t from_ascii[0x60] = {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        //         !       "       #       $      %        &       '
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        // (       )       *       +       ,       -       .       /
        0x0000, 0x0000, 0x3FC0, 0x1540, 0x0000, 0x0440, 0x4000, 0x2200,
        // 0       1       2       3       4       5       6       7
        0x003F, 0x0006, 0x045B, 0x044F, 0x0466, 0x046D, 0x047D, 0x2201,
        // 8       9       :       ;       <       =       >       ?
        0x047F, 0x046F, 0x8000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        // @       A       B       C       D       E       F       G
        0x0000, 0x0477, 0x047C, 0x0039, 0x045E, 0x0479, 0x0471, 0x043D,
        // H       I       J       K       L       M       N       O
        0x0476, 0x1109, 0x001E, 0x1B00, 0x0038, 0x02B6, 0x08B6, 0x003F,
        // P       Q       R       S       T       U       V       W
        0x0473, 0x0467, 0x0C73, 0x046D, 0x1101, 0x003E, 0x0886, 0x2836,
        // X       Y       Z       [       \       ]       ^       _
        0x2A80, 0x1280, 0x2209, 0x0000, 0x0880, 0x0000, 0x0000, 0x0008
    };
    // Masks not defined. Nothing to display
    if (symbol > 0x60) {
        return;
    }

    write_mask_to_lcd_ram(position, from_ascii[symbol], clear_before);
}

/**
 * Shows maximal possible part of the given text on LCD screen. If text is too
 * short, shows it cycled.
 * @param {char*} text Text to show
 * @param {int} offset Positive offset inside the text from which we should
 *  start showing it.
 */
static void lcd_show(char* text, int offset)
{
    const int TEXT_LENGTH = strlen(text);

    do {} while (!lcd_is_for_update_ready());

    for (int i=0; i<LCD_LETTERS_COUNT; ++i) {
        write_char_to_lcd_ram(i, text[(offset + i) % TEXT_LENGTH], true);
    }

    lcd_update();
}

int main(void)
{
    const int TEXT_LENGTH = strlen(TEXT);

    lcd_init();

    for (int offset=0; ; ++offset, offset%=TEXT_LENGTH) {
        lcd_show(TEXT, offset);

        // Wait a bit
        for (int i=0; i<100000; ++i) {
            __asm__("nop");
        }
    }

    return 0;
}
