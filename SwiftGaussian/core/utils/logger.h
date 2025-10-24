#ifndef LOGGER
#define LOGGER

#include <stdio.h>
#ifdef ANDROID
#include <android/log.h>
#define print(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "TEST", fmt, ##__VA_ARGS__)
#define error(x) __android_log_write(ANDROID_LOG_ERROR, "TEST", x)
#define warn(x) __android_log_write(ANDROID_LOG_WARN, "TEST", x)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "ImagePixels", __VA_ARGS__)
#define LOG(tag, ...) __android_log_print(ANDROID_LOG_DEBUG, tag, __VA_ARGS__)
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(__posix__) || defined(__macos__) || defined(__windows__)
#define print printf
#define error printf
#define warn printf

#else
#define print(x) "not implemented for this platform"
#define error(x) "not implemented for this platform"
#define warn(x) "not implemented for this platform"
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
#include <stdio.h>
#define print printf
#define error printf
#define warn printf
#endif

#endif // LOGGER
