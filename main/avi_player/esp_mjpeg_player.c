#include "esp_mjpeg_player.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_dma_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#if CONFIG_IDF_TARGET_ESP32P4
#include "driver/jpeg_decode.h"
#else
#include "esp_jpeg_decode.h"
#endif

#include "media_src_storage.h"

static const char *TAG = "mjpeg_player";

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(num, align)  (((num) - ((align) + 1)) & ~((align) - 1))

typedef struct {
    bool is_playing;
    bool is_loop;
    TaskHandle_t task_handle;
    media_src_t file;
    uint64_t file_size;

    // 缓冲区
    uint8_t *in_buff;         // JPEG输入缓冲区
    uint32_t in_buff_size;    // 改为uint32_t以匹配video_decoder_malloc
    uint8_t *out_buff;        // RGB565输出缓冲区  
    uint32_t out_buff_size;   // 改为uint32_t以匹配video_decoder_malloc
    uint8_t *cache_buff;      // 文件读取缓存
    size_t cache_buff_size;

#if CONFIG_IDF_TARGET_ESP32P4
    // P4硬件JPEG解码器
    jpeg_decoder_handle_t jpeg_handle;
#endif

    // 回调函数
    void (*on_frame_cb)(uint8_t *rgb565, uint32_t width, uint32_t height, void* ctx);
    void* user_data;
} mjpeg_player_t;

#if CONFIG_IDF_TARGET_ESP32P4
// P4硬件JPEG解码配置 - 完全参考esp_lvgl_simple_player
static const jpeg_decode_cfg_t jpeg_decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
};

// P4硬件解码器初始化 - 完全参考esp_lvgl_simple_player
static esp_err_t video_decoder_init(mjpeg_player_t *player)
{
    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = -1,
    };
    return jpeg_new_decoder_engine(&engine_cfg, &player->jpeg_handle);
}

static void video_decoder_deinit(mjpeg_player_t *player)
{
    if (player->jpeg_handle) {
        jpeg_del_decoder_engine(player->jpeg_handle);
        player->jpeg_handle = NULL;
    }
}

// P4专用内存分配 - 完全参考esp_lvgl_simple_player的video_decoder_malloc
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
#endif

static uint8_t* find_jpeg_marker(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len - 1; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8) {
            return &buf[i];
        }
    }
    return NULL;
}

static uint8_t* find_jpeg_end(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len - 1; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD9) {
            return &buf[i + 2];
        }
    }
    return NULL;
}

static void mjpeg_player_task(void *arg) {
    mjpeg_player_t *player = (mjpeg_player_t *)arg;
    uint32_t read_pos = 0;
    size_t bytes_read = 0;
    int64_t last_frame_time = 0;

    ESP_LOGI(TAG, "MJPEG player task started");

    while (player->is_playing) {
        // 读取数据到缓存
        bytes_read = media_src_storage_read(&player->file, 
            player->cache_buff, player->cache_buff_size);
        
        if (bytes_read <= 0) {
            if (player->is_loop) {
                ESP_LOGI(TAG, "End of file, restarting loop...");
                media_src_storage_seek(&player->file, 0);
                read_pos = 0;
                continue;
            }
            ESP_LOGI(TAG, "End of file, stopping playback");
            break;
        }

        // 查找JPEG帧
        uint8_t *frame_start = find_jpeg_marker(player->cache_buff, bytes_read);
        if (!frame_start) {
            continue;
        }

        uint8_t *frame_end = find_jpeg_end(frame_start, 
            bytes_read - (frame_start - player->cache_buff));
        if (!frame_end) {
            continue;
        }

        size_t frame_size = frame_end - frame_start;
        if (frame_size > player->in_buff_size) {
            ESP_LOGW(TAG, "Frame too large: %d > %ld", frame_size, player->in_buff_size);
            continue;
        }

        // 复制帧数据到输入缓冲区
        memcpy(player->in_buff, frame_start, frame_size);

#if CONFIG_IDF_TARGET_ESP32P4
        // P4硬件JPEG解码 - 完全参考esp_lvgl_simple_player
        uint32_t frame_size_aligned = ALIGN_UP(frame_size, 16);
        uint32_t ret_size = player->out_buff_size;
        
        esp_err_t ret = jpeg_decoder_process(player->jpeg_handle, &jpeg_decode_cfg, 
                                           player->in_buff, frame_size_aligned,
                                           player->out_buff, player->out_buff_size, &ret_size);
        
        if (ret == ESP_OK) {
            // 调用回调函数显示帧
            if (player->on_frame_cb) {
                // 通过解码器获取图像信息
                jpeg_decode_picture_info_t picture_info;
                esp_err_t info_ret = jpeg_decoder_get_info(player->in_buff, frame_size, &picture_info);
                if (info_ret == ESP_OK) {
                    player->on_frame_cb(player->out_buff, picture_info.width, picture_info.height, player->user_data);
                } else {
                    ESP_LOGW(TAG, "Failed to get JPEG info for callback");
                }
            }
        } else {
            ESP_LOGW(TAG, "Hardware JPEG decode failed: %d", ret);
        }
#else
        // S3软件JPEG解码 - 参考原有方式
        uint8_t *rgb565_buf = NULL;
        int rgb_size = 0;
        jpeg_error_t ret = esp_jpeg_decode_one_picture(
            player->in_buff, frame_size,
            &rgb565_buf, &rgb_size);

        if (ret == JPEG_ERR_OK && rgb565_buf && rgb_size > 0) {
            // 调用回调函数显示帧
            if (player->on_frame_cb) {
                player->on_frame_cb(rgb565_buf, get_rgb_width(), get_rgb_height(), player->user_data);
            }
            free(rgb565_buf);
        } else {
            ESP_LOGW(TAG, "Software JPEG decode failed: %d", ret);
        }
#endif

        // 更新读取位置
        size_t advance = frame_end - player->cache_buff;
        read_pos += advance;
        media_src_storage_seek(&player->file, read_pos);

        // 帧率控制 - 限制到8fps
        int64_t current_time = esp_timer_get_time();
        if (last_frame_time > 0) {
            int64_t elapsed = current_time - last_frame_time;
            if (elapsed < 125000) { // 125ms = 8fps
                vTaskDelay(pdMS_TO_TICKS((125000 - elapsed) / 1000));
            }
        }
        last_frame_time = current_time;
    }

    player->is_playing = false;
    ESP_LOGI(TAG, "MJPEG player task finished");
    vTaskDelete(NULL);
}

esp_err_t mjpeg_player_create(const mjpeg_player_config_t* config, mjpeg_player_handle_t* handle) {
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }

    mjpeg_player_t *player = heap_caps_calloc(1, sizeof(mjpeg_player_t), MALLOC_CAP_INTERNAL);
    if (!player) {
        return ESP_ERR_NO_MEM;
    }

    // 设置缓冲区大小
    player->in_buff_size = config->frame_buffer_size ? 
        config->frame_buffer_size : 64 * 1024;  // 默认64KB输入缓冲区
    player->cache_buff_size = config->cache_buffer_size ? 
        config->cache_buffer_size : 64 * 1024;

#if CONFIG_IDF_TARGET_ESP32P4
    // P4使用硬件解码器专用内存分配 - 完全参考esp_lvgl_simple_player
    player->in_buff = video_decoder_malloc(player->in_buff_size, true, &player->in_buff_size);
    if (!player->in_buff) {
        ESP_LOGE(TAG, "Failed to allocate input buffer");
        heap_caps_free(player);
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化硬件解码器
    esp_err_t ret = video_decoder_init(player);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hardware decoder: %d", ret);
        heap_caps_free(player->in_buff);
        heap_caps_free(player);
        return ret;
    }
    
    // 输出缓冲区稍后根据实际图像大小分配
    ESP_LOGI(TAG, "Using P4 hardware JPEG decoder");
#else
    // S3使用普通内存分配
    player->in_buff = malloc(player->in_buff_size);
    if (!player->in_buff) {
        ESP_LOGE(TAG, "Failed to allocate input buffer");
        heap_caps_free(player);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Using S3 software JPEG decoder");
#endif

    // 分配缓存缓冲区
    if (config->cache_in_psram) {
        player->cache_buff = heap_caps_malloc(player->cache_buff_size, MALLOC_CAP_SPIRAM);
    } else {
        player->cache_buff = malloc(player->cache_buff_size);
    }

    if (!player->cache_buff) {
        ESP_LOGE(TAG, "Failed to allocate cache buffer");
#if CONFIG_IDF_TARGET_ESP32P4
        video_decoder_deinit(player);
#endif
        heap_caps_free(player->in_buff);
        heap_caps_free(player);
        return ESP_ERR_NO_MEM;
    }

    // 初始化媒体源
    if (media_src_storage_open(&player->file) != 0) {
        ESP_LOGE(TAG, "Failed to open media source");
#if CONFIG_IDF_TARGET_ESP32P4
        video_decoder_deinit(player);
#endif
        heap_caps_free(player->cache_buff);
        heap_caps_free(player->in_buff);
        heap_caps_free(player);
        return ESP_FAIL;
    }

    player->on_frame_cb = config->on_frame_cb;
    player->user_data = config->user_data;
    *handle = player;

    ESP_LOGI(TAG, "MJPEG player created successfully");
    return ESP_OK;
}

