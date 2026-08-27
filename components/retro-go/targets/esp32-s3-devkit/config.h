/****************************************************************************
 * Target definition — Hands On Lab (kit educacional, ESP32-S3, flash 16 MB)*
 *                                                                          *
 * Derivado de targets/esp32-s3-devkit do retro-go (ducalex), branch dev.    *
 * Diferenças em relação a ele:                                             *
 *   - gamepad em 10 GPIOs em vez de D-pad em ADC                           *
 *   - combos virtuais para MENU e OPTION                                   *
 *   - armazenamento na partição `vfs` interna (o kit não tem cartão SD)     *
 *   - updater desligado (as releases do ducalex não servem para este kit)   *
 ****************************************************************************/
#define RG_TARGET_NAME             "HANDS-ON-LAB"


/****************************************************************************
 * Status LED                                                               *
 ****************************************************************************/
#define RG_LED_DRIVER               1   // 1 = GPIO
#define RG_GPIO_LED                 GPIO_NUM_38
// #define RG_GPIO_LED_INVERT          // Descomente se o LED for ativo em nível baixo


/****************************************************************************
 * I2C / GPIO Extender                                                      *
 ****************************************************************************/
// #define RG_I2C_GPIO_DRIVER          0   // 1 = AW9523, 2 = PCF9539, 3 = MCP23017, 4 = PCF8575, 5 = PCF8574
// #define RG_I2C_GPIO_ADDR            0x00
// #define RG_GPIO_I2C_SDA             GPIO_NUM_15
// #define RG_GPIO_I2C_SCL             GPIO_NUM_4


/****************************************************************************
 * Storage                                                                  *
 *                                                                          *
 * O rg_storage tenta o cartão SD primeiro e cai para a partição de flash    *
 * se ele não montar. O kit não tem slot de SD, mas o firmware de referência *
 * (v1a) foi compilado com o bloco SDSPI ativo — mantido aqui por paridade.  *
 * Comentar as quatro linhas de SDSPI economiza tempo de boot e libera os    *
 * GPIOs 9/10/11/13, mas é mudança de comportamento: testar antes.           *
 ****************************************************************************/
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_FLASH_PARTITION  "vfs"
#define RG_STORAGE_SDSPI_HOST       SPI3_HOST
#define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_DEFAULT
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_9
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_11
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_13
#define RG_GPIO_SDSPI_CS            GPIO_NUM_10


/****************************************************************************
 * Audio                                                                    *
 ****************************************************************************/
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Desligado, 1 = GPIO25, 2 = GPIO26, 3 = Ambos
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Desligado, 1 = Ligado
#define RG_AUDIO_USE_BUZZER_PIN     0   // Ver drivers/audio/buzzer.c
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_41
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_42
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_40
// #define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_18


/****************************************************************************
 * Video — display SPI 240 x 320 (usado na horizontal, 320 x 240)           *
 ****************************************************************************/
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341/ST7789
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_40M
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATION          3   // 0-7
#define RG_SCREEN_RGB_BGR           1   // 0-1 (trocar se as cores saírem erradas)
#define RG_SCREEN_PIXEL_FORMAT      0   // 0 = 565_BE, 1 = 565_LE
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0} // left, top, right, bottom
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0} // left, top, right, bottom
#define RG_SCREEN_PARTIAL_UPDATES   1
#define RG_SCREEN_INIT()                                                                                         \
    ILI9341_CMD(0xCF, 0x00, 0xc3, 0x30);                                                                         \
    ILI9341_CMD(0xED, 0x64, 0x03, 0x12, 0x81);                                                                   \
    ILI9341_CMD(0xE8, 0x85, 0x00, 0x78);                                                                         \
    ILI9341_CMD(0xCB, 0x39, 0x2c, 0x00, 0x34, 0x02);                                                             \
    ILI9341_CMD(0xF7, 0x20);                                                                                     \
    ILI9341_CMD(0xEA, 0x00, 0x00);                                                                               \
    ILI9341_CMD(0xC0, 0x1B);                 /* Power control   //VRH[5:0] */                                    \
    ILI9341_CMD(0xC1, 0x12);                 /* Power control   //SAP[2:0];BT[3:0] */                            \
    ILI9341_CMD(0xC5, 0x32, 0x3C);           /* VCM control */                                                   \
    ILI9341_CMD(0xC7, 0x91);                 /* VCM control2 */                                                  \
    ILI9341_CMD(0xB1, 0x00, 0x10);           /* Frame Rate Control (1B=70, 1F=61, 10=119) */                     \
    ILI9341_CMD(0xB6, 0x0A, 0xA2);           /* Display Function Control */                                      \
    ILI9341_CMD(0xF6, 0x01, 0x30);                                                                               \
    ILI9341_CMD(0xF2, 0x00);                 /* 3Gamma Function Disable */                                       \
    ILI9341_CMD(0x26, 0x01);                 /* Gamma curve selected */                                          \
    ILI9341_CMD(0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00); \
    ILI9341_CMD(0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);
