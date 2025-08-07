#ifndef INTEGRATION_EXAMPLE_H
#define INTEGRATION_EXAMPLE_H

#include <lvgl.h>

/**
 * @brief 初始化桌面MJPEG管理器
 * @param parent_obj LVGL父对象
 * @param width 屏幕宽度
 * @param height 屏幕高度
 */
void InitializeDesktopMjpegManager(lv_obj_t* parent_obj, int width, int height);

/**
 * @brief 处理唤醒事件，切换到MJPEG对话模式
 */
void HandleWakeUpEvent();

/**
 * @brief 返回桌面模式
 */
void ReturnToDesktopMode();

/**
 * @brief 设置管理器可见性
 * @param visible 是否可见
 */
void SetDesktopMjpegVisible(bool visible);

/**
 * @brief 清理资源
 */
void CleanupDesktopMjpegManager();

#endif // INTEGRATION_EXAMPLE_H