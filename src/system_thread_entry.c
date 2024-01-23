#include "system_thread.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define LOG_TO_FILE                (0)
#define STDIO_FILE_IDS             (3)

#define MEMPOOL_SIZE               (63488)
#define UX_FSP_DEVICE_INSERTION    (0x01U)
#define UX_FSP_DEVICE_REMOVAL      (0x02U)
#define EVENT_USB_PLUG_IN          (1UL << 0)
#define EVENT_USB_PLUG_OUT         (1UL << 1)
#define MAX_OPEN_FILE_COUNT        (5)
#define DOOM_THREAD_STACK_SIZE      (4096)

#define MIPI_DSI_DISPLAY_CONFIG_DATA_DELAY_FLAG      ((mipi_dsi_cmd_id_t) 0xFE)
#define MIPI_DSI_DISPLAY_CONFIG_DATA_END_OF_TABLE    ((mipi_dsi_cmd_id_t) 0xFD)

#define BLUE                                         (0x000000FF)
#define LIME                                         (0xFF00FF00)
#define RED                                          (0x00FF0000)
#define BLACK                                        (0x00000000)
#define WHITE                                        (0xFFFFFFFF)
#define YELLOW                                       (0xFFFFFF00)
#define AQUA                                         (0xFF00FFFF)
#define MAGENTA                                      (0x00FF00FF)

static uint8_t  g_ux_pool_memory[MEMPOOL_SIZE];
static uint8_t doom_thread_stack[DOOM_THREAD_STACK_SIZE] BSP_PLACE_IN_SECTION(".stack.doom_thread") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);

TX_THREAD doom_thread;
static FX_MEDIA * g_p_media           = UX_NULL;
char volume_name[256];
volatile bool g_vsync_flag;

FX_FILE file_array[MAX_OPEN_FILE_COUNT];

FILE* filedes[MAX_OPEN_FILE_COUNT];

char dbg_info[256];

volatile bool g_message_sent = false;
volatile mipi_dsi_phy_status_t g_phy_status;
/* Variables to store resolution information */
uint16_t g_hz_size, g_vr_size;
/* Variables used for buffer usage */
uint32_t g_buffer_size, g_hstride;

enum {
    STATUS_LED_BLUE,
    STATUS_LED_GREEN,
    STATUS_LED_RED,
};

typedef struct
{
    int argc;
    char ** argv;
}args_t;

typedef struct
{
    unsigned char        size;
    unsigned char        buffer[10];
    mipi_dsi_cmd_id_t    cmd_id;
    mipi_dsi_cmd_flag_t flags;
} lcd_table_setting_t;

const lcd_table_setting_t g_lcd_init_focuslcd[] =
{
 {6,  {0xFF, 0xFF, 0x98, 0x06, 0x04, 0x01}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE,        MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x08, 0x10},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x21, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0x30, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x31, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0x40, 0x14},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x41, 0x33},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x42, 0x02},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x43, 0x09},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x44, 0x06},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x50, 0x70},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x51, 0x70},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x52, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x53, 0x48},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x60, 0x07},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x61, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x62, 0x08},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x63, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0xa0, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa1, 0x03},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa2, 0x09},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa3, 0x0d},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa4, 0x06},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa5, 0x16},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa6, 0x09},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa7, 0x08},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa8, 0x03},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xa9, 0x07},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xaa, 0x06},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xab, 0x05},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xac, 0x0d},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xad, 0x2c},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xae, 0x26},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xaf, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0xc0, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc1, 0x04},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc2, 0x0b},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc3, 0x0f},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc4, 0x09},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc5, 0x18},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc6, 0x07},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc7, 0x08},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc8, 0x05},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xc9, 0x09},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xca, 0x07},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xcb, 0x05},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xcc, 0x0c},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xcd, 0x2d},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xce, 0x28},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xcf, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {6,  {0xFF, 0xFF, 0x98, 0x06, 0x04, 0x06}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE,        MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x00, 0x21},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x01, 0x09},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x02, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x03, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x04, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x05, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x06, 0x80},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x07, 0x05},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x08, 0x02},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x09, 0x80},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x0a, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x0b, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x0c, 0x0a},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x0d, 0x0a},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x0e, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x0f, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x10, 0xe0},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x11, 0xe4},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x12, 0x04},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x13, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x14, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x15, 0xc0},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x16, 0x08},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x17, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x18, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x19, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x1a, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x1b, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x1c, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x1d, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0x20, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x21, 0x23},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x22, 0x45},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x23, 0x67},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x24, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x25, 0x23},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x26, 0x45},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x27, 0x67},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0x30, 0x01},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x31, 0x11},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x32, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x33, 0xee},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x34, 0xff},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x35, 0xcb},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x36, 0xda},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x37, 0xad},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x38, 0xbc},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x39, 0x76},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3a, 0x67},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3b, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3c, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3d, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3e, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3f, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x40, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {2,  {0x53, 0x10},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x54, 0x10},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {6,  {0xFF, 0xFF, 0x98, 0x06, 0x04, 0x07}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE,        MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x18, 0x1d},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x26, 0xb2},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x02, 0x77},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0xe1, 0x79},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x17, 0x22},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {6,  {0xFF, 0xFF, 0x98, 0x06, 0x04, 0x00}, MIPI_DSI_CMD_ID_DCS_LONG_WRITE,        MIPI_DSI_CMD_FLAG_LOW_POWER},
 {120, {0},                                 MIPI_DSI_DISPLAY_CONFIG_DATA_DELAY_FLAG,   (mipi_dsi_cmd_flag_t)0},
 {2,  {0x11, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_0_PARAM,       MIPI_DSI_CMD_FLAG_LOW_POWER},
 {5,   {0},                                 MIPI_DSI_DISPLAY_CONFIG_DATA_DELAY_FLAG,   (mipi_dsi_cmd_flag_t)0},
 {2,  {0x29, 0x00},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_0_PARAM,       MIPI_DSI_CMD_FLAG_LOW_POWER},
 {2,  {0x3a, 0x70},                         MIPI_DSI_CMD_ID_DCS_SHORT_WRITE_1_PARAM, MIPI_DSI_CMD_FLAG_LOW_POWER},

 {0x00, {0},                                MIPI_DSI_DISPLAY_CONFIG_DATA_END_OF_TABLE, (mipi_dsi_cmd_flag_t)0},
};


