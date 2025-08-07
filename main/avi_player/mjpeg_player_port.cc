#include "mjpeg_player_port.h"
#include "esp_mjpeg_player.h"
#include "esp_heap_caps.h"
#include "fs_manager.h"
#include "board.h"
#include "display.h"
#include <string.h>

static const char *TAG = "mjpeg_player_port";

// 播放任务类型
typedef enum {
    PLAYER_TASK_PLAY,      // 播放新文件
    PLAYER_TASK_STOP,      // 停止播放
    PLAYER_TASK_SET_LOOP   // 设置循环模式
} player_task_type_t;

// 播放任务结构
typedef struct {
    player_task_type_t type;   // 任务类型
    char filepath[256];        // 文件路径
    bool loop_mode;           // 循环模式
} player_task_t;

// 播放器状态
typedef enum {
    PLAYER_STATE_IDLE,        // 空闲状态
    PLAYER_STATE_PLAYING,     // 正在播放
    PLAYER_STATE_STOPPING,   // 正在停止
    PLAYER_STATE_SWITCHING    // 正在切换文件
} player_state_t;

// 全局变量
static struct {
    mjpeg_player_handle_t handle;      // 播放器句柄
    QueueHandle_t task_queue;          // 任务队列
    TaskHandle_t manager_task;         // 管理任务句柄
    SemaphoreHandle_t state_mutex;     // 状态保护互斥锁
    player_state_t state;              // 播放器状态
    bool loop_enabled;                 // 是否启用循环
    char current_file[256];           // 当前播放文件
    volatile bool shutdown_requested;  // 是否请求关闭
} player = {
    .handle = NULL,
    .task_queue = NULL,
    .manager_task = NULL,
    .state_mutex = NULL,
    .state = PLAYER_STATE_IDLE,
    .loop_enabled = true,
    .current_file = {0},
    .shutdown_requested = false
};

// 帧率统计相关变量
static uint32_t frame_count = 0;
static int64_t last_fps_time = 0;
static uint32_t fps_frame_count = 0;
static int64_t last_frame_time = 0;

