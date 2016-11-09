/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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
#include "AutomaticThread.h"

#include "DataLog.h"

namespace WTF {

static const bool verbose = false;

RefPtr<AutomaticThreadCondition> AutomaticThreadCondition::create()
{
    return adoptRef(new AutomaticThreadCondition());
}

AutomaticThreadCondition::AutomaticThreadCondition()
{
}

AutomaticThreadCondition::~AutomaticThreadCondition()
{
}

void AutomaticThreadCondition::notifyOne(const LockHolder& locker)
{
    if (m_condition.notifyOne())
        return;
    
    if (m_threads.isEmpty())
        return;
    
    m_threads.takeLast()->start(locker);
}

void AutomaticThreadCondition::notifyAll(const LockHolder& locker)
{
    m_condition.notifyAll();
    
    for (AutomaticThread* thread : m_threads)
        thread->start(locker);
    m_threads.clear();
}

void AutomaticThreadCondition::wait(Lock& lock)
{
    m_condition.wait(lock);
}

void AutomaticThreadCondition::add(const LockHolder&, AutomaticThread* thread)
{
    ASSERT(!m_threads.contains(thread));
    m_threads.append(thread);
}

void AutomaticThreadCondition::remove(const LockHolder&, AutomaticThread* thread)
{
    m_threads.removeFirst(thread);
    ASSERT(!m_threads.contains(thread));
}

bool AutomaticThreadCondition::contains(const LockHolder&, AutomaticThread* thread)
{
    return m_threads.contains(thread);
}

AutomaticThread::AutomaticThread(const LockHolder& locker, Box<Lock> lock, RefPtr<AutomaticThreadCondition> condition)
    : m_lock(lock)
    , m_condition(condition)
{
    if (verbose)
        dataLog(RawPointer(this), ": Allocated AutomaticThread.\n");
    m_condition->add(locker, this);
}

AutomaticThread::~AutomaticThread()
{
    if (verbose)
        dataLog(RawPointer(this), ": Deleting AutomaticThread.\n");
    LockHolder locker(*m_lock);
    
    // It's possible that we're in a waiting state with the thread shut down. This is a goofy way to
    // die, but it could happen.
    m_condition->remove(locker, this);
}

bool AutomaticThread::tryStop(const LockHolder&)
{
    if (!m_isRunning)
        return true;
    if (m_hasUnderlyingThread)
        return false;
    m_isRunning = false;
    return true;
}

void AutomaticThread::join()
{
    LockHolder locker(*m_lock);
    while (m_isRunning)
        m_isRunningCondition.wait(*m_lock);
}

class AutomaticThread::ThreadScope {
public:
    ThreadScope(RefPtr<AutomaticThread> thread)
        : m_thread(thread)
    {
        m_thread->threadDidStart();
    }
    
    ~ThreadScope()
    {
        m_thread->threadWillStop();
        
        LockHolder locker(*m_thread->m_lock);
        m_thread->m_hasUnderlyingThread = false;
    }

private:
    RefPtr<AutomaticThread> m_thread;
};

void AutomaticThread::start(const LockHolder&)
{
    RELEASE_ASSERT(m_isRunning);
    
    RefPtr<AutomaticThread> preserveThisForThread = this;
    
    m_hasUnderlyingThread = true;
    
    ThreadIdentifier thread = createThread(
        "WTF::AutomaticThread",
        [=] () {
            if (verbose)
                dataLog(RawPointer(this), ": Running automatic thread!\n");
            ThreadScope threadScope(preserveThisForThread);
            
            if (!ASSERT_DISABLED) {
                LockHolder locker(*m_lock);
                ASSERT(!m_condition->contains(locker, this));
            }
            
            auto stop = [&] (const LockHolder&) {
                m_isRunning = false;
                m_isRunningCondition.notifyAll();
            };
            
            for (;;) {
                {
                    LockHolder locker(*m_lock);
                    for (;;) {
                        PollResult result = poll(locker);
                        if (result == PollResult::Work)
                            break;
                        if (result == PollResult::Stop)
                            return stop(locker);
                        RELEASE_ASSERT(result == PollResult::Wait);
                        // Shut the thread down after one second.
                        bool awokenByNotify =
                            m_condition->m_condition.waitFor(*m_lock, Seconds(1));
                        if (!awokenByNotify) {
                            if (verbose)
                                dataLog(RawPointer(this), ": Going to sleep!\n");
                            m_condition->add(locker, this);
                            return;
                        }
                    }
                }
                
                WorkResult result = work();
                if (result == WorkResult::Stop) {
                    LockHolder locker(*m_lock);
                    return stop(locker);
                }
                RELEASE_ASSERT(result == WorkResult::Continue);
            }
        });
    detachThread(thread);
}

void AutomaticThread::threadDidStart()
{
}

void AutomaticThread::threadWillStop()
{
}

} // namespace WTF