char * args[] = {
       [0] = "doomgeneric",
       "-iwad",
       "/doom1.wad",
       "-nosound",
       "-nomusic",
       "-nosfx",
       "-playdemo"
//       "-timedemo",
//       "demo3"

};

args_t message =
{
 .argc = sizeof(args)/sizeof(args[0]),
 .argv = args,
};

void doom_thread_entry(void);

static UINT apl_change_function (ULONG event, UX_HOST_CLASS * host_class, VOID * instance)
{
    UINT                          status = UX_SUCCESS;
    UX_HOST_CLASS               * class;
    UX_HOST_CLASS_STORAGE       * storage;
    UX_HOST_CLASS_STORAGE_MEDIA * storage_media;
    FSP_PARAMETER_NOT_USED      ((void) instance);

    /* Check the class container if it is for a USBX Host Mass Storage class. */
    if (UX_SUCCESS ==
        _ux_utility_memory_compare(_ux_system_host_class_storage_name, host_class,
                                   _ux_utility_string_length_get(_ux_system_host_class_storage_name)))
    {
        /* Check if there is a device insertion. */
        if (UX_FSP_DEVICE_INSERTION == event)
        {
            status = ux_host_stack_class_get(_ux_system_host_class_storage_name, &class);
            if (UX_SUCCESS != status)
            {
                return status;
            }
            status = ux_host_stack_class_instance_get(class, 0, (void **) &storage);
            if (UX_SUCCESS != status)
            {
                return status;
            }
            if (UX_HOST_CLASS_INSTANCE_LIVE != storage->ux_host_class_storage_state)
            {
                return UX_ERROR;
            }
            storage_media = class->ux_host_class_media;
            g_p_media     = &storage_media->ux_host_class_storage_media;
            tx_event_flags_set(&g_usb_plug_events, EVENT_USB_PLUG_IN, TX_OR);
        }
        else if (UX_FSP_DEVICE_REMOVAL == event) /* Check if there is a device removal. */
        {
            g_p_media = UX_NULL;
            tx_event_flags_set(&g_usb_plug_events, EVENT_USB_PLUG_OUT, TX_OR);
        }
        else
        {
            ;
        }
    }
    return status;
}

void doom_thread_entry(void)
{


    while (1)
    {
        tx_thread_sleep (1);
    }
}

static void doom_thread_func(ULONG thread_input)
{
    extern TX_THREAD system_thread;
    extern void doomgeneric_Create(int argc, char **argv);

    UINT       fx_return = UX_SUCCESS;

    char * p_default_directory = NULL;

    args_t * p_msg =  (args_t *)thread_input;

    /* Get the current local path */
    fx_return = fx_directory_local_path_get(g_p_media, &p_default_directory);
    if(FX_SUCCESS != fx_return)
    {
        __BKPT(0);
    }

    if(p_default_directory == NULL)
    {
        FX_LOCAL_PATH     my_previous_local_path;
        fx_return = fx_directory_local_path_set(g_p_media, &my_previous_local_path, "\\");
        if(FX_SUCCESS != fx_return)
        {
            __BKPT(0);
        }
    }


    /* TODO: add your own code here */
#if 1
    doomgeneric_Create(p_msg->argc, p_msg->argv);
#endif

    while(1)
        {
            doomgeneric_Tick();
        }

    __BKPT(0);

    tx_thread_terminate(tx_thread_identify);

    tx_thread_resume(&system_thread);

    return;
}

void mipi_dsi_push_table (const lcd_table_setting_t *table)
{
    fsp_err_t err = FSP_SUCCESS;
    const lcd_table_setting_t *p_entry = table;

    while (MIPI_DSI_DISPLAY_CONFIG_DATA_END_OF_TABLE != p_entry->cmd_id)
    {
        mipi_dsi_cmd_t msg =
        {
          .channel = 0,
          .cmd_id = p_entry->cmd_id,
          .flags = p_entry->flags,
          .tx_len = p_entry->size,
          .p_tx_buffer = p_entry->buffer,
        };

        if (MIPI_DSI_DISPLAY_CONFIG_DATA_DELAY_FLAG == msg.cmd_id)
        {
            R_BSP_SoftwareDelay (table->size, BSP_DELAY_UNITS_MILLISECONDS);
        }
        else
        {
            g_message_sent = false;
            /* Send a command to the peripheral device */
            err = R_MIPI_DSI_Command (&g_mipi_dsi0_ctrl, &msg);

            /* Wait */
            while (!g_message_sent);
        }
        p_entry++;
    }
}


/*******************************************************************************************************************//**
 * @brief      User-defined function to draw the current display to a framebuffer.
 *
 * @param[in]  framebuffer    Pointer to frame buffer.
 * @retval     None.
 **********************************************************************************************************************/
static void display_draw (uint32_t * framebuffer)
{
    /* Draw buffer */
    uint32_t color[]= {BLUE, LIME, RED, BLACK, WHITE, YELLOW, AQUA, MAGENTA};
    uint16_t bit_width = g_hz_size / (sizeof(color)/sizeof(color[0]));
    for (uint32_t y = 0; y < g_vr_size; y++)
    {
        for (uint32_t x = 0; x < g_hz_size; x ++)
        {
            uint32_t bit       = x / bit_width;
            framebuffer[x] = color [bit];
        }
        framebuffer += g_hstride;
    }
}

/*******************************************************************************************************************//**
 * @brief      Callback functions for MIPI DSI interrupts
 *
 * @param[in]  p_args    Callback arguments
 * @retval     none
 **********************************************************************************************************************/
void mipi_dsi_callback(mipi_dsi_callback_args_t *p_args)
{
    switch (p_args->event)
    {
        case MIPI_DSI_EVENT_SEQUENCE_0:
        {
            if (MIPI_DSI_SEQUENCE_STATUS_DESCRIPTORS_FINISHED == p_args->tx_status)
            {
                g_message_sent = true;
            }
            break;
        }
        case MIPI_DSI_EVENT_PHY:
        {
            g_phy_status |= p_args->phy_status;
            break;
        }
        default:
        {
            break;
        }

    }
}



void gpt_callback(timer_callback_args_t *p_args)
{
    /* Check for the event */
    if (TIMER_EVENT_CYCLE_END == p_args->event)
    {
        return;
    }
}

/* SystemThread entry function */
void system_thread_entry(void)
{
    UINT       ux_return = UX_SUCCESS;
    UINT       fx_return = UX_SUCCESS;
    fsp_err_t err = FSP_SUCCESS;
    ULONG    actual_flags          = 0;
    ULONG system_status = 0;
    char * p_root_directory = NULL;
    char * p_default_directory = NULL;
    ioport_instance_t * p_ioport = &g_ioport;

    void tx_startup_err_callback(void *p_instance, void *p_data);

    memset(volume_name, 0, sizeof(volume_name));

    /* Create files for STDIN, STDOUT, STDERR */
#if (LOG_TO_FILE > 0)
    {
        const char * filenames[] =
        {
         [0] = "\\stdin.txt",
         [1] = "\\stdout.txt",
         [2] = "\\stderr.txt",
        };

        for (uint32_t itr = 0 ; itr < sizeof(filenames)/sizeof(filenames[0]); itr++)
        {
            filedes[itr] = fopen(&filenames[itr][0], "w+");
        }
    }
#else
    {
        err = g_comms_uart_stdio.p_api->open(g_comms_uart_stdio.p_ctrl,
                                             g_comms_uart_stdio.p_cfg);

        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }
        uint8_t init_message[] = "Debug Comms Initialized!\r\n";
        err = g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl,
                                              &init_message[0],
                                              sizeof(init_message));

    }
#endif

    /* Initialize SDRAM */
    {
        bsp_sdram_init();
        uint8_t init_message[] = "SDRAM Initialized!\r\n";
        err = g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl,
                                              &init_message[0],
                                              sizeof(init_message));
    }

    g_ioport.p_api->pinWrite(g_ioport.p_ctrl, DISP_RST, BSP_IO_LEVEL_LOW);

    {
        UINT tx_err = TX_SUCCESS;
        /* Enter reset keeping both INT and RESET pin low*/
        {
            err = p_ioport->p_api->pinCfg(p_ioport->p_ctrl,
                                          DISP_INT,
                                          ((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                                                  (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW));
            if (err != FSP_SUCCESS)
                return err;

            err = p_ioport->p_api->pinCfg(p_ioport->p_ctrl,
                                          DISP_RST,
                                          ((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                                                  (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW));
            if (err != FSP_SUCCESS)
                return err;
        }

        /* Sleep for 10 ms*/
        {
            UINT tx_err = TX_SUCCESS;
            tx_err = tx_thread_sleep(10);
            if(TX_SUCCESS != tx_err)
                return FSP_ERR_ABORTED;
        }

        /* First set INT pin high */
        err = R_IOPORT_PinWrite(p_ioport->p_ctrl, DISP_INT, BSP_IO_LEVEL_HIGH);
        if (err != FSP_SUCCESS)
            return err;

        /* Wait for 100 us */
#if (BSP_CFG_RTOS == 0)

#elif (BSP_CFG_RTOS == 1)
        tx_err = tx_thread_sleep(1);
        if(TX_SUCCESS != tx_err)
            return FSP_ERR_ABORTED;
#elif (BSP_CFG_RTOS == 2)
        {
            TickType_t xDelay = 1 / portTICK_PERIOD_MS;
            TickType_t xLastTick = xTaskGetTickCount();
            vTaskDelayUntil(&xLastTick, xDelay);
        }
#endif

        /* Then RESET can go high */
        err = R_IOPORT_PinWrite(p_ioport->p_ctrl, DISP_RST, BSP_IO_LEVEL_HIGH);
        if (err != FSP_SUCCESS)
            return err;

        /* Wait for 5 ms*/
#if (BSP_CFG_RTOS == 0)

#elif (BSP_CFG_RTOS == 1)
        tx_err = tx_thread_sleep(5);
        if(TX_SUCCESS != tx_err)
            return FSP_ERR_ABORTED;
#elif (BSP_CFG_RTOS == 2)
        {
            TickType_t xDelay = 6 / portTICK_PERIOD_MS;
            TickType_t xLastTick = xTaskGetTickCount();
            vTaskDelayUntil(&xLastTick, xDelay);
        }
#endif

        /* Then INT pin can go low */
        R_IOPORT_PinWrite(p_ioport->p_ctrl, DISP_INT, BSP_IO_LEVEL_LOW);
        if (err != FSP_SUCCESS)
            return err;

        /* Wait for 50 ms */
#if (BSP_CFG_RTOS == 0)

#elif (BSP_CFG_RTOS == 1)
        tx_err = tx_thread_sleep(50);
        if(TX_SUCCESS != tx_err)
            return FSP_ERR_ABORTED;
#elif (BSP_CFG_RTOS == 2)
        {
            TickType_t xDelay = 52 / portTICK_PERIOD_MS;
            TickType_t xLastTick = xTaskGetTickCount();
            vTaskDelayUntil(&xLastTick, xDelay);
        }
#endif

        /* Set IRQ pin as input */
        err = p_ioport->p_api->pinCfg(p_ioport->p_ctrl,
                                      DISP_INT,
                                      ((uint32_t) IOPORT_CFG_IRQ_ENABLE |
                                              (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT));
        if (err != FSP_SUCCESS)
            return err;

    }




    /* Initialize LCD screen */
    {
        extern const display_cfg_t g_display_cfg;
        /* Initialize GLCDC module */
        err = R_GLCDC_Open(&g_display_ctrl, &g_display_cfg);
        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }


        /* Initialize GPT module */
        err = R_GPT_Open(&g_timer_PWM_ctrl, &g_timer_PWM_cfg);
        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

        /* Enable GPT Timer */
        err = R_GPT_Enable(&g_timer_PWM_ctrl);
        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

        /* Start GPT timer */
        err = R_GPT_Start(&g_timer_PWM_ctrl);
        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

        /* Initialize LCD. */
        mipi_dsi_push_table(g_lcd_init_focuslcd);

        /* Get LCDC configuration */
        g_hz_size = (g_display_cfg.input[0].hsize);
        g_vr_size = (g_display_cfg.input[0].vsize);
        g_hstride = (g_display_cfg.input[0].hstride);

#if (SHOW_COLOR_BANDS_ON_INIT)
        /* Draw a test image into the buffer */
        display_draw(fb_background[0]);
        display_draw(fb_background[1]);
#else
        memset(fb_background, 0, sizeof(fb_background));
#if (BSP_CFG_DCACHE_ENABLED == 1)
    /* Synchronize the Data cache to SDRAM */
    __DSB();
#endif
#endif
        /* Start video mode */
        err = R_GLCDC_Start(&g_display_ctrl);
        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }
    }

    /* Open DAVE2D */
    {
        extern d2_device *d2_handle;

        d2_s32 d2_err = D2_OK;
        d2_handle = d2_opendevice(0);
        if(D2_OK != d2_err){__BKPT(0);};

        d2_err = d2_inithw(d2_handle, 0);
        if(D2_OK != d2_err){__BKPT(0);};

        /* Set various D2 parameters */
        d2_err = d2_setblendmode(d2_handle, d2_bm_alpha, d2_bm_one_minus_alpha);
        if(D2_OK != d2_err){__BKPT(0);};
        d2_err = d2_setalphamode(d2_handle, d2_am_constant);
        if(D2_OK != d2_err){__BKPT(0);};
        d2_err = d2_setalpha(d2_handle, 0xff);
        if(D2_OK != d2_err){__BKPT(0);};
//        d2_err = d2_setblur( d2_handle, 4*16 );
//        if(D2_OK != d2_err){__BKPT(0);};

    }
    /* fx_system_initialization */
    fx_system_initialize();

    /* ux_system_initialization */
    ux_return = _ux_system_initialize((CHAR *)g_ux_pool_memory, MEMPOOL_SIZE, UX_NULL, 0);

    if(UX_SUCCESS != ux_return)
    {
        __BKPT(0);
    }

    /* ux host stack initialization */
    ux_return =  ux_host_stack_initialize(apl_change_function);
    if (UX_SUCCESS != ux_return)
    {
        __BKPT(0);
    }

    /* Open usb driver */
    err = R_USB_Open(&g_basic0_ctrl, &g_basic0_cfg);

    if(FSP_SUCCESS != err)
    {
        __BKPT(0);
    }

    /* Indicate waiting for insertion with the Yellow LED */
    {
        UINT tx_err = TX_SUCCESS;
        system_status = STATUS_LED_BLUE;
        tx_err = tx_queue_send(&g_led_status_queue, &system_status, TX_WAIT_FOREVER);

        if(TX_SUCCESS != tx_err)
        {
            __BKPT(0);
        }
    }

    /*  Wait until device inserted.*/
    tx_event_flags_get (&g_usb_plug_events, EVENT_USB_PLUG_IN, TX_AND_CLEAR, &actual_flags, TX_WAIT_FOREVER);

    if(EVENT_USB_PLUG_IN == actual_flags)
    {
        /* Mount the USB drive */
        fx_return = fx_media_volume_get(g_p_media, &volume_name[0], FX_DIRECTORY_SECTOR);

        if(FX_SUCCESS != fx_return)
        {
            __BKPT(0);
        }

        /* Set the directory to the root directory */
        fx_return = fx_directory_default_get(g_p_media, &p_root_directory);
        if(FX_SUCCESS != fx_return)
        {
            __BKPT(0);
        }
        fx_directory_default_set(g_p_media,"\\");

        /* Get the current local path */
        fx_return = fx_directory_local_path_get(g_p_media, &p_default_directory);
        if(FX_SUCCESS != fx_return)
        {
            __BKPT(0);
        }

        if(p_default_directory == NULL)
        {
            FX_LOCAL_PATH     my_previous_local_path;
            fx_return = fx_directory_local_path_set(g_p_media, &my_previous_local_path, "\\");
            if(FX_SUCCESS != fx_return)
            {
                __BKPT(0);
            }
        }

        /* Check if the doom1.wad file exists. */
        {
            UINT attributes = 0;
            fx_return = fx_file_attributes_read(g_p_media, &args[2][0], &attributes);
            if(FX_SUCCESS != fx_return)
            {
                __BKPT(0);
            }
        }

        /* Indicate running with the Green LED */
        {
            UINT tx_err = TX_SUCCESS;
            system_status = STATUS_LED_GREEN;
            tx_err = tx_queue_send(&g_led_status_queue, &system_status, TX_WAIT_FOREVER);

            if(TX_SUCCESS != tx_err)
            {
                __BKPT(0);
            }


        }

        /* Enable the thread running doom */
        {
            UINT tx_err;
            tx_err = tx_thread_create (&doom_thread,
                                       (CHAR*) "Doom Thread",
                                       doom_thread_func, (ULONG) &message,
                                       &doom_thread_stack, DOOM_THREAD_STACK_SIZE,
                                       3, 1,
                                       1, TX_AUTO_START);
            if (TX_SUCCESS != tx_err)
            {
                __BKPT(0);
                tx_startup_err_callback (&doom_thread, 0);
            }
        }

    }

    tx_thread_suspend(tx_thread_identify());
#if (LOG_TO_FILE > 0)
    /* Close files for STDIN, STDOUT, STDERR */
    {
        const char * filenames[] =
        {
         [0] = "\\stdin.txt",
         [1] = "\\stdout.txt",
         [2] = "\\stderr.txt",
        };

        for (uint32_t itr = 0 ; itr < sizeof(filenames)/sizeof(filenames[0]); itr++)
        {
            fclose(filedes[itr]);
        }
    }
#endif

    tx_event_flags_get (&g_usb_plug_events, EVENT_USB_PLUG_OUT, TX_AND_CLEAR, &actual_flags, TX_WAIT_FOREVER);

    if(EVENT_USB_PLUG_OUT == actual_flags)
    {
        __BKPT(0);
        /* Pause the thread running DOOM */

        /* Close any open files */

        /*close the media*/
        fx_return = fx_media_close(g_p_media);
        if (FX_SUCCESS != fx_return)
        {
            __BKPT(0);
        }

        /* Indicate stop with the RED LED */


    }

    while (1)
    {
        tx_thread_sleep (1);
    }
}
#if 1

static int file_open_count = 3;

/*
 * FX_SUCCESS (0x00) Supplied name is a directory.
FX_MEDIA_NOT_OPEN (0x11) Specified media is not open
FX_NOT_FOUND (0x04) Directory entry could not be found.
FX_NOT_DIRECTORY (0x0E) Entry is not a directory
FX_IO_ERROR (0x90) Driver I/O error.
FX_MEDIA_INVALID (0x02) Invalid media.
FX_FILE_CORRUPT (0x08) File is corrupted.
FX_SECTOR_INVALID (0x89) Invalid sector.
FX_FAT_READ_ERROR (0x03) Unable to read FAT entry.
FX_NO_MORE_SPACE (0x0A) No more space to complete the operation.
FX_PTR_ERROR (0x18) Invalid media or name pointer.
FX_CALLER_ERROR (0x20) Caller is not a thread.
 */

int mkdir (const char *_path, mode_t __mode )
{
    extern int errno;
    UINT fx_err;
#if 0
    __BKPT(0);
#endif

    FSP_PARAMETER_NOT_USED(__mode);

    size_t path_len = strlen(_path);

    if (path_len == 1)
    {
        if ((_path[0] == '.') || (_path[0] == '\\') || (_path[0] == '/'))
        {
            /* Nothing to do. Current/Root directory already exists */
        }
        return 0;
    }

//    bool style_windows = strchr('\\') != NULL ? true:false;


    char * p_end = (char *)&_path[path_len];
    char * p_dirname = (char *)_path;
    char * p_dirname_end = (char *)_path;
    for(; p_dirname < p_end; p_dirname = p_dirname_end + 1)
    {
        p_dirname_end = strchr(p_dirname,'/');

        if(p_dirname_end != NULL)
        {
            /* directory name is between p_dirname and p_dirname_end*/
            const size_t dirname_len = p_dirname_end - p_dirname;

            if(dirname_len == 1)
            {
                if(p_dirname[0] == '.')
                {
                    /* skip the current directory */
                    continue;
                }
            }

            else if(dirname_len > 0)
            {
                char dirname[dirname_len + 1];
                dirname[dirname_len] = '\0';
                memcpy(dirname, p_dirname, dirname_len);

                /* Test if the directory exists */
                fx_err = fx_directory_name_test(g_p_media, (char*)dirname);

                switch( fx_err)
                {
                    case FX_SUCCESS:
                        errno = EEXIST;
                        return -1;
                        break;

                    case FX_NOT_FOUND:
                    {
                        /* Create the directory */
                        __BKPT(0);
                        fx_err = fx_directory_create(g_p_media, (char*)dirname);
                        if(FX_SUCCESS == fx_err)
                        {
                            fx_media_flush(g_p_media);

                            if(FX_SUCCESS == fx_err)
                            {
                                return 0;
                            }
                        }
                    }
                    break;

                    default:
                        ;
                        break;
                }
            }

        }
    }

    return -1;

}

/** Exit the application */
void exit(int code)
{
    __BKPT(0);
    FSP_PARAMETER_NOT_USED(code);

    TX_THREAD * t = tx_thread_identify();
    UINT tx_err = tx_thread_terminate(t);

    if(TX_SUCCESS == tx_err)
    {
        tx_thread_delete(t);
    }
}

/** Set position in a file. */

/*
 * FX_SUCCESS (0x00) Successful file relative seek.
FX_NOT_OPEN (0x07) Specified file is not currently open.
FX_IO_ERROR (0x90) Driver I/O error.
FX_FILE_CORRUPT (0x08) File is corrupted.
FX_SECTOR_INVALID (0x89) Invalid sector.
FX_NO_MORE_ENTRIES (0x0F) No more FAT entries.
FX_PTR_ERROR (0x18) Invalid file pointer.
FX_CALLER_ERROR (0x20) Caller is not a thread.
 */

int _lseek(int file, off_t pos, int whence)
{
#if DEBUG_DOOM
    __BKPT(0);
#endif
    extern int errno;
    UINT seek_from = 0;
    ULONG byte_offset = 0;

    switch(whence)
    {
        default:
        {
            errno = EINVAL;
            return -1;
        }
        break;

        case SEEK_SET:
        case SEEK_END:
        case SEEK_CUR:
        {
            ;
        }
        break;
    }

    if(file < MAX_OPEN_FILE_COUNT)
    {
        if(SEEK_SET == whence)
        {
            seek_from = FX_SEEK_BEGIN;
            byte_offset = pos;
        }
        else if(SEEK_END == whence)
        {
            seek_from = FX_SEEK_END;
            byte_offset = abs(pos);
        }
        else if(SEEK_CUR == whence)
        {
            if(pos < 0)
            {
                seek_from = FX_SEEK_BACK;
                byte_offset = -pos;
            }
            else
            {
                seek_from = FX_SEEK_FORWARD;
                byte_offset = pos;
            }
        }
        UINT fx_err = fx_file_relative_seek(&file_array[file], byte_offset, seek_from);
        if (FX_SUCCESS == fx_err)
        {
            return (int)file_array[file].fx_file_current_file_offset;
        }
        switch(fx_err)
        {
            case FX_NOT_OPEN:
            case FX_IO_ERROR:
            case FX_CALLER_ERROR:
                errno = EACCES;
            break;

            case FX_PTR_ERROR:
            case FX_FILE_CORRUPT:
            case FX_SECTOR_INVALID:
                errno = EBADF;
            break;

            case FX_NO_MORE_ENTRIES :
                errno = EINVAL;
            break;

            default:
                errno = ENOTSUP;
                break;
        }
    }
    return -1;
}

/** Open a file. */
int _open(const char *file, int flags)
{
#if 1
    snprintf(dbg_info, sizeof(dbg_info), "_open %s, %d\r\n", file, flags );
    g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl, (uint8_t*)dbg_info, strlen(dbg_info));
#endif

    extern int errno;
    int fd;
    UINT fx_err;

    if(file_open_count >= MAX_OPEN_FILE_COUNT)
    {
        /* Cannot create the file. Consider increasing MAX_OPEN_FILE_COUNT */
        errno = EMFILE;
        return -1;
    }

    do
    {
        fx_err = fx_file_open(g_p_media, &file_array[file_open_count], (char*) file, flags & 0x03);

        if(FX_SUCCESS == fx_err)
        {
            fd = file_open_count;
            file_open_count++;
            break;
        }

        if((flags & O_CREAT) && FX_NOT_FOUND == fx_err)
        {
            fx_file_create(g_p_media, file);
        }
    }while(FX_SUCCESS != fx_err);


    return fd;
}

/** Read from a file.
 *
FX_SUCCESS              (0x00) Successful file read.
FX_NOT_OPEN             (0x07) Specified file is not open.
FX_FILE_CORRUPT         (0x08) Specified file is corrupt and the read failed.
FX_END_OF_FILE          (0x09) End of file has been reached.
FX_FILE_CORRUPT         (0x08) File is corrupted.
FX_NO_MORE_SPACE        (0x0A) No more space to complete the operation
FX_IO_ERROR             (0x90) Driver I/O error.
FX_PTR_ERROR            (0x18) Invalid file or buffer pointer.
FX_CALLER_ERROR         (0x20) Caller is not a thread.
*/
int _read(int file, char *ptr, int len)
{
#if (LOG_TO_FILE == 0)
    if(file < STDIO_FILE_IDS)
    {
        fsp_err_t fsp_err = FSP_SUCCESS;

        if (file == 0)
        {
            fsp_err = g_comms_uart_stdio.p_api->read(g_comms_uart_stdio.p_ctrl, (uint8_t * )ptr, (uint32_t) len);

            if(FSP_SUCCESS != fsp_err)
            {
                __BKPT(0);
            }

            return len;
        }
    }
#endif
    extern int errno;
    if(file < MAX_OPEN_FILE_COUNT)
    {
#if DEBUG_DOOM
        __BKPT(0);
#endif
        ULONG actual_length = 0;
        UINT fx_err = fx_file_read(&file_array[file], ptr, len, &actual_length);
#if 0
    snprintf(dbg_info, sizeof(dbg_info), "_read %ld , %d\r\n", actual_length, fx_err );
    g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl, (uint8_t*)dbg_info, strlen(dbg_info));
#endif
        if(FX_SUCCESS == fx_err)
        {
            return actual_length;
        }

        __BKPT(0);
        switch(fx_err)
        {
            case FX_NOT_OPEN     :
            case FX_FILE_CORRUPT :
                errno = EBADF;
                break;
            case FX_END_OF_FILE  :
            case FX_NO_MORE_SPACE:
                errno = EOVERFLOW;
                return actual_length;
                break;
            case FX_IO_ERROR     : errno = ENXIO; break;
            case FX_PTR_ERROR    : errno = EINVAL; break;
            case FX_CALLER_ERROR : errno = EACCES; break;

            default:
                ;
                break;
        }
    }
    else
        errno = EBADF;

    return -1;
}

/** Remove a file’s directory entry.
FX_SUCCESS                 (0x00) Successful file delete.
FX_MEDIA_NOT_OPEN           (0x11) Specified media is not open.
FX_NOT_FOUND                (0x04) Specified file was not found.
FX_NOT_A_FILE               (0x05) Specified file name was a directory or volume.
FX_ACCESS_ERROR             (0x06) Specified file is currently open.
FX_FILE_CORRUPT             (0x08) File is corrupted.
FX_SECTOR_INVALID           (0x89) Invalid sector.
FX_FAT_READ_ERROR           (0x03) Unable to read FAT entry.
FX_NO_MORE_ENTRIES          (0x0F) No more FAT entries.
FX_NO_MORE_SPACE            (0x0A) No more space to complete the operation
FX_IO_ERROR                 (0x90) Driver I/O error.
FX_WRITE_PROTECT            (0x23) Specified media is write protected.
FX_MEDIA_INVALID            (0x02) Invalid media.
FX_PTR_ERROR                (0x18) Invalid media pointer.
FX_CALLER_ERROR             (0x20) Caller is not a thread.
 */
int _unlink(char *name) {
    __BKPT(0);
  extern int errno;
  errno = ENOENT;

  UINT fx_err = fx_file_delete(g_p_media, (char *)name);

  if(FX_SUCCESS == fx_err)
      return 0;

  switch(fx_err)
  {
      case FX_PTR_ERROR      :
      case FX_NOT_FOUND      :
      case FX_NOT_A_FILE     :
      case FX_ACCESS_ERROR   :
          errno = ENOENT;
          break;

      case FX_IO_ERROR       :
      case FX_MEDIA_INVALID  :
      case FX_SECTOR_INVALID :
      case FX_MEDIA_NOT_OPEN :
      case FX_FAT_READ_ERROR :
          errno = ENXIO;
          break;

      case FX_FILE_CORRUPT   :
      case FX_NO_MORE_ENTRIES:
      case FX_NO_MORE_SPACE  :
      case FX_WRITE_PROTECT  :
      case FX_CALLER_ERROR   :
          errno = EACCES;
          break;

      default:
          ;
          break;
  }

  return -1;
}

/**
 * Write to a file. libc subroutines will use this system routine for output to all files,
 * including stdout—so if you need to generate any output, for example to a serial port for debugging,
 * you should make your minimal write capable of doing this.
 * The following minimal implementation is an incomplete example; it relies on a outbyte subroutine
 * (not shown; typically, you must write this in assembler from examples provided by
 * your hardware manufacturer) to actually perform the output.
 FX_SUCCESS (0x00) Successful file write.
 FX_NOT_OPEN (0x07) Specified file is not open.
  - FX_ACCESS_ERROR (0x06) Specified file is not open for writing.
  - FX_NO_MORE_SPACE (0x0A) There is no more room available in the media to perform this write.
  - FX_IO_ERROR (0x90) Driver I/O error.
  - FX_WRITE_PROTECT (0x23) Specified media is write protected.
 - FX_FILE_CORRUPT (0x08) File is corrupted.
 - FX_SECTOR_INVALID (0x89) Invalid sector.
 - FX_FAT_READ_ERROR (0x03) Unable to read FAT entry.
 - FX_NO_MORE_ENTRIES (0x0F) No more FAT entries.
 - FX_PTR_ERROR (0x18) Invalid file or buffer pointer.
 - FX_CALLER_ERROR (0x20) Caller is not a thread.
 */
