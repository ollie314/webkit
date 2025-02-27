/*
 * Copyright (C) 2016 Apple, Inc. All Rights Reserved.
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

#pragma once

#include "CachedModuleScript.h"
#include "CachedModuleScriptClient.h"
#include "LoadableScript.h"
#include <wtf/TypeCasts.h>

namespace WebCore {

class LoadableModuleScript final : public LoadableScript, private CachedModuleScriptClient {
public:
    virtual ~LoadableModuleScript();

    static Ref<LoadableModuleScript> create(CachedModuleScript&);

    bool isLoaded() const final;
    Optional<Error> error() const final;
    bool wasCanceled() const final;

    CachedModuleScript& moduleScript() { return m_moduleScript.get(); }
    bool isModuleScript() const final { return true; }

    void execute(ScriptElement&) final;

    void setError(Error&&);

private:
    LoadableModuleScript(CachedModuleScript&);

    void notifyFinished(CachedModuleScript&) final;

    Ref<CachedModuleScript> m_moduleScript;
};

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::LoadableModuleScript)
    static bool isType(const WebCore::LoadableScript& script) { return script.isModuleScript(); }
SPECIALIZE_TYPE_TRAITS_END()
