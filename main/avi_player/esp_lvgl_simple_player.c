/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_dma_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/jpeg_decode.h"
#include "media_src_storage.h"

#include "esp_lvgl_simple_player.h"

#define CACHE_BUF_ALIGN         (1024)

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(num, align)    (((num) - ((align) + 1)) & ~((align) - 1))

static const char *TAG = "esp_lvgl_player";
static const uint16_t EOI = 0xd9ff; /* End of image */
static BaseType_t player_task_handle = NULL;

typedef struct
{
    bool is_init;
    const char              *video_path;
    const char              *bgm_path;
    media_src_t             file;
    uint64_t                filesize;
    jpeg_decoder_handle_t   jpeg;

    uint32_t    screen_width;   /* Width of the video player object */
    uint32_t    screen_height;  /* Height of the video player object */
    uint32_t    video_width;      /* Maximum width of the video */
    uint32_t    video_height;     /* Maximum height of the video  */

    player_state_t  state;
    bool            loop;
    bool            hide_controls;
    bool            hide_slider;
    bool            hide_status;
    bool            auto_width;
    bool            auto_height;

    /* Buffers */
    uint8_t     *in_buff;
    uint32_t    in_buff_size;
    uint8_t     *out_buff;
    uint32_t    out_buff_size;
    uint8_t     *cache_buff;
    uint32_t    cache_buff_size;
    bool        cache_buff_in_psram;

    /* LVGL objects */
    lv_obj_t    *main;
    lv_obj_t    *canvas;
    lv_obj_t    *slider;
    lv_obj_t    *btn_play;
    lv_obj_t    *btn_pause;
    lv_obj_t    *btn_stop;
    lv_obj_t    *btn_repeat;
    lv_obj_t    *img_pause;
    lv_obj_t    *img_stop;
    lv_obj_t    *controls;
} player_ctx_t;

static player_ctx_t player_ctx;


static const jpeg_decode_cfg_t jpeg_decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
};



static void play_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        esp_lvgl_simple_player_play();
    }
}

static void stop_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        esp_lvgl_simple_player_stop();

        bsp_display_unlock();
        if (esp_lvgl_simple_player_wait_task_stop(100) != ESP_OK) {
            ESP_LOGE(TAG, "Player task stop timeout");
        }
        bsp_display_lock(100);
    }
}

static void pause_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        if(player_ctx.state == PLAYER_STATE_PAUSED) {
            esp_lvgl_simple_player_play();
        } else if (player_ctx.state == PLAYER_STATE_PLAYING) {
            esp_lvgl_simple_player_pause();
        }
    }
}

static void repeat_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        bool loop = lv_obj_get_state(obj) & LV_STATE_CHECKED ? true : false;
        esp_lvgl_simple_player_repeat(loop);
    }
}


static esp_err_t get_video_size(uint32_t * width, uint32_t * height)
{
    esp_err_t err;
    jpeg_decode_picture_info_t header;
    assert(width && height);

    int size = media_src_storage_read(&player_ctx.file, player_ctx.cache_buff, player_ctx.cache_buff_size);
    if(size < 0)
        return ESP_ERR_INVALID_SIZE;

    err = jpeg_decoder_get_info(player_ctx.cache_buff, size, &header);

    ESP_LOGI(TAG, "header parsed, width is %" PRId32 ", height is %" PRId32 ", size is %d", header.width, header.height, size);

    *width = header.width;
    *height = header.height;

    return err;
}

static esp_err_t video_decoder_init(void)
{
    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = -1,
    };
    return jpeg_new_decoder_engine(&engine_cfg, &player_ctx.jpeg);
}

static void video_decoder_deinit(void)
{
    if (player_ctx.jpeg) {
        jpeg_del_decoder_engine(player_ctx.jpeg);
    }
}

static uint8_t * video_decoder_malloc(uint32_t size, bool inbuff, uint32_t * outsize)
{
    jpeg_decode_memory_alloc_cfg_t tx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    return (uint8_t *)jpeg_alloc_decoder_mem(size, (inbuff ? &tx_mem_cfg : &rx_mem_cfg), (size_t*)outsize);
}