int _write(int file, char *ptr, int len)
{
#if (LOG_TO_FILE == 0)
    if(file < STDIO_FILE_IDS)
    {
        fsp_err_t fsp_err = FSP_SUCCESS;

        if (file == 2)
        {
            uint8_t stderr_str[] = "STDERR:";
            fsp_err = g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl, &stderr_str[0], strlen(stderr_str));
            if(FSP_SUCCESS != fsp_err)
            {
                __BKPT(0);
            }
        }
        fsp_err = g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl, (uint8_t * )ptr, (uint32_t) len);

        if(FSP_SUCCESS != fsp_err)
        {
            __BKPT(0);
        }

        return len;
    }
#endif

#if 0
    snprintf(dbg_info, sizeof(dbg_info), "_write %d, %s, %d", file, ptr, len );
    fsp_err_t fsp_err = g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl, (uint8_t*)dbg_info, sizeof("\r\n"));
#endif

    __BKPT(0);
    extern int errno;
    if(file < MAX_OPEN_FILE_COUNT)
    {
        __BKPT(0);
        ULONG actual_length = 0;
        UINT fx_err = fx_file_write(&file_array[file], ptr, len);

        if(FX_SUCCESS == fx_err)
        {
            fx_media_flush(g_p_media);
            return actual_length;
        }

        switch(fx_err)
        {
            case FX_PTR_ERROR      :
            case FX_NOT_FOUND      :
            case FX_NOT_A_FILE     :
                errno = EINVAL;
                break;

            case FX_IO_ERROR       :
            case FX_FAT_READ_ERROR :
            case FX_MEDIA_INVALID  :
            case FX_SECTOR_INVALID :
            case FX_MEDIA_NOT_OPEN :
                errno = ENXIO;
                break;

            case FX_NOT_OPEN       :
            case FX_FILE_CORRUPT   :
            case FX_NO_MORE_ENTRIES:
            case FX_NO_MORE_SPACE  :
            case FX_WRITE_PROTECT  :
            case FX_ACCESS_ERROR   :
            case FX_CALLER_ERROR   :
                errno = EACCES;
                break;

            default:
                ;
                break;
        }
    }
    else
        errno = EBADF;

    return -1;
  return len;
}

/** Status of an open file. For consistency with other minimal implementations in these examples,
 * all files are regarded as character special devices.
 * The sys/stat.h header file required is distributed in the include subdirectory for this C library.
 */
int _fstat(int file, struct stat *st)
{
#if (LOG_TO_FILE == 0)
    if(file < STDIO_FILE_IDS)
    {
        st->st_mode = S_IFCHR;
        return 0;
    }
#endif
  extern int errno;

      if(file < MAX_OPEN_FILE_COUNT)
      {
          st->st_mode = S_IFCHR;
          FX_FILE * f = &file_array[file];
          st->st_size = f->fx_file_current_file_size;
#if 0

          st->st_dev = f->fx_file_media_ptr->fx_media_id;
          st->st_blocks = f->fx_file_total_clusters;
          st->st_blksize = f->fx_file_media_ptr->fx_media_bytes_per_sector;
          st->st_atim.tv_sec = f->fx_file_dir_entry.fx_dir_entry_last_accessed_date;
          st->st_mtim.tv_nsec = f->fx_file_dir_entry.fx_dir_entry_created_date;
          st->st_ctim.tv_nsec = f->fx_file_dir_entry.fx_dir_entry_created_date;
#endif
      }
      else
          errno = EBADF;

  return 0;
}


/** Query whether output stream is a terminal.
 * For consistency with the other minimal implementations,
 * which only support output to stdout, this minimal implementation is suggested:
 */

int _isatty(int file)
{
#if (LOG_TO_FILE == 0)
    if(file < STDIO_FILE_IDS)
    {
        return 1;
    }
#endif
    if(file < MAX_OPEN_FILE_COUNT)
        return 1;
    else
        return 0;

}


/** Establish a new name for an existing file. */
int _link(char *old, char *new)
{
    __BKPT(0);
  UINT fx_err = fx_file_rename(g_p_media, old, new);

  if(FX_SUCCESS == fx_err)
  {
      return 0;
  }
  else
  {
      extern int errno;
      errno = EMLINK;
  }
  return -1;
}

/** Close a file. */
int _close(int fd)
{
#if 1
    snprintf(dbg_info, sizeof(dbg_info), "_close %s\r\n", file_array[fd].fx_file_name );
    g_comms_uart_stdio.p_api->write(g_comms_uart_stdio.p_ctrl, (uint8_t*)dbg_info, strlen(dbg_info));
#endif
    UINT fx_err;
    if(fd < MAX_OPEN_FILE_COUNT)
    {
        fx_err = fx_file_close(&file_array[fd]);
        if(FX_SUCCESS == fx_err)
        {
            file_open_count--;
        }
        return 0;
    }
    return -1;
}

#endif
