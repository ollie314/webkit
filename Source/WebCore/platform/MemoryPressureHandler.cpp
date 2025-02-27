/*
 * Copyright (C) 2011, 2014 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MemoryPressureHandler.h"

#include "Logging.h"

namespace WebCore {

WEBCORE_EXPORT bool MemoryPressureHandler::ReliefLogger::s_loggingEnabled = false;

MemoryPressureHandler& MemoryPressureHandler::singleton()
{
    static NeverDestroyed<MemoryPressureHandler> memoryPressureHandler;
    return memoryPressureHandler;
}

MemoryPressureHandler::MemoryPressureHandler()
#if OS(LINUX)
    : m_holdOffTimer(RunLoop::main(), this, &MemoryPressureHandler::holdOffTimerFired)
#endif
{
}

void MemoryPressureHandler::beginSimulatedMemoryPressure()
{
    m_isSimulatingMemoryPressure = true;
    respondToMemoryPressure(Critical::Yes, Synchronous::Yes);
}

void MemoryPressureHandler::endSimulatedMemoryPressure()
{
    m_isSimulatingMemoryPressure = false;
}

void MemoryPressureHandler::releaseMemory(Critical critical, Synchronous synchronous)
{
    if (!m_lowMemoryHandler)
        return;

    m_lowMemoryHandler(critical, synchronous);
    platformReleaseMemory(critical);
}

void MemoryPressureHandler::ReliefLogger::logMemoryUsageChange()
{
#if !RELEASE_LOG_DISABLED
#define STRING_SPECIFICATION "%{public}s"
#define MEMORYPRESSURE_LOG(...) RELEASE_LOG(MemoryPressure, __VA_ARGS__)
#else
#define STRING_SPECIFICATION "%s"
#define MEMORYPRESSURE_LOG(...) WTFLogAlways(__VA_ARGS__)
#endif

    if (s_loggingEnabled) {
        size_t currentMemory = platformMemoryUsage();
        if (currentMemory == static_cast<size_t>(-1) || m_initialMemory == static_cast<size_t>(-1)) {
            MEMORYPRESSURE_LOG("Memory pressure relief: " STRING_SPECIFICATION ": (Unable to get dirty memory information for process)", m_logString);
            return;
        }

        long memoryDiff = currentMemory - m_initialMemory;
        if (memoryDiff < 0)
            MEMORYPRESSURE_LOG("Memory pressure relief: " STRING_SPECIFICATION ": -dirty %ld bytes (from %zu to %zu)", m_logString, (memoryDiff * -1), m_initialMemory, currentMemory);
        else if (memoryDiff > 0)
            MEMORYPRESSURE_LOG("Memory pressure relief: " STRING_SPECIFICATION ": +dirty %ld bytes (from %zu to %zu)", m_logString, memoryDiff, m_initialMemory, currentMemory);
        else
            MEMORYPRESSURE_LOG("Memory pressure relief: " STRING_SPECIFICATION ": =dirty (at %zu bytes)", m_logString, currentMemory);
#if !RELEASE_LOG_DISABLED
    } else {
        MEMORYPRESSURE_LOG("Memory pressure relief: " STRING_SPECIFICATION, m_logString);
#endif
    }
}

#if !PLATFORM(COCOA) && !OS(LINUX) && !PLATFORM(WIN)
void MemoryPressureHandler::install() { }
void MemoryPressureHandler::uninstall() { }
void MemoryPressureHandler::holdOff(unsigned) { }
void MemoryPressureHandler::respondToMemoryPressure(Critical, Synchronous) { }
void MemoryPressureHandler::platformReleaseMemory(Critical) { }
size_t MemoryPressureHandler::ReliefLogger::platformMemoryUsage() { return 0; }
#endif

} // namespace WebCore
