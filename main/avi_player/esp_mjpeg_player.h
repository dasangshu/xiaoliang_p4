#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "stdbool.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef void* mjpeg_player_handle_t;

/**
 * @brief MJPEG播放器配置
 */
typedef struct {
    size_t frame_buffer_size;     /*!< 帧缓冲区大小，用于存储JPEG数据 */
    size_t cache_buffer_size;     /*!< 文件读取缓存大小 */
    bool cache_in_psram;          /*!< 是否使用PSRAM存储缓存 */
    int task_priority;            /*!< 任务优先级 */
    int task_core;               /*!< 运行的CPU核心ID */
    void (*on_frame_cb)(uint8_t *rgb565, uint32_t width, uint32_t height, void* ctx); /*!< 帧回调函数 */
    void* user_data;             /*!< 用户数据,会传递给回调函数 */
} mjpeg_player_config_t;

/**
 * @brief 创建MJPEG播放器实例
 * @param config 配置参数
 * @param[out] handle 返回的播放器句柄
 * @return ESP_OK成功，其他值失败
 */
esp_err_t mjpeg_player_create(const mjpeg_player_config_t* config, mjpeg_player_handle_t* handle);

/**
 * @brief 开始播放MJPEG文件
 * @param handle 播放器句柄
 * @param filepath MJPEG文件路径
 * @return ESP_OK成功，其他值失败
 */
esp_err_t mjpeg_player_play_file(mjpeg_player_handle_t handle, const char* filepath);

/**
 * @brief 从内存播放MJPEG数据
 * @param handle 播放器句柄
 * @param data MJPEG数据缓冲区
 * @param size 数据大小
 * @return ESP_OK成功，其他值失败
 */
esp_err_t mjpeg_player_play_memory(mjpeg_player_handle_t handle, const uint8_t* data, size_t size);

/**
 * @brief 停止播放
 * @param handle 播放器句柄
 * @return ESP_OK成功，其他值失败
 */
esp_err_t mjpeg_player_stop(mjpeg_player_handle_t handle);

/**
 * @brief 设置是否循环播放
 * @param handle 播放器句柄
 * @param enable true开启循环播放,false关闭
 * @return ESP_OK成功，其他值失败
 */
esp_err_t mjpeg_player_set_loop(mjpeg_player_handle_t handle, bool enable);

/**
 * @brief 销毁播放器实例
 * @param handle 播放器句柄
 * @return ESP_OK成功，其他值失败
 */
esp_err_t mjpeg_player_destroy(mjpeg_player_handle_t handle);

#ifdef __cplusplus
}
#endif
