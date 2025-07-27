#ifndef DESK_IMAGE_H
#define DESK_IMAGE_H

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t desk;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // DESK_IMAGE_H