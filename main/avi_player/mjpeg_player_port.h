#ifndef MJPEG_PLAYER_PORT_H
#define MJPEG_PLAYER_PORT_H

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MJPEG播放器端口配置
 */
typedef struct {
    size_t buffer_size;     /*!< 内部缓冲区大小，0表示使用默认值32KB */
    int core_id;           /*!< 运行的CPU核心ID，0或1 */
    bool use_psram;        /*!< 是否使用PSRAM存储缓存 */
    int task_priority;     /*!< 播放器任务优先级，1-20，数值越大优先级越高 */
} mjpeg_player_port_config_t;

/**
 * @brief 初始化MJPEG播放器端口
 * @param config 播放器配置
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t mjpeg_player_port_init(mjpeg_player_port_config_t *config);

/**
 * @brief 播放指定文件
 * @param filepath 文件路径（支持相对路径和绝对路径）
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t mjpeg_player_port_play_file(const char* filepath);

/**
 * @brief 停止播放
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t mjpeg_player_port_stop(void);

/**
 * @brief 设置是否循环播放
 * @param enable true启用循环，false禁用循环
 */
void mjpeg_player_port_set_loop(bool enable);

/**
 * @brief 反初始化播放器端口
 */
void mjpeg_player_port_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MJPEG_PLAYER_PORT_H