#define RG_SCREEN_DEINIT() \
    /* Nada a fazer */
#define RG_GPIO_LCD_MISO            GPIO_NUM_NC
#define RG_GPIO_LCD_MOSI            GPIO_NUM_12
#define RG_GPIO_LCD_CLK             GPIO_NUM_48
#define RG_GPIO_LCD_CS              GPIO_NUM_NC
#define RG_GPIO_LCD_DC              GPIO_NUM_47
#define RG_GPIO_LCD_BCKL            GPIO_NUM_39
// #define RG_GPIO_LCD_BCKL_INVERT     // Descomente se o backlight for ativo em nível baixo
#define RG_GPIO_LCD_RST             GPIO_NUM_3


/****************************************************************************
 * Input — 10 botões em GPIO, todos com pull-up e ativos em nível baixo     *
 ****************************************************************************/
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_SELECT, .num = GPIO_NUM_16, .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_17, .pullup = 1, .level = 0},\
    {RG_KEY_Y,      .num = GPIO_NUM_18, .pullup = 1, .level = 0},\
    {RG_KEY_X,      .num = GPIO_NUM_8,  .pullup = 1, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_14, .pullup = 1, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_19, .pullup = 1, .level = 0},\
    {RG_KEY_UP,     .num = GPIO_NUM_20, .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_21, .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_45, .pullup = 1, .level = 0},\
    {RG_KEY_LEFT,   .num = GPIO_NUM_46, .pullup = 1, .level = 0},\
}

// O kit não tem botões dedicados de MENU e OPTION — eles saem de combos.
// MENU   = SELECT + UP
// OPTION = SELECT + DOWN
#define RG_GAMEPAD_VIRT_MAP {\
    {RG_KEY_MENU,   RG_KEY_SELECT | RG_KEY_UP},\
    {RG_KEY_OPTION, RG_KEY_SELECT | RG_KEY_DOWN},\
}


/****************************************************************************
 * Battery — 18650 via divisor no ADC1 canal 3                              *
 ****************************************************************************/
#define RG_BATTERY_DRIVER           1
#define RG_BATTERY_ADC_UNIT         ADC_UNIT_1
#define RG_BATTERY_ADC_CHANNEL      ADC_CHANNEL_4
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3500.f) / (4200.f - 3500.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)


/****************************************************************************
 * Updater                                                                  *
 *                                                                          *
 * Por padrão o launcher oferece "Update Retro-Go" apontando para as         *
 * releases do ducalex, que NÃO servem para este kit (partições e target     *
 * diferentes). Desligado até existir um endpoint nosso.                     *
 ****************************************************************************/
#define RG_UPDATER_ENABLE           0
// Quando houver releases próprias, trocar por:
// #define RG_UPDATER_ENABLE           1
// #define RG_UPDATER_GITHUB_RELEASES  "https://api.github.com/repos/luciano-cognisense/retro-go/releases?per_page=10"
