// ============================================================================
//  Logger.h - lowest priority output in the system.
//
//  Logging must never delay audio, MIDI or valve timing, so it is a plain
//  printf to the debug UART with a compile-time level.  Nothing in the audio
//  render path is allowed to call it.
// ============================================================================
#pragma once

#include "core/Platform.h"

#if defined(OT_HOST_BUILD)
#include <cstdio>
#define OT_LOGE(tag, fmt, ...) std::fprintf(stderr, "[E][" tag "] " fmt "\n", ##__VA_ARGS__)
#define OT_LOGW(tag, fmt, ...) std::fprintf(stderr, "[W][" tag "] " fmt "\n", ##__VA_ARGS__)
#define OT_LOGI(tag, fmt, ...) std::fprintf(stderr, "[I][" tag "] " fmt "\n", ##__VA_ARGS__)
#define OT_LOGD(tag, fmt, ...) ((void)0)
#else
#include <esp_log.h>
#define OT_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#define OT_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define OT_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define OT_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#endif
