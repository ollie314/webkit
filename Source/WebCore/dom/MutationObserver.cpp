/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "MutationObserver.h"

#include "Dictionary.h"
#include "Document.h"
#include "ExceptionCode.h"
#include "HTMLSlotElement.h"
#include "Microtasks.h"
#include "MutationCallback.h"
#include "MutationObserverRegistration.h"
#include "MutationRecord.h"
#include <algorithm>
#include <wtf/MainThread.h>

namespace WebCore {

static unsigned s_observerPriority = 0;

Ref<MutationObserver> MutationObserver::create(Ref<MutationCallback>&& callback)
{
    ASSERT(isMainThread());
    return adoptRef(*new MutationObserver(WTFMove(callback)));
}

MutationObserver::MutationObserver(Ref<MutationCallback>&& callback)
    : m_callback(WTFMove(callback))
    , m_priority(s_observerPriority++)
{
}

MutationObserver::~MutationObserver()
{
    ASSERT(m_registrations.isEmpty());
}

bool MutationObserver::validateOptions(MutationObserverOptions options)
{
    return (options & (Attributes | CharacterData | ChildList))
        && ((options & Attributes) || !(options & AttributeOldValue))
        && ((options & Attributes) || !(options & AttributeFilter))
        && ((options & CharacterData) || !(options & CharacterDataOldValue));
}

ExceptionOr<void> MutationObserver::observe(Node& node, const Init& init)
{
    MutationObserverOptions options = 0;

    if (init.childList)
        options |= ChildList;
    if (init.subtree)
        options |= Subtree;
    if (init.attributeOldValue.valueOr(false))
        options |= AttributeOldValue;
    if (init.characterDataOldValue.valueOr(false))
        options |= CharacterDataOldValue;

    HashSet<AtomicString> attributeFilter;
    if (init.attributeFilter) {
        for (auto& value : init.attributeFilter.value())
            attributeFilter.add(value);
        options |= AttributeFilter;
    }

    if (init.attributes ? init.attributes.value() : (options & (AttributeFilter | AttributeOldValue)))
        options |= Attributes;

    if (init.characterData ? init.characterData.value() : (options & CharacterDataOldValue))
        options |= CharacterData;

    if (!validateOptions(options))
        return Exception { TypeError };

    node.registerMutationObserver(this, options, attributeFilter);

    return { };
}

Vector<Ref<MutationRecord>> MutationObserver::takeRecords()
{
    Vector<Ref<MutationRecord>> records;
    records.swap(m_records);
    return records;
}

void MutationObserver::disconnect()
{
    m_records.clear();
    HashSet<MutationObserverRegistration*> registrations(m_registrations);
    for (auto* registration : registrations)
        MutationObserverRegistration::unregisterAndDelete(registration);
}

void MutationObserver::observationStarted(MutationObserverRegistration& registration)
{
    ASSERT(!m_registrations.contains(&registration));
    m_registrations.add(&registration);
}

void MutationObserver::observationEnded(MutationObserverRegistration& registration)
{
    ASSERT(m_registrations.contains(&registration));
    m_registrations.remove(&registration);
}

typedef HashSet<RefPtr<MutationObserver>> MutationObserverSet;

static MutationObserverSet& activeMutationObservers()
{
    static NeverDestroyed<MutationObserverSet> activeObservers;
    return activeObservers;
}

static MutationObserverSet& suspendedMutationObservers()
{
    static NeverDestroyed<MutationObserverSet> suspendedObservers;
    return suspendedObservers;
}

// https://dom.spec.whatwg.org/#signal-slot-list
static Vector<RefPtr<HTMLSlotElement>>& signalSlotList()
{
    static NeverDestroyed<Vector<RefPtr<HTMLSlotElement>>> list;
    return list;
}

static bool mutationObserverCompoundMicrotaskQueuedFlag;

class MutationObserverMicrotask final : public Microtask {
    WTF_MAKE_FAST_ALLOCATED;
private:
    Result run() final
    {
        MutationObserver::notifyMutationObservers();
        return Result::Done;
    }
};

static void queueMutationObserverCompoundMicrotask()
{
    if (mutationObserverCompoundMicrotaskQueuedFlag)
        return;
    mutationObserverCompoundMicrotaskQueuedFlag = true;
    MicrotaskQueue::mainThreadQueue().append(std::make_unique<MutationObserverMicrotask>());
}

void MutationObserver::enqueueMutationRecord(Ref<MutationRecord>&& mutation)
{
    ASSERT(isMainThread());
    m_records.append(WTFMove(mutation));
    activeMutationObservers().add(this);

    queueMutationObserverCompoundMicrotask();
}

void MutationObserver::enqueueSlotChangeEvent(HTMLSlotElement& slot)
{
    ASSERT(isMainThread());
    ASSERT(!signalSlotList().contains(&slot));
    signalSlotList().append(&slot);

    queueMutationObserverCompoundMicrotask();
}

void MutationObserver::setHasTransientRegistration()
{
    ASSERT(isMainThread());
    activeMutationObservers().add(this);

    queueMutationObserverCompoundMicrotask();
}

HashSet<Node*> MutationObserver::observedNodes() const
{
    HashSet<Node*> observedNodes;
    for (auto* registration : m_registrations)
        registration->addRegistrationNodesToSet(observedNodes);
    return observedNodes;
}

bool MutationObserver::canDeliver()
{
    return m_callback->canInvokeCallback();
}

void MutationObserver::deliver()
{
    ASSERT(canDeliver());

    // Calling clearTransientRegistrations() can modify m_registrations, so it's necessary
    // to make a copy of the transient registrations before operating on them.
    Vector<MutationObserverRegistration*, 1> transientRegistrations;
    for (auto* registration : m_registrations) {
        if (registration->hasTransientRegistrations())
            transientRegistrations.append(registration);
    }
    for (auto& registration : transientRegistrations)
        registration->clearTransientRegistrations();

    if (m_records.isEmpty())
        return;

    Vector<Ref<MutationRecord>> records;
    records.swap(m_records);

    m_callback->call(records, this);
}

void MutationObserver::notifyMutationObservers()
{
    // https://dom.spec.whatwg.org/#notify-mutation-observers
    // 1. Unset mutation observer compound microtask queued flag.
    mutationObserverCompoundMicrotaskQueuedFlag = false;

    ASSERT(isMainThread());
    static bool deliveryInProgress = false;
    if (deliveryInProgress)
        return;
    deliveryInProgress = true;

    if (!suspendedMutationObservers().isEmpty()) {
        Vector<RefPtr<MutationObserver>> suspended;
        copyToVector(suspendedMutationObservers(), suspended);
        for (auto& observer : suspended) {
            if (!observer->canDeliver())
                continue;

            suspendedMutationObservers().remove(observer);
            activeMutationObservers().add(observer);
        }
    }

    while (!activeMutationObservers().isEmpty() || !signalSlotList().isEmpty()) {
        // 2. Let notify list be a copy of unit of related similar-origin browsing contexts' list of MutationObserver objects.
        Vector<RefPtr<MutationObserver>> notifyList;
        copyToVector(activeMutationObservers(), notifyList);
        activeMutationObservers().clear();
        std::sort(notifyList.begin(), notifyList.end(), [](auto& lhs, auto& rhs) {
            return lhs->m_priority < rhs->m_priority;
        });

        // 3. Let signalList be a copy of unit of related similar-origin browsing contexts' signal slot list.
        // 4. Empty unit of related similar-origin browsing contexts' signal slot list.
        Vector<RefPtr<HTMLSlotElement>> slotList;
        if (!signalSlotList().isEmpty()) {
            slotList.swap(signalSlotList());
            for (auto& slot : slotList)
                slot->didRemoveFromSignalSlotList();
        }

        // 5. For each MutationObserver object mo in notify list, execute a compound microtask subtask
        for (auto& observer : notifyList) {
            if (observer->canDeliver())
                observer->deliver();
            else
                suspendedMutationObservers().add(observer);
        }

        // 6. For each slot slot in signalList, in order, fire an event named slotchange, with its bubbles attribute set to true, at slot.
        for (auto& slot : slotList)
            slot->dispatchSlotChangeEvent();
    }

    deliveryInProgress = false;
}

} // namespace WebCore