esp_err_t mjpeg_player_play_file(mjpeg_player_handle_t handle, const char* filepath) {
    mjpeg_player_t *player = (mjpeg_player_t *)handle;
    if (!player || !filepath) {
        return ESP_ERR_INVALID_ARG;
    }

    if (player->is_playing) {
        mjpeg_player_stop(handle);
        vTaskDelay(pdMS_TO_TICKS(100)); // 等待停止完成
    }

    // 连接到文件
    if (media_src_storage_connect(&player->file, filepath) != 0) {
        ESP_LOGE(TAG, "Failed to connect to file: %s", filepath);
        return ESP_FAIL;
    }

    if (media_src_storage_get_size(&player->file, &player->file_size) != 0) {
        ESP_LOGE(TAG, "Failed to get file size");
        media_src_storage_disconnect(&player->file);
        return ESP_FAIL;
    }

#if CONFIG_IDF_TARGET_ESP32P4
    // P4需要预先获取图像尺寸并分配输出缓冲区
    int size = media_src_storage_read(&player->file, player->cache_buff, player->cache_buff_size);
    if (size <= 0) {
        ESP_LOGE(TAG, "Failed to read file header");
        media_src_storage_disconnect(&player->file);
        return ESP_FAIL;
    }
    
    // 获取JPEG图像信息
    jpeg_decode_picture_info_t header;
    esp_err_t info_ret = jpeg_decoder_get_info(player->cache_buff, size, &header);
    if (info_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get JPEG info: %d", info_ret);
        media_src_storage_disconnect(&player->file);
        return info_ret;
    }
    
    ESP_LOGI(TAG, "JPEG info: %dx%d", (int)header.width, (int)header.height);
    
    // 分配输出缓冲区 - 完全参考esp_lvgl_simple_player
    uint32_t width = ALIGN_UP(header.width, 16);
    uint32_t height = header.height;
    player->out_buff_size = width * height * 3;  // RGB565需要的空间
    player->out_buff = video_decoder_malloc(player->out_buff_size, false, &player->out_buff_size);
    if (!player->out_buff) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        media_src_storage_disconnect(&player->file);
        return ESP_ERR_NO_MEM;
    }
    
    // 重新定位到文件开头
    media_src_storage_seek(&player->file, 0);
#endif

    player->is_playing = true;
    BaseType_t ret = xTaskCreatePinnedToCore(mjpeg_player_task, "mjpeg_player", 
        8 * 1024, player, 4, &player->task_handle, 0);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create player task");
        player->is_playing = false;
#if CONFIG_IDF_TARGET_ESP32P4
        if (player->out_buff) {
            heap_caps_free(player->out_buff);
            player->out_buff = NULL;
        }
#endif
        media_src_storage_disconnect(&player->file);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started playing file: %s", filepath);
    return ESP_OK;
}

esp_err_t mjpeg_player_stop(mjpeg_player_handle_t handle) {
    mjpeg_player_t *player = (mjpeg_player_t *)handle;
    if (!player) {
        return ESP_ERR_INVALID_ARG;
    }

    if (player->is_playing) {
        player->is_playing = false;
        
        // 等待任务结束
        if (player->task_handle) {
            // 等待任务自动结束
            uint32_t timeout = 1000; // 1秒超时
            while (eTaskGetState(player->task_handle) != eDeleted && timeout-- > 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            if (timeout == 0) {
                ESP_LOGW(TAG, "Task did not finish gracefully, force deleting");
                vTaskDelete(player->task_handle);
            }
            player->task_handle = NULL;
        }
        
        media_src_storage_disconnect(&player->file);
        
#if CONFIG_IDF_TARGET_ESP32P4
        // P4清理输出缓冲区
        if (player->out_buff) {
            heap_caps_free(player->out_buff);
            player->out_buff = NULL;
            player->out_buff_size = 0;
        }
#endif
        
        ESP_LOGI(TAG, "Player stopped");
    }

    return ESP_OK;
}

esp_err_t mjpeg_player_set_loop(mjpeg_player_handle_t handle, bool enable) {
    mjpeg_player_t *player = (mjpeg_player_t *)handle;
    if (!player) {
        return ESP_ERR_INVALID_ARG;
    }

    player->is_loop = enable;
    ESP_LOGI(TAG, "Loop mode %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t mjpeg_player_destroy(mjpeg_player_handle_t handle) {
    mjpeg_player_t *player = (mjpeg_player_t *)handle;
    if (!player) {
        return ESP_ERR_INVALID_ARG;
    }

    mjpeg_player_stop(handle);
    media_src_storage_close(&player->file);

#if CONFIG_IDF_TARGET_ESP32P4
    video_decoder_deinit(player);
#endif

    if (player->in_buff) {
        heap_caps_free(player->in_buff);
    }
    if (player->out_buff) {
        heap_caps_free(player->out_buff);
    }
    if (player->cache_buff) {
        heap_caps_free(player->cache_buff);
    }

    heap_caps_free(player);
    ESP_LOGI(TAG, "MJPEG player destroyed");
    return ESP_OK;
}
