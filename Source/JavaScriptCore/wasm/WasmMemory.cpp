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
#include "WasmMemory.h"

#if ENABLE(WEBASSEMBLY)

namespace JSC { namespace Wasm {

Memory::Memory(uint32_t startingSize, uint32_t capacity, const Vector<unsigned>& pinnedSizeRegisters)
    : m_mode(Mode::BoundsChecking)
    , m_size(startingSize)
    , m_capacity(capacity)
    // FIXME: If we add signal based bounds checking then we need extra space for overflow on load.
    // see: https://bugs.webkit.org/show_bug.cgi?id=162693
    , m_mappedCapacity(static_cast<uint64_t>(maxPageCount) * static_cast<uint64_t>(pageSize))
{
    ASSERT(pinnedSizeRegisters.size() > 0);

    // FIXME: It would be nice if we had a VM tag for wasm memory. https://bugs.webkit.org/show_bug.cgi?id=163600
    void* result = mmap(nullptr, m_mappedCapacity, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (result == MAP_FAILED) {
        // Try again with a different number.
        m_mappedCapacity = m_capacity;
        result = mmap(nullptr, m_mappedCapacity, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (result == MAP_FAILED)
            return;
    }

    ASSERT(startingSize <= m_mappedCapacity);
    if (mprotect(result, startingSize, PROT_READ | PROT_WRITE)) {
        munmap(result, m_mappedCapacity);
        return;
    }

    unsigned remainingPinnedRegisters = pinnedSizeRegisters.size() + 1;
    jscCallingConvention().m_calleeSaveRegisters.forEach([&] (Reg reg) {
        GPRReg gpr = reg.gpr();
        if (!remainingPinnedRegisters || RegisterSet::stackRegisters().get(reg))
            return;
        if (remainingPinnedRegisters == 1) {
            m_pinnedRegisters.baseMemoryPointer = gpr;
            remainingPinnedRegisters--;
        } else
            m_pinnedRegisters.sizeRegisters.append({ gpr, pinnedSizeRegisters[--remainingPinnedRegisters - 1] });
    });

    ASSERT(!remainingPinnedRegisters);
    m_memory = result;
}

} // namespace JSC

} // namespace Wasm

#endif // ENABLE(WEBASSEMBLY)
