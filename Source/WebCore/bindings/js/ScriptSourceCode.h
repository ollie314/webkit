/*
 * Copyright (c) 2008, Google Inc. All rights reserved.
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

#pragma once

#include "CachedResourceHandle.h"
#include "CachedScript.h"
#include "CachedScriptSourceProvider.h"
#include "URL.h"
#include <parser/SourceProvider.h>
#include <wtf/text/TextPosition.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class ScriptSourceCode {
public:
    ScriptSourceCode(const String& source, const URL& url = URL(), const TextPosition& startPosition = TextPosition::minimumPosition(), JSC::SourceProviderSourceType sourceType = JSC::SourceProviderSourceType::Program)
        : m_provider(JSC::StringSourceProvider::create(source, url.isNull() ? String() : url.string(), startPosition, sourceType))
        , m_code(m_provider, startPosition.m_line.oneBasedInt(), startPosition.m_column.oneBasedInt())
        , m_url(url)
    {
    }

    explicit ScriptSourceCode(CachedScript* cachedScript, JSC::SourceProviderSourceType sourceType)
        : m_provider(CachedScriptSourceProvider::create(cachedScript, sourceType))
        , m_code(m_provider)
        , m_cachedScript(cachedScript)
    {
    }

    bool isEmpty() const { return m_code.length() == 0; }

    const JSC::SourceCode& jsSourceCode() const { return m_code; }

    StringView source() const { return m_provider->source(); }

    int startLine() const { return m_code.firstLine(); }

    CachedScript* cachedScript() const { return m_cachedScript.get(); }

    const URL& url() const { return m_url; }
    
private:
    RefPtr<JSC::SourceProvider> m_provider;
    JSC::SourceCode m_code;
    CachedResourceHandle<CachedScript> m_cachedScript;
    URL m_url;
};

} // namespace WebCore