static int video_decoder_read_jpeg_image(uint32_t *file_seek_start, uint32_t *file_seek_offset)
{
    uint32_t read_size = 0;
    uint32_t jpeg_image_size = 0;
    uint8_t * match = NULL;
    uint8_t *cache_buff = player_ctx.cache_buff;
    uint8_t *cache_buff_offset = NULL;
    uint32_t cache_buff_size = player_ctx.cache_buff_size;
    uint32_t seek_pos_offset = *file_seek_offset;
    uint32_t seek_pos_cur = *file_seek_start;
    uint32_t seek_pos_next = 0;

    // if (seek_pos_cur % CACHE_BUF_ALIGN != 0) {
    //     ESP_LOGE(TAG, "File seek start is not aligned to %d", CACHE_BUF_ALIGN);
    //     return 0;
    // }

    while (match == NULL) {
        read_size = media_src_storage_read(&player_ctx.file, cache_buff, cache_buff_size) - seek_pos_offset;
        if (read_size <= 0) {
            break;
        }

        cache_buff_offset = cache_buff + seek_pos_offset;

        /* Search for EOI. */
        match = memmem(cache_buff_offset, read_size, &EOI, 2);
        if(match) {
            read_size = (uint32_t)((match + 2) - cache_buff_offset);
        }

        memcpy(player_ctx.in_buff + jpeg_image_size, cache_buff_offset, read_size);
        jpeg_image_size += read_size;

        seek_pos_next = ALIGN_DOWN(seek_pos_cur + seek_pos_offset + read_size, CACHE_BUF_ALIGN);
        seek_pos_cur = seek_pos_cur + seek_pos_offset + read_size;
        seek_pos_offset = seek_pos_cur - seek_pos_next;
        media_src_storage_seek(&player_ctx.file, seek_pos_next);
        seek_pos_cur = seek_pos_next;

    }
    if (jpeg_image_size > player_ctx.in_buff_size) {
        ESP_LOGE(TAG, "JPEG image size is bigger than input buffer size");
        jpeg_image_size = -1;
    }
    *file_seek_start = seek_pos_next;
    *file_seek_offset = seek_pos_offset;

    // if (seek_pos_next % CACHE_BUF_ALIGN != 0) {
    //     ESP_LOGE(TAG, "File seek next is not aligned to %d", CACHE_BUF_ALIGN);
    //     return 0;
    // }

    return jpeg_image_size;
}

static int video_decoder_decode(uint32_t jpeg_image_size)
{
    esp_err_t err;
    uint32_t ret_size = 0;
    uint32_t jpeg_image_size_aligned = ALIGN_UP(jpeg_image_size, 16);

    if (jpeg_image_size_aligned > player_ctx.in_buff_size) {
        ESP_LOGE(TAG, "JPEG image size is bigger than input buffer size");
        return -1;
    }

    /* Decode JPEG */
    ret_size = player_ctx.out_buff_size;
    err = jpeg_decoder_process(player_ctx.jpeg, &jpeg_decode_cfg, player_ctx.in_buff, jpeg_image_size_aligned,
                               player_ctx.out_buff, player_ctx.out_buff_size, &ret_size);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG decode failed");
        return -1;
    }

    if (ret_size > player_ctx.out_buff_size) {
        ESP_LOGE(TAG, "Output buffer is too small");
        return -1;
    }

    return jpeg_image_size;
}

