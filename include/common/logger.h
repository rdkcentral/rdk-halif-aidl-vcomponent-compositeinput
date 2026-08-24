/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

/**
 * @file logger.h
 * @brief Tiny printf-style logging helper for vcomponent utilities.
 *
 * This is intentionally minimal; real implementations should integrate with the platform logging
 * facility.
 */

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>

namespace vcomponent
{
namespace common
{

/**
 * @brief Logging severity.
 */
enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

/**
 * @brief Convert LogLevel to a stable string.
 *
 * @param[in] level  Log level.
 * @return C-string representation.
 */
inline const char* toString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        default:              return "UNKNOWN";
    }
}

/**
 * @brief Write a log line to stderr.
 *
 * @param[in] level  Log level.
 * @param[in] tag    Optional tag (may be empty).
 * @param[in] msg    Message text.
 */
inline void log(LogLevel level, const std::string& tag, const std::string& msg)
{
    if (!tag.empty())
    {
        std::cerr << "[" << toString(level) << "][" << tag << "] " << msg << std::endl;
    }
    else
    {
        std::cerr << "[" << toString(level) << "] " << msg << std::endl;
    }
}

/**
 * @brief Format a printf-style string into a std::string.
 *
 * @param[in] fmt     printf-style format string.
 * @param[in] argList va_list argument list.
 * @return Formatted string (may be empty if formatting fails).
 */
inline std::string vformat(const char* formatString, va_list argList)
{
    // Determine required length.
    va_list argListCopy;
    va_copy(argListCopy, argList);
    const int formattedLength = std::vsnprintf(nullptr, 0, formatString, argListCopy);
    va_end(argListCopy);

    if (formattedLength <= 0)
    {
        return std::string{};
    }

    std::string output;
    output.resize(static_cast<size_t>(formattedLength) + 1U);

    // Write formatted string (std::string storage is contiguous).
    const int resultLength =
        std::vsnprintf(output.data(), output.size(), formatString, argList);
    if (resultLength < 0)
    {
        return std::string{};
    }

    // Remove the trailing NUL from the logical string size.
    output.resize(static_cast<size_t>(resultLength));
    return output;
}

/**
 * @brief Log a printf-style formatted message at the given level.
 *
 * @param[in] level  Log level.
 * @param[in] fmt    printf-style format string.
 */
inline void logf(LogLevel level, const char* fmt, ...)
{
    va_list argList;
    va_start(argList, fmt);
    std::string msg = vformat(fmt, argList);
    va_end(argList);

    log(level, /*tag*/ "", msg);
}

/**
 * @brief Debug helper that prefixes function and line number.
 *
 * @param[in] func  Function name string (may be null).
 * @param[in] line  Line number.
 * @param[in] fmt   printf-style format string.
 */
inline void logfDebug(const char* func, int line, const char* fmt, ...)
{
    va_list argList;
    va_start(argList, fmt);
    std::string msg = vformat(fmt, argList);
    va_end(argList);

    std::string decorated;
    decorated.reserve(msg.size() + 48);
    decorated += "[";
    decorated += func ? func : "?";
    decorated += ":";
    decorated += std::to_string(line);
    decorated += "] ";
    decorated += msg;

    log(LogLevel::Debug, /*tag*/ "", decorated);
}

} // namespace common
} // namespace vcomponent

// LOGF_* macros (printf-style) used across HDMI Output component.
#define LOGF_INFO(fmt, ...)  ::vcomponent::common::logf(::vcomponent::common::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define LOGF_WARN(fmt, ...)  ::vcomponent::common::logf(::vcomponent::common::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define LOGF_ERR(fmt, ...)   ::vcomponent::common::logf(::vcomponent::common::LogLevel::Error, fmt, ##__VA_ARGS__)
#define LOGF_ERROR(fmt, ...) LOGF_ERR(fmt, ##__VA_ARGS__)
#define LOGF_DEBUG(fmt, ...) ::vcomponent::common::logfDebug(__func__, __LINE__, fmt, ##__VA_ARGS__)

// Backward compatible legacy macros (prefer LOGF_* at callsites).
#define VCOMP_LOGD(TAG, MSG) ::vcomponent::common::log(::vcomponent::common::LogLevel::Debug, (TAG), (MSG))
#define VCOMP_LOGI(TAG, MSG) ::vcomponent::common::log(::vcomponent::common::LogLevel::Info,  (TAG), (MSG))
#define VCOMP_LOGW(TAG, MSG) ::vcomponent::common::log(::vcomponent::common::LogLevel::Warn,  (TAG), (MSG))
#define VCOMP_LOGE(TAG, MSG) ::vcomponent::common::log(::vcomponent::common::LogLevel::Error, (TAG), (MSG))
