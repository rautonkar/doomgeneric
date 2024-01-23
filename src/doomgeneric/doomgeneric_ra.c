//doomgeneric for Renesas RA MCU with ThreadX RTOS

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <stdio.h>
#include <unistd.h>

#include <stdbool.h>

#include "tx_api.h"

#include "r_dmac.h"
#include "r_display_api.h"
#include "r_transfer_api.h"
#include "dave_driver.h"

#include "common_data.h"

#define USE_TX_SYNC_OBJECTS (0)

#if (BSP_CFG_DCACHE_ENABLED == 0)
#define DCACHE_KEEP_BEFORE_DTC 1
#endif

uint32_t min_ticks = UINT32_MAX;

uint32_t screenBuffer[DOOMGENERIC_RESY][DOOMGENERIC_RESX];

static byte videoBuffer[DOOMGENERIC_RESX*DOOMGENERIC_RESY];

uint32_t dmac_destination_viewer = 0;
uint8_t dmcnt_enable_value = R_DMAC0_DMCNT_DTE_Msk;

uint32_t * dmac_destination_address_list[2][(sizeof(screenBuffer)/sizeof(screenBuffer[0]))-1];

const uint8_t bpp[] = {
         [DISPLAY_IN_FORMAT_32BITS_ARGB8888] = sizeof(uint32_t),
         [DISPLAY_IN_FORMAT_32BITS_RGB888] = sizeof(uint32_t),
         [DISPLAY_IN_FORMAT_16BITS_RGB565] = sizeof(uint16_t),
    };

transfer_info_t g_dtc_chained_transfer_info[2];
transfer_cfg_t g_dtc_chained_cfg;

void DG_SetWindowTitle(const char * title)
{
    return;
}

void DG_SleepMs(uint32_t ms)
{
#if 0
    __BKPT(0);
#endif
    ULONG ticks = (uint32_t)((TX_TIMER_TICKS_PER_SECOND*ms)/1000);

    ticks = (ticks == 0) ? 1 : ticks;

    if (ticks < min_ticks)
    {
        min_ticks = ticks;
    }

    tx_thread_sleep(ms);
}

uint32_t DG_GetTicksMs()
{
#if 0
    __BKPT(0);
#endif
    uint32_t tx_ticks = (uint32_t)((uint64_t)(tx_time_get() * TX_TIMER_TICKS_PER_SECOND)/1000);

    return tx_ticks;
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
#if 0
    __BKPT(0);
#endif
    return 0;
}

static uint32_t * framebuffers[2] =
{
     NULL,
     NULL,
};

void rotateCoordinatesClockwise(int32_t *x, int32_t *y, uint32_t w, uint32_t h) {
    int32_t temp = *x;
    *x = h - 1 - *y;
        *y = temp;
}

void rotateCoordinatesCounterClockwise(int32_t *x, int32_t *y, uint32_t w, uint32_t h) {
    int32_t temp = *x;
    *x = *y;
        *y = w - 1 - temp;
}

uint32_t t_processing_0 = 0;
uint32_t t_processing_1      = 0;
uint32_t t_copy_rotate_0      = 0;
uint32_t t_copy_rotate_1      = 0;
volatile bool dma_copy_complete = false;
volatile bool refresh_done = false;
volatile uint32_t dwt_cycle_copy_rotate_min = UINT32_MAX;
volatile uint32_t dwt_cycle_not_drawing_min = UINT32_MAX;
volatile uint32_t dwt_cycle_copy_rotate_max = 0;
volatile uint32_t dwt_cycle_not_drawing_max = 0;
volatile uint32_t dwt_cycle_copy_rotate = 0;
volatile uint32_t dwt_cycle_not_drawing = 0;
static volatile uint32_t * p_buffer_to_display = &fb_background[0][0];
uint32_t buffer_index = 0;

volatile uint32_t g_vsync_count = 0;
const uint32_t GLCDC_VSYNC_TIMEOUT = UINT32_MAX;


/* Simple wait that returns 1 if no Vsync happened within the timeout period */
void vsync_wait (void)
{

    extern TX_EVENT_FLAGS_GROUP g_screen_buffer_event_flags;
    ULONG actual_flags;
    UINT tx_err = tx_event_flags_get(&g_screen_buffer_event_flags,
                                     0x01,
                                     TX_OR_CLEAR,
                                     &actual_flags,
                                     TX_WAIT_FOREVER);
    if(TX_SUCCESS != tx_err)
    {
        __BKPT(0);
    }

}

void g_lcd_callback (display_callback_args_t * p_args)
{
    if (p_args->event == DISPLAY_EVENT_LINE_DETECTION)
    {
        extern TX_EVENT_FLAGS_GROUP g_screen_buffer_event_flags;
        UINT tx_err = tx_event_flags_set(&g_screen_buffer_event_flags,
                                         0x01,
                                         TX_OR);

        if(TX_SUCCESS != tx_err)
        {
            __BKPT(0);
        }
    }
    return;
}

/* User-defined function to draw the current display to a framebuffer */
void display_draw (uint8_t * framebuffer)
{
    extern d2_device *d2_handle;
    extern const display_cfg_t g_display_cfg;
    uint32_t h = g_display_cfg.input[0].hsize;
    uint32_t w = g_display_cfg.input[0].vsize;

    d2_s32 d2_err;
#if 0
    d2_err = d2_framebuffer(d2_handle, framebuffer, h, h, w, d2_mode_argb8888);
        if(D2_OK != d2_err){__BKPT(0);};
    d2_err = d2_utility_fbblitcopy(d2_handle, DOOMGENERIC_RESY, DOOMGENERIC_RESX, 0, 0, 0, 0, d2_tm_filter);
       if(D2_OK != d2_err){__BKPT(0);};
#else
   d2_err = d2_framebuffer(d2_handle, framebuffer, h, h, w, d2_mode_argb8888);
   if(D2_OK != d2_err){__BKPT(0);};

    d2_err = d2_startframe(d2_handle);
    if(D2_OK != d2_err){__BKPT(0);};




    d2_err = d2_blitcopy(d2_handle,
                         DOOMGENERIC_RESY, DOOMGENERIC_RESX,  // Source width/height
            (d2_blitpos) 0, 0, // Source position
            (d2_width) ((480) << 4), (d2_width) ((854) << 4), // Destination size width/height
            (d2_width) (((480 - 480)/2) << 4), (d2_width) (((854 - 854)/2) << 4), // Destination offset position
            d2_tm_filter);
    if(D2_OK != d2_err){__BKPT(0);};

    d2_err = d2_endframe(d2_handle);
    if(D2_OK != d2_err){__BKPT(0);};


#endif
    return;
}

/* Frame is ready in DG_ScreenBuffer. Copy it to your platform's screen. */
void DG_DrawFrame(void)
{
    extern uint32_t* DG_ScreenBuffer;
    extern const display_cfg_t g_display_cfg;
    extern const display_instance_t g_display;
    static uint32_t frame_count = 0;

//    extern uint8_t fb_background[2][DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0];
    t_processing_1      = DWT->CYCCNT;

    if(t_processing_1 > t_processing_0)
        {
        dwt_cycle_not_drawing = (t_processing_1 - t_processing_0);
        }
        else
        {
            dwt_cycle_not_drawing = ((~t_processing_0+1) + t_processing_1);
        }
//    dwt_cycle_not_drawing = t_processing_1 > t_processing_0? (t_processing_1 - t_processing_0) : (t_processing_0 + t_processing_1);
    dwt_cycle_not_drawing_min = (dwt_cycle_not_drawing < dwt_cycle_not_drawing_min) ? dwt_cycle_not_drawing : dwt_cycle_not_drawing_min;
    dwt_cycle_not_drawing_max = (dwt_cycle_not_drawing > dwt_cycle_not_drawing_max) ? dwt_cycle_not_drawing : dwt_cycle_not_drawing_max;

    t_copy_rotate_0 = DWT->CYCCNT;


    uint32_t * p_buffer_input = DG_ScreenBuffer;
    uint32_t h = g_display_cfg.input[0].hsize;
    uint32_t w = g_display_cfg.input[0].vsize;

    uint32_t * p_buffer_to_render;
    p_buffer_to_display = (p_buffer_to_display == framebuffers[0]) ? framebuffers[1] : framebuffers[0];
    p_buffer_to_render = (p_buffer_to_display == framebuffers[0]) ? framebuffers[1] : framebuffers[0];
    buffer_index = (p_buffer_to_display == framebuffers[0]) ? 0 : 1;

#if 0   /* ROTATE WITH DTC */

#if (BSP_CFG_DCACHE_ENABLED == 1)
    /* Synchronize the Data Cache to the SRAM */
    __DSB();
#endif

    {
#if !defined(DCACHE_KEEP_BEFORE_DTC)
        SCB_DisableDCache();
        __DSB();
#endif


        extern const transfer_cfg_t g_transfer_fb_background0_cfg;
        extern const transfer_instance_t g_transfer_fb_background0;

        fsp_err_t err = FSP_SUCCESS;

        {
            extern const transfer_instance_t g_transfer_dtc0;
            dmac_instance_ctrl_t * p_ctrl = (dmac_instance_ctrl_t *)g_transfer_fb_background0.p_ctrl;
            const uint16_t len = sizeof(dmac_destination_address_list[0])/sizeof(dmac_destination_address_list[0][0]);

            g_dtc_chained_transfer_info[0].p_src = &dmac_destination_address_list[buffer_index][0];
            g_dtc_chained_transfer_info[0].p_dest =  (void* volatile)&p_ctrl->p_reg->DMDAR;
            g_dtc_chained_transfer_info[0].length = len;

            g_dtc_chained_transfer_info[0].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_EACH;

            g_dtc_chained_transfer_info[1].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_SOURCE;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.irq = g_dtc_chained_transfer_info[0].transfer_settings_word_b.irq;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;

            g_dtc_chained_transfer_info[1].p_src = &dmcnt_enable_value;
            g_dtc_chained_transfer_info[1].p_dest = (void* volatile)&p_ctrl->p_reg->DMCNT;
            g_dtc_chained_transfer_info[1].num_blocks = 0;
            g_dtc_chained_transfer_info[1].length = len;

            err = g_transfer_dtc0.p_api->reconfigure(g_transfer_dtc0.p_ctrl, &g_dtc_chained_transfer_info[0]);
            if(FSP_SUCCESS != err)
            {
                __BKPT(0);
            }
        }

        g_transfer_fb_background0_cfg.p_info->p_src = p_buffer_input;
        g_transfer_fb_background0_cfg.p_info->p_dest = p_buffer_to_display;
#if 0
        /* Pointless since the extended cfg is stored in flash :( */
        dmac_extended_cfg_t * p_extended_cfg =  (dmac_extended_cfg_t *)g_transfer_fb_background0_cfg.p_extend;
        p_extended_cfg->offset = g_display_cfg.input[0].hsize*bpp[g_display_cfg.input[0].format];
#endif

#if (USE_TX_SYNC_OBJECTS)

#else
        dma_copy_complete = false;
#endif

        err = g_transfer_fb_background0.p_api->reconfigure(g_transfer_fb_background0.p_ctrl,g_transfer_fb_background0_cfg.p_info);

        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

        err = g_transfer_fb_background0.p_api->softwareStart(g_transfer_fb_background0.p_ctrl, TRANSFER_START_MODE_REPEAT);

        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

#if (USE_TX_SYNC_OBJECTS)
        {
            extern TX_EVENT_FLAGS_GROUP g_screen_buffer_event_flags;
            ULONG actual_flags;
            UINT tx_err = tx_event_flags_get(&g_screen_buffer_event_flags,
                                             0x01,
                                             TX_OR_CLEAR,
                                             &actual_flags,
                                             TX_WAIT_FOREVER);
            if(TX_SUCCESS != tx_err)
            {
                __BKPT(0);
            }
        }
#else
        while(dma_copy_complete == false);
#endif



#if !defined(DCACHE_KEEP_BEFORE_DTC)
        SCB_EnableDCache();
        __DSB();
#endif
    }
#endif /* ROTATE WITH DTC */

#if 0
    for(uint32_t itr_y = 0; itr_y < h; itr_y++)
    {
        for(uint32_t itr_x = 0; itr_x < w; itr_x++)
        {
            if (itr_x >= DOOMGENERIC_RESX || itr_y >= DOOMGENERIC_RESY)
            {
                continue;
            }

            uint32_t * addr_input = &p_buffer_input[itr_x + itr_y * DOOMGENERIC_RESX];
            int32_t rotated_x = itr_x;
            int32_t rotated_y = itr_y;
//            rotateCoordinatesClockwise(&rotated_x, &rotated_y, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
            uint32_t * addr_output = &p_buffer_to_display[rotated_x + rotated_y * h];
            *addr_output = *addr_input;
        }

    }

#if (BSP_CFG_DCACHE_ENABLED == 1)
    /* Synchronize the Data cache to SDRAM */
    __DSB();
#endif /* (BSP_CFG_DCACHE_ENABLED == 1) */

#endif



#if 1
    /* Use DAVE2D to stretch the rotated image from */
    {
        display_draw(p_buffer_to_display);

//        /* Now that the framebuffer is ready, update the GLCDC buffer pointer on the next Vsync */
        fsp_err_t err = R_GLCDC_BufferChange (g_display.p_ctrl, (uint8_t*) p_buffer_to_render, DISPLAY_FRAME_LAYER_1);
        FSP_PARAMETER_NOT_USED(err);

        vsync_wait();
    }
#endif

    t_copy_rotate_1 = DWT->CYCCNT;
     /* Calculate DW cycles */
 //    dwt_cycle_copy_rotate = t_copy_rotate_1 > t_copy_rotate_0? t_copy_rotate_1 - t_copy_rotate_0 : ((~t_copy_rotate_0 + 1)+ t_copy_rotate_1);
     if(t_copy_rotate_1 > t_copy_rotate_0)
     {
         dwt_cycle_copy_rotate = (t_copy_rotate_1 - t_copy_rotate_0);
     }
     else
     {
         dwt_cycle_copy_rotate = ((~t_copy_rotate_0 + 1)+ t_copy_rotate_1);
     }
     dwt_cycle_copy_rotate_min = (dwt_cycle_copy_rotate < dwt_cycle_copy_rotate_min) ? dwt_cycle_copy_rotate : dwt_cycle_copy_rotate_min;
     dwt_cycle_copy_rotate_max = (dwt_cycle_copy_rotate > dwt_cycle_copy_rotate_max) ? dwt_cycle_copy_rotate : dwt_cycle_copy_rotate_max;
    t_processing_0 = DWT->CYCCNT;
}

void DG_Init(void)
{
    extern uint32_t* DG_ScreenBuffer;
    extern byte * I_VideoBuffer;
    extern const display_cfg_t g_display_cfg;

    framebuffers[0] = (uint32_t*) g_display_cfg.input[0].p_base;
    framebuffers[1] = (uint32_t*)((uint8_t*)framebuffers[0] + (bpp[g_display_cfg.input[0].format] * g_display_cfg.input[0].hsize * g_display_cfg.input[0].vsize));

    I_VideoBuffer = videoBuffer;
    DG_ScreenBuffer = &screenBuffer[0][0];


    {
        extern const transfer_cfg_t g_transfer_fb_background0_cfg;
        extern const transfer_instance_t g_transfer_fb_background0;


        fsp_err_t err = FSP_SUCCESS;
        g_transfer_fb_background0_cfg.p_info->p_src = &screenBuffer[0][0];
        g_transfer_fb_background0_cfg.p_info->p_dest = framebuffers[0];

        err = g_transfer_fb_background0.p_api->open(g_transfer_fb_background0.p_ctrl,
                                                    g_transfer_fb_background0.p_cfg);

        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

        {
            extern const transfer_instance_t g_transfer_dtc0;
            const uint16_t len = sizeof(dmac_destination_address_list[0])/sizeof(dmac_destination_address_list[0][0]);

            for (uint32_t buffer_id = 0; buffer_id < sizeof(framebuffers)/sizeof(framebuffers[0]); buffer_id++)
            {
                for(uint32_t itr = 0; itr < len; itr++)
                {
                    dmac_destination_address_list[buffer_id][itr] = framebuffers[buffer_id] + itr + 1;
                }
            }



            fsp_err_t err = FSP_SUCCESS;

            g_transfer_dtc0.p_cfg->p_info->p_src = &dmac_destination_address_list[0][0];
            g_transfer_dtc0.p_cfg->p_info->p_dest = &dmac_destination_viewer;
            g_transfer_dtc0.p_cfg->p_info->length = len;

            g_dtc_chained_cfg.p_extend = g_transfer_dtc0.p_cfg->p_extend;
            g_dtc_chained_cfg.p_info = &g_dtc_chained_transfer_info[0];


            memcpy(&g_dtc_chained_transfer_info[0], g_transfer_dtc0.p_cfg->p_info, sizeof(transfer_info_t));
            memcpy(&g_dtc_chained_transfer_info[1], g_transfer_dtc0.p_cfg->p_info, sizeof(transfer_info_t));

            g_dtc_chained_transfer_info[1].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_EACH;

            dmac_instance_ctrl_t * p_ctrl = (dmac_instance_ctrl_t *)g_transfer_fb_background0.p_ctrl;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_SOURCE;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.irq = TRANSFER_IRQ_END;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.size = TRANSFER_SIZE_1_BYTE;
            g_dtc_chained_transfer_info[1].transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;

            g_dtc_chained_transfer_info[1].p_src = &dmcnt_enable_value;
            g_dtc_chained_transfer_info[1].p_dest = &p_ctrl->p_reg->DMCNT;
            g_dtc_chained_transfer_info[1].num_blocks = 0;
            g_dtc_chained_transfer_info[1].length = len;

            err = g_transfer_dtc0.p_api->open(g_transfer_dtc0.p_ctrl, &g_dtc_chained_cfg);

            if(FSP_SUCCESS != err)
            {
                __BKPT(0);
            }

            err = g_transfer_dtc0.p_api->enable(g_transfer_dtc0.p_ctrl);

            if(FSP_SUCCESS != err)
            {
                __BKPT(0);
            }
        }

        err = g_transfer_fb_background0.p_api->enable(g_transfer_fb_background0.p_ctrl);

        if(FSP_SUCCESS != err)
        {
            __BKPT(0);
        }

    }

    {
        extern d2_device *d2_handle;
        d2_s32 d2_err;
        d2_err = d2_setblitsrc(d2_handle, screenBuffer, DOOMGENERIC_RESY, DOOMGENERIC_RESY, DOOMGENERIC_RESX, d2_mode_argb8888);
        if(D2_OK != d2_err){__BKPT(0);};
    }

}

void transfer_fb_background0_callback(dmac_callback_args_t *p_args)
{
    static uint32_t itr = 0;

    if(g_dtc_chained_transfer_info[0].length == 0)
    {
        itr = 0;

#if (USE_TX_SYNC_OBJECTS)
        extern TX_EVENT_FLAGS_GROUP g_screen_buffer_event_flags;
        UINT tx_err = tx_event_flags_set(&g_screen_buffer_event_flags,
                                         0x01,
                                         TX_OR);

        if(TX_SUCCESS != tx_err)
        {
            __BKPT(0);
        }
#else
        dma_copy_complete = true;
#endif

    }
    else
    {
        itr++;
    }


#if (BSP_CFG_DCACHE_ENABLED == 1) && defined(DCACHE_KEEP_BEFORE_DTC)
    /* Synchronize the Data cache to SDRAM */
    __DSB();
#endif /* (BSP_CFG_DCACHE_ENABLED == 1) */
    return;
}