static void show_video_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    int processed = 0;
    int all_size = 0;
    int file_seek_pos = 0;
    int file_seek_offset = 0;
    int jpeg_image_size = 0;

    /* Open video file */
    ESP_LOGI(TAG, "Opening video file %s ...", player_ctx.video_path);
    ESP_GOTO_ON_FALSE(media_src_storage_open(&player_ctx.file) == 0, ESP_ERR_NO_MEM, err, TAG, "Storage open failed");
    ESP_GOTO_ON_FALSE(media_src_storage_connect(&player_ctx.file, player_ctx.video_path) == 0, ESP_ERR_NO_MEM, err, TAG, "Storage connect failed");

    if (player_ctx.bgm_path != NULL) {
        ESP_LOGI(TAG, "Opening bgm file %s ...", player_ctx.bgm_path);
    }

    /* Get file size */
    ESP_GOTO_ON_FALSE(media_src_storage_get_size(&player_ctx.file, &player_ctx.filesize) == 0, ESP_ERR_NO_MEM, err, TAG, "Get file size failed");

    /* Create input buffer */
    player_ctx.in_buff = video_decoder_malloc(player_ctx.in_buff_size, true, &player_ctx.in_buff_size);
    ESP_GOTO_ON_FALSE(player_ctx.in_buff, ESP_ERR_NO_MEM, err, TAG, "Allocation in_buff failed");

    /* Init video decoder */
    ESP_GOTO_ON_ERROR(video_decoder_init(), err, TAG, "Initialize video decoder failed");

    /* Get video output size */
    uint32_t height = 0;
    uint32_t width = 0;
    ESP_GOTO_ON_ERROR(get_video_size(&width, &height), err, TAG, "Get video file size failed");
    width = ALIGN_UP(width, 16);

    /* Create output buffer */
    player_ctx.out_buff_size = width * height * 3;
    player_ctx.out_buff = video_decoder_malloc(player_ctx.out_buff_size, false, &player_ctx.out_buff_size);
    ESP_GOTO_ON_FALSE(player_ctx.out_buff, ESP_ERR_NO_MEM, err, TAG, "Allocation out_buff failed");

	/* Set buffer to LVGL canvas */
    lv_canvas_set_buffer(player_ctx.canvas, player_ctx.out_buff, width, height, LV_IMG_CF_TRUE_COLOR);

    if (player_ctx.auto_width || player_ctx.auto_height) {
        uint32_t h = (player_ctx.auto_height ? (height+120) : lv_obj_get_height(player_ctx.main));
        uint32_t w = (player_ctx.auto_width ? width : lv_obj_get_width(player_ctx.main));
        lv_obj_set_size(player_ctx.main, w, h);
    }


    player_ctx.state = PLAYER_STATE_PLAYING;

    ESP_LOGI(TAG, "Video player initialized");

    if ((player_ctx.bgm_path != NULL) && bsp_extra_player_play_file(player_ctx.bgm_path) != ESP_OK) {
        ESP_LOGE(TAG, "Play bgm failed");
    }
    media_src_storage_seek(&player_ctx.file, 0);

    while (player_ctx.state != PLAYER_STATE_STOPPED) {
        if (player_ctx.state == PLAYER_STATE_PAUSED) {
            if (bsp_display_lock(10)) {
                lv_obj_clear_flag(player_ctx.img_pause, LV_OBJ_FLAG_HIDDEN);
                bsp_display_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        jpeg_image_size = video_decoder_read_jpeg_image(&file_seek_pos, &file_seek_offset);
        if (jpeg_image_size < 0) {
            ESP_LOGE(TAG, "Read JPEG image failed. Skip frame.");
            break;
        } else if (jpeg_image_size == 0) {
            ESP_LOGI(TAG, "Playing finished.");
            if (player_ctx.loop) {
                ESP_LOGI(TAG, "Playing loop enabled. Play again...");
                media_src_storage_seek(&player_ctx.file, 0);
                file_seek_pos = 0;
                file_seek_offset = 0;
                all_size = 0;
                continue;
            } else {
                esp_lvgl_simple_player_stop();
                continue;
            }
        }

        /* Decode one frame */
        processed = video_decoder_decode(jpeg_image_size);
        if (processed < 0) {
            ESP_LOGE(TAG, "Decode JPEG image failed. Skip frame.");
            break;
        } else {
            all_size += processed;
        }

    }

err:

    /* Show black on screen */
    memset(player_ctx.out_buff, 0, player_ctx.out_buff_size);
    if (player_ctx.auto_height) {
        lv_obj_set_height(player_ctx.main, 320);
    }

    if (player_ctx.bgm_path != NULL) {
        bsp_extra_player_register_callback(NULL, NULL);
        if (audio_player_stop() != ESP_OK) {
            ESP_LOGE(TAG, "Stop bgm failed");
        }
    }

    /* Close storage */
    media_src_storage_disconnect(&player_ctx.file);
    media_src_storage_close(&player_ctx.file);

    /* Deinit video decoder */
    video_decoder_deinit();

    if (player_ctx.in_buff) {
        heap_caps_free(player_ctx.in_buff);
        player_ctx.in_buff = NULL;
    }
    if (player_ctx.out_buff) {
        heap_caps_free(player_ctx.out_buff);
        player_ctx.out_buff = NULL;
        player_ctx.out_buff_size = 0;
    }

    ESP_LOGI(TAG, "Video player task finished.");

    /* Close task */
    vTaskDelete(NULL);
}

lv_obj_t * esp_lvgl_simple_player_create(esp_lvgl_simple_player_cfg_t * params)
{
    ESP_RETURN_ON_FALSE(params->video_path, NULL, TAG, "File path must be filled");
    ESP_RETURN_ON_FALSE(params->screen, NULL, TAG, "LVGL screen must be filled");
    ESP_RETURN_ON_FALSE(params->buff_size, NULL, TAG, "Size of the video frame buffer must be filled");
    ESP_RETURN_ON_FALSE(params->screen_width > 0 && params->screen_height > 0, NULL, TAG, "Object size must be filled");

    player_ctx.video_path = params->video_path;
    player_ctx.bgm_path = params->bgm_path;
    player_ctx.in_buff_size = params->buff_size;

    player_ctx.cache_buff_size = ALIGN_UP(params->cache_buff_size, CACHE_BUF_ALIGN);
    player_ctx.cache_buff_in_psram = params->cache_buff_in_psram;
    /* Create split buffer */
    uint32_t flag = player_ctx.cache_buff_in_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    player_ctx.cache_buff = (uint8_t *)heap_caps_aligned_alloc(128, player_ctx.cache_buff_size, flag);
    if (!player_ctx.cache_buff) {
        ESP_LOGE(TAG, "Malloc cache buffer failed");
        return NULL;
    }

    player_ctx.screen_width = params->screen_width;
    player_ctx.screen_height = params->screen_height;
    player_ctx.hide_controls = params->flags.hide_controls;
    player_ctx.hide_slider = params->flags.hide_slider;
    player_ctx.hide_status = params->flags.hide_status;
    player_ctx.auto_width = params->flags.auto_width;
    player_ctx.auto_height = params->flags.auto_height;
    player_ctx.is_init = true;

    /* Create LVGL objects */
    lv_obj_t * player_screen = create_lvgl_objects(params->screen);

    /* Default player state */
    esp_lvgl_simple_player_stop();

    return player_screen;
}

player_state_t esp_lvgl_simple_player_get_state(void)
{
    return player_ctx.state;
}

void esp_lvgl_simple_player_hide_controls(bool hide)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

}

