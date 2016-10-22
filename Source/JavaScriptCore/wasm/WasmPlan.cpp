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
#include "WasmPlan.h"

#if ENABLE(WEBASSEMBLY)

#include "B3Compilation.h"
#include "WasmB3IRGenerator.h"
#include "WasmCallingConvention.h"
#include "WasmModuleParser.h"
#include <wtf/DataLog.h>

namespace JSC { namespace Wasm {

static const bool verbose = false;

Plan::Plan(VM& vm, Vector<uint8_t> source)
{
    if (verbose)
        dataLogLn("Starting plan.");
    ModuleParser moduleParser(source);
    if (!moduleParser.parse()) {
        dataLogLn("Parsing module failed.");
        return;
    }

    if (verbose)
        dataLogLn("Parsed module.");

    for (const FunctionInformation& info : moduleParser.functionInformation()) {
        if (verbose)
            dataLogLn("Processing funcion starting at: ", info.start, " and ending at: ", info.end);
        result.append(parseAndCompile(vm, source, moduleParser.memory().get(), info, moduleParser.functionInformation()));
    }

    // Patch the call sites for each function.
    for (std::unique_ptr<FunctionCompilation>& functionPtr : result) {
        FunctionCompilation* function = functionPtr.get();
        for (auto& call : function->unlinkedCalls)
            MacroAssembler::repatchCall(call.callLocation, CodeLocationLabel(result[call.functionIndex]->code->code()));
    }

    memory = WTFMove(moduleParser.memory());
}

} } // namespace JSC::Wasm

#endif // ENABLE(WEBASSEMBLY)