// 安全获取播放器状态
static player_state_t get_player_state(void) {
    player_state_t state = PLAYER_STATE_IDLE;
    if (player.state_mutex && xSemaphoreTake(player.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = player.state;
        xSemaphoreGive(player.state_mutex);
    }
    return state;
}

// 安全设置播放器状态
static bool set_player_state(player_state_t new_state) {
    bool success = false;
    if (player.state_mutex && xSemaphoreTake(player.state_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        ESP_LOGI(TAG, "State transition: %d -> %d", player.state, new_state);
        player.state = new_state;
        success = true;
        xSemaphoreGive(player.state_mutex);
    }
    return success;
}

// 视频帧回调函数
static void frame_callback(uint8_t *rgb565, uint32_t width, uint32_t height, void* ctx) {
    // 检查是否还在播放状态
    player_state_t state = get_player_state();
    if (state != PLAYER_STATE_PLAYING) {
        ESP_LOGD(TAG, "Frame callback called but not in playing state: %d", state);
        return;
    }

    int64_t start_time = esp_timer_get_time();
    
    // 计算帧间隔时间
    int64_t frame_interval = 0;
    if (last_frame_time > 0) {
        frame_interval = start_time - last_frame_time;
    }
    last_frame_time = start_time;

    // 显示帧
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->SetFaceImage(rgb565, width, height);
    }

    // 帧率统计
    frame_count++;
    fps_frame_count++;

    int64_t current_time = esp_timer_get_time();

    // 每100帧或每5秒显示一次统计信息
    if (fps_frame_count >= 100 || (current_time - last_fps_time) >= 5000000) {
        if (last_fps_time > 0) {
            int64_t time_diff = current_time - last_fps_time;
            float fps = (float)fps_frame_count * 1000000.0f / time_diff;

#if CONFIG_IDF_TARGET_ESP32P4
            ESP_LOGI(TAG, "[HW DECODE] Frame Stats - FPS: %.1f, Frames: %u, interval=%.1f ms", 
#else
            ESP_LOGI(TAG, "[SW DECODE] Frame Stats - FPS: %.1f, Frames: %u, interval=%.1f ms", 
#endif
                     fps, (unsigned int)frame_count, frame_interval / 1000.0f);
        }
        
        last_fps_time = current_time;
        fps_frame_count = 0;
    }
}

// 安全停止播放器
static esp_err_t safe_stop_player(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Starting safe stop procedure...");
    
    player_state_t current_state = get_player_state();
    if (current_state == PLAYER_STATE_IDLE) {
        ESP_LOGI(TAG, "Player already in idle state");
        return ESP_OK;
    }
    
    // 设置为停止状态
    if (!set_player_state(PLAYER_STATE_STOPPING)) {
        ESP_LOGE(TAG, "Failed to set stopping state");
        return ESP_FAIL;
    }
    
    // 停止MJPEG播放器
    esp_err_t ret = mjpeg_player_stop(player.handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MJPEG player stop returned error: %d", ret);
    }
    
    // 等待播放器真正停止
    uint32_t wait_time = 0;
    const uint32_t check_interval = 10; // 10ms检查间隔
    
    while (wait_time < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        wait_time += check_interval;
        
        // 这里可以添加检查播放器是否真正停止的逻辑
        // 目前简单等待一段时间
        if (wait_time >= 200) { // 至少等待200ms
            break;
        }
    }
    
    // 设置为空闲状态
    set_player_state(PLAYER_STATE_IDLE);
    
    ESP_LOGI(TAG, "Safe stop procedure completed in %lu ms", wait_time);
    return ESP_OK;
}

// 任务管理器函数
static void player_manager_task(void *arg) {
    player_task_t task;
    
    ESP_LOGI(TAG, "MJPEG player manager task started");
    
    while (!player.shutdown_requested) {
        // 使用较短的超时来检查关闭请求
        if (xQueueReceive(player.task_queue, &task, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (task.type) {
                case PLAYER_TASK_PLAY: {
                    ESP_LOGI(TAG, "Processing play task for: %s", task.filepath);
                    
                    // 如果正在播放，先安全停止
                    player_state_t current_state = get_player_state();
                    if (current_state != PLAYER_STATE_IDLE) {
                        ESP_LOGI(TAG, "Stopping current playback before starting new one");
                        set_player_state(PLAYER_STATE_SWITCHING);
                        safe_stop_player(1000); // 1秒超时
                    }
                    
                    // 重置统计
                    frame_count = 0;
                    fps_frame_count = 0;
                    last_fps_time = esp_timer_get_time();
                    last_frame_time = 0;
                    
                    // 获取文件系统类型和构建完整路径
                    fs_type_t fs_type = fs_manager_get_type();
                    const char *mount_path = (fs_type == FS_TYPE_SD_CARD) ? "/sdcard" : "/spiffs";
                    
                    char full_path[256] = {0};
                    size_t path_len = 0;
                    
                    if (strncmp(task.filepath, "/sdcard/", 8) == 0 || 
                        strncmp(task.filepath, "/spiffs/", 8) == 0) {
                        path_len = strlen(task.filepath);
                        if (path_len >= sizeof(full_path)) {
                            ESP_LOGE(TAG, "File path too long");
                            break;
                        }
                        memcpy(full_path, task.filepath, path_len);
                    } else if (task.filepath[0] == '/') {
                        path_len = strlen(mount_path) + strlen(task.filepath);
                        if (path_len >= sizeof(full_path)) {
                            ESP_LOGE(TAG, "Combined path too long");
                            break;
                        }
                        memcpy(full_path, mount_path, strlen(mount_path));
                        memcpy(full_path + strlen(mount_path), task.filepath, strlen(task.filepath));
                    } else {
                        path_len = strlen(mount_path) + 1 + strlen(task.filepath);
                        if (path_len >= sizeof(full_path)) {
                            ESP_LOGE(TAG, "Combined path too long");
                            break;
                        }
                        memcpy(full_path, mount_path, strlen(mount_path));
                        full_path[strlen(mount_path)] = '/';
                        memcpy(full_path + strlen(mount_path) + 1, task.filepath, strlen(task.filepath));
                    }
                    full_path[path_len] = '\0';
                    
                    // 开始播放
                    esp_err_t ret = mjpeg_player_play_file(player.handle, full_path);
                    if (ret == ESP_OK) {
                        set_player_state(PLAYER_STATE_PLAYING);
                        size_t copy_len = strlen(full_path);
                        if (copy_len >= sizeof(player.current_file)) {
                            copy_len = sizeof(player.current_file) - 1;
                        }
                        memcpy(player.current_file, full_path, copy_len);
                        player.current_file[copy_len] = '\0';
                        ESP_LOGI(TAG, "Successfully started playing: %s", full_path);
                    } else {
                        set_player_state(PLAYER_STATE_IDLE);
                        ESP_LOGE(TAG, "Failed to play: %s, error: %d", full_path, ret);
                    }
                    break;
                }
                
                case PLAYER_TASK_STOP: {
                    ESP_LOGI(TAG, "Processing stop task");
                    esp_err_t ret = safe_stop_player(1000);
                    if (ret == ESP_OK) {
                        memset(player.current_file, 0, sizeof(player.current_file));
                        ESP_LOGI(TAG, "Playback stopped successfully");
                    } else {
                        ESP_LOGW(TAG, "Stop task completed with warnings");
                    }
                    break;
                }
                
                case PLAYER_TASK_SET_LOOP: {
                    ESP_LOGI(TAG, "Processing set loop task: %s", task.loop_mode ? "enabled" : "disabled");
                    player.loop_enabled = task.loop_mode;
                    mjpeg_player_set_loop(player.handle, task.loop_mode);
                    break;
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "MJPEG player manager task shutting down");
    vTaskDelete(NULL);
}

esp_err_t mjpeg_player_port_init(mjpeg_player_port_config_t *config) {
    if (player.handle != NULL) {
        ESP_LOGW(TAG, "MJPEG player already initialized");
        return ESP_OK;
    }

    // 创建状态保护互斥锁
    player.state_mutex = xSemaphoreCreateMutex();
    if (player.state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    // 配置SD卡
    fs_config_t sdcard_config = {
        .type = FS_TYPE_SD_CARD,
        .sd_card = {
            .mount_point = "/sdcard",
            .clk = GPIO_NUM_2,
            .cmd = GPIO_NUM_42,
            .d0 = GPIO_NUM_1,
            .d1 = GPIO_NUM_NC,
            .d2 = GPIO_NUM_NC,
            .d3 = GPIO_NUM_NC,
            .format_if_mount_failed = false,
            .max_files = 5
        }
    };
#elif CONFIG_IDF_TARGET_ESP32P4
    // 配置SD卡
    fs_config_t sdcard_config = {
        .type = FS_TYPE_SD_CARD,
        .sd_card = {
            .mount_point = "/sdcard",
            .clk = GPIO_NUM_43,
            .cmd = GPIO_NUM_44,
            .d0 = GPIO_NUM_39,
            .d1 = GPIO_NUM_40,
            .d2 = GPIO_NUM_41,
            .d3 = GPIO_NUM_42,
            .format_if_mount_failed = false,
            .max_files = 5
        }
    };
#else
#error "Unsupported target"
#endif

    // 使用SPIFFS作为备选
    fs_config_t spiffs_config = {
        .type = FS_TYPE_SPIFFS,
        .spiffs = {
            .base_path = "/spiffs",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true
        }
    };

    // 自动初始化文件系统，先尝试TF卡，失败则使用SPIFFS
    ESP_ERROR_CHECK(fs_manager_auto_init(&sdcard_config, &spiffs_config));

    // 获取当前使用的文件系统类型
    fs_type_t fs_type = fs_manager_get_type();
    const char *mount_path = (fs_type == FS_TYPE_SD_CARD) ? "/sdcard" : "/spiffs";
    ESP_LOGI(TAG, "Using filesystem: %s", mount_path);

    // 列出文件
    fs_manager_list_files(mount_path);

    // 创建MJPEG播放器
    mjpeg_player_config_t player_config = {
        .frame_buffer_size = config->buffer_size ? config->buffer_size : 64 * 1024,  // 默认64KB
        .cache_buffer_size = 64 * 1024,
        .cache_in_psram = config->use_psram,
        .task_priority = 2,
        .task_core = config->core_id,
        .on_frame_cb = frame_callback,
        .user_data = NULL
    };

    esp_err_t ret = mjpeg_player_create(&player_config, &player.handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MJPEG player");
        vSemaphoreDelete(player.state_mutex);
        player.state_mutex = NULL;
        return ret;
    }

    // 创建任务队列
    player.task_queue = xQueueCreate(5, sizeof(player_task_t)); // 减少队列大小避免堆积
    if (player.task_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create task queue");
        mjpeg_player_destroy(player.handle);
        player.handle = NULL;
        vSemaphoreDelete(player.state_mutex);
        player.state_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 创建管理任务
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        player_manager_task,
        "mjpeg_manager",
        6144,  // 增加栈大小
        NULL,
        config->task_priority,
        &player.manager_task,
        config->core_id);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create manager task");
        vQueueDelete(player.task_queue);
        player.task_queue = NULL;
        mjpeg_player_destroy(player.handle);
        player.handle = NULL;
        vSemaphoreDelete(player.state_mutex);
        player.state_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 默认启用循环播放
    mjpeg_player_set_loop(player.handle, true);
    
    ESP_LOGI(TAG, "MJPEG player port initialized successfully");
    return ESP_OK;
}

esp_err_t mjpeg_player_port_play_file(const char* filepath) {
    if (player.handle == NULL || player.task_queue == NULL) {
        ESP_LOGE(TAG, "MJPEG player not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (player.shutdown_requested) {
        ESP_LOGW(TAG, "Shutdown requested, ignoring play request");
        return ESP_ERR_INVALID_STATE;
    }

    player_task_t task = {
        .type = PLAYER_TASK_PLAY
    };
    size_t path_len = strlen(filepath);
    if (path_len >= sizeof(task.filepath)) {
        ESP_LOGE(TAG, "File path too long");
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(task.filepath, filepath, path_len);
    task.filepath[path_len] = '\0';

    // 清空队列中旧的播放任务，避免堆积
    player_task_t old_task;
    while (xQueueReceive(player.task_queue, &old_task, 0) == pdTRUE) {
        ESP_LOGD(TAG, "Removed old task from queue");
    }

    if (xQueueSend(player.task_queue, &task, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send play task - queue full");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Play task queued for file: %s", filepath);
    return ESP_OK;
}

esp_err_t mjpeg_player_port_stop(void) {
    if (player.handle == NULL || player.task_queue == NULL) {
        ESP_LOGW(TAG, "MJPEG player not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    player_task_t task = {
        .type = PLAYER_TASK_STOP
    };
    
    if (xQueueSend(player.task_queue, &task, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send stop task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stop task queued");
    return ESP_OK;
}

void mjpeg_player_port_set_loop(bool enable) {
    if (player.handle == NULL || player.task_queue == NULL) {
        return;
    }

    player_task_t task = {
        .type = PLAYER_TASK_SET_LOOP,
        .loop_mode = enable
    };
    
    if (xQueueSend(player.task_queue, &task, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send set loop task");
        return;
    }

    ESP_LOGI(TAG, "Set loop task queued: %s", enable ? "enabled" : "disabled");
}

void mjpeg_player_port_deinit(void) {
    if (player.handle == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Starting deinitialize procedure...");
    
    // 请求关闭
    player.shutdown_requested = true;
    
    // 发送停止任务
    player_task_t stop_task = {
        .type = PLAYER_TASK_STOP
    };
    xQueueSend(player.task_queue, &stop_task, pdMS_TO_TICKS(500));
    
    // 等待管理任务结束
    if (player.manager_task != NULL) {
        uint32_t timeout = 2000; // 2秒超时
        while (eTaskGetState(player.manager_task) != eDeleted && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (timeout == 0) {
            ESP_LOGW(TAG, "Manager task did not finish gracefully, force deleting");
            vTaskDelete(player.manager_task);
        }
        player.manager_task = NULL;
    }

    // 删除任务队列
    if (player.task_queue != NULL) {
        vQueueDelete(player.task_queue);
        player.task_queue = NULL;
    }

    // 销毁MJPEG播放器
    if (player.handle != NULL) {
        mjpeg_player_destroy(player.handle);
        player.handle = NULL;
    }

    // 删除互斥锁
    if (player.state_mutex != NULL) {
        vSemaphoreDelete(player.state_mutex);
        player.state_mutex = NULL;
    }

    memset(&player, 0, sizeof(player));
    ESP_LOGI(TAG, "MJPEG player port deinitialized successfully");
}