void esp_lvgl_simple_player_change_file(const char *video_file)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

    if (player_ctx.state != PLAYER_STATE_STOPPED) {
        ESP_LOGW(TAG, "Playing file can be changed only when video is stopped.");
    }
    player_ctx.video_path = video_file;

    ESP_LOGI(TAG, "Video file changed to %s", video_file);
}

void esp_lvgl_simple_player_play(void)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

    if (player_ctx.state == PLAYER_STATE_STOPPED) {
        ESP_LOGI(TAG, "Player starting playing.");
        /* Create video task */
        xTaskCreate(show_video_task, "video task", 8 * 1024, NULL, 4, &player_task_handle);
    } else if(player_ctx.state == PLAYER_STATE_PAUSED) {
        esp_lvgl_simple_player_resume();
    }
}

void esp_lvgl_simple_player_pause(void)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

    if (player_ctx.state == PLAYER_STATE_PLAYING) {
        ESP_LOGI(TAG, "Player paused.");
        player_ctx.state = PLAYER_STATE_PAUSED;
        if (player_ctx.bgm_path != NULL) {
            if (audio_player_pause() != ESP_OK) {
                ESP_LOGE(TAG, "Pause bgm failed");
            }
        }


    }
}

void esp_lvgl_simple_player_resume(void)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

    if (player_ctx.state == PLAYER_STATE_PAUSED) {
        ESP_LOGI(TAG, "Player resume playing.");
        player_ctx.state = PLAYER_STATE_PLAYING;
        if (player_ctx.bgm_path != NULL) {
            if (!bsp_extra_player_is_playing_by_path(player_ctx.bgm_path)) {
                ESP_LOGW(TAG, "BGM is not playing. Start playing.");
                if (bsp_extra_player_play_file(player_ctx.bgm_path) != ESP_OK) {
                    ESP_LOGE(TAG, "Play bgm failed");
                }
            } else if (audio_player_resume() != ESP_OK) {
                ESP_LOGE(TAG, "Pause bgm failed");
            }
        }

 
    }
}

void esp_lvgl_simple_player_stop(void)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

    ESP_LOGI(TAG, "Player stopped.");
    player_ctx.state = PLAYER_STATE_STOPPED;

}

void esp_lvgl_simple_player_repeat(bool repeat)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return;
    }

    ESP_LOGI(TAG, "Player repeat %s.", (repeat ? "enabled" : "disabled"));
    player_ctx.loop = repeat;
}

esp_err_t esp_lvgl_simple_player_del(void)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return ESP_OK;
    }

    if (player_task_handle != 0) {
        esp_lvgl_simple_player_stop();
        if (esp_lvgl_simple_player_wait_task_stop(-1) != ESP_OK) {
            ESP_LOGE(TAG, "Player task stop timeout");
        }
    }

    if (player_ctx.cache_buff) {
        heap_caps_free(player_ctx.cache_buff);
        player_ctx.cache_buff = NULL;
    }

    player_ctx.is_init = false;

    return ESP_OK;
}

esp_err_t esp_lvgl_simple_player_wait_task_stop(int timeout_ms)
{
    if (!player_ctx.is_init) {
        ESP_LOGW(TAG, "Not init");
        return ESP_OK;
    }

    if (player_task_handle == 0) {
        return ESP_OK;
    }

    uint32_t i = 0;
    while (eTaskGetState(player_task_handle) != eDeleted && (timeout_ms < 0 || i < timeout_ms)) {
        vTaskDelay(pdMS_TO_TICKS(1));
        i++;
    }
    player_task_handle = NULL;

    if (i >= timeout_ms) {
        ESP_LOGE(TAG, "Player task stop timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
