/*
 * Copyright (C) 2009, 2010, 2013, 2015-2016 Apple Inc. All rights reserved.
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

#include "BatchedTransitionOptimizer.h"
#include "CodeBlock.h"
#include "Debugger.h"
#include "JIT.h"
#include "JSCInlines.h"
#include "LLIntEntrypoint.h"
#include "Parser.h"
#include "ProgramCodeBlock.h"
#include "TypeProfiler.h"
#include "VMInlines.h"
#include <wtf/CommaPrinter.h>

namespace JSC {

const ClassInfo ProgramExecutable::s_info = { "ProgramExecutable", &ScriptExecutable::s_info, 0, CREATE_METHOD_TABLE(ProgramExecutable) };

ProgramExecutable::ProgramExecutable(ExecState* exec, const SourceCode& source)
    : ScriptExecutable(exec->vm().programExecutableStructure.get(), exec->vm(), source, false, DerivedContextType::None, false, EvalContextType::None, NoIntrinsic)
{
    ASSERT(source.provider()->sourceType() == SourceProviderSourceType::Program);
    m_typeProfilingStartOffset = 0;
    m_typeProfilingEndOffset = source.length() - 1;
    if (exec->vm().typeProfiler() || exec->vm().controlFlowProfiler())
        exec->vm().functionHasExecutedCache()->insertUnexecutedRange(sourceID(), m_typeProfilingStartOffset, m_typeProfilingEndOffset);
}

void ProgramExecutable::destroy(JSCell* cell)
{
    static_cast<ProgramExecutable*>(cell)->ProgramExecutable::~ProgramExecutable();
}

JSObject* ProgramExecutable::checkSyntax(ExecState* exec)
{
    ParserError error;
    VM* vm = &exec->vm();
    JSGlobalObject* lexicalGlobalObject = exec->lexicalGlobalObject();
    std::unique_ptr<ProgramNode> programNode = parse<ProgramNode>(
        vm, m_source, Identifier(), JSParserBuiltinMode::NotBuiltin,
        JSParserStrictMode::NotStrict, JSParserScriptMode::Classic, SourceParseMode::ProgramMode, SuperBinding::NotNeeded, error);
    if (programNode)
        return 0;
    ASSERT(error.isValid());
    return error.toErrorObject(lexicalGlobalObject, m_source);
}

JSObject* ProgramExecutable::initializeGlobalProperties(VM& vm, CallFrame* callFrame, JSScope* scope)
{
    RELEASE_ASSERT(scope);
    JSGlobalObject* globalObject = scope->globalObject();
    RELEASE_ASSERT(globalObject);
    ASSERT(&globalObject->vm() == &vm);

    JSObject* exception = 0;
    UnlinkedProgramCodeBlock* unlinkedCodeBlock = globalObject->createProgramCodeBlock(callFrame, this, &exception);
    if (exception)
        return exception;

    JSGlobalLexicalEnvironment* globalLexicalEnvironment = globalObject->globalLexicalEnvironment();
    const VariableEnvironment& variableDeclarations = unlinkedCodeBlock->variableDeclarations();
    const VariableEnvironment& lexicalDeclarations = unlinkedCodeBlock->lexicalDeclarations();
    // The ES6 spec says that no vars/global properties/let/const can be duplicated in the global scope.
    // This carried out section 15.1.8 of the ES6 spec: http://www.ecma-international.org/ecma-262/6.0/index.html#sec-globaldeclarationinstantiation
    {
        ExecState* exec = globalObject->globalExec();
        // Check for intersection of "var" and "let"/"const"/"class"
        for (auto& entry : lexicalDeclarations) {
            if (variableDeclarations.contains(entry.key))
                return createSyntaxError(exec, makeString("Can't create duplicate variable: '", String(entry.key.get()), "'"));
        }

        // Check if any new "let"/"const"/"class" will shadow any pre-existing global property names, or "var"/"let"/"const" variables.
        // It's an error to introduce a shadow.
        for (auto& entry : lexicalDeclarations) {
            if (globalObject->hasProperty(exec, entry.key.get())) {
                // The ES6 spec says that just RestrictedGlobalProperty can't be shadowed
                // This carried out section 8.1.1.4.14 of the ES6 spec: http://www.ecma-international.org/ecma-262/6.0/index.html#sec-hasrestrictedglobalproperty
                PropertyDescriptor descriptor;
                globalObject->getOwnPropertyDescriptor(exec, entry.key.get(), descriptor);
                
                if (descriptor.value() != jsUndefined() && !descriptor.configurable())
                    return createSyntaxError(exec, makeString("Can't create duplicate variable that shadows a global property: '", String(entry.key.get()), "'"));
            }
                
            if (globalLexicalEnvironment->hasProperty(exec, entry.key.get())) {
                if (UNLIKELY(entry.value.isConst() && !vm.globalConstRedeclarationShouldThrow() && !isStrictMode())) {
                    // We only allow "const" duplicate declarations under this setting.
                    // For example, we don't "let" variables to be overridden by "const" variables.
                    if (globalLexicalEnvironment->isConstVariable(entry.key.get()))
                        continue;
                }
                return createSyntaxError(exec, makeString("Can't create duplicate variable: '", String(entry.key.get()), "'"));
            }
        }

        // Check if any new "var"s will shadow any previous "let"/"const"/"class" names.
        // It's an error to introduce a shadow.
        if (!globalLexicalEnvironment->isEmpty()) {
            for (auto& entry : variableDeclarations) {
                if (globalLexicalEnvironment->hasProperty(exec, entry.key.get()))
                    return createSyntaxError(exec, makeString("Can't create duplicate variable: '", String(entry.key.get()), "'"));
            }
        }
    }


    m_unlinkedProgramCodeBlock.set(vm, this, unlinkedCodeBlock);

    BatchedTransitionOptimizer optimizer(vm, globalObject);

    for (size_t i = 0, numberOfFunctions = unlinkedCodeBlock->numberOfFunctionDecls(); i < numberOfFunctions; ++i) {
        UnlinkedFunctionExecutable* unlinkedFunctionExecutable = unlinkedCodeBlock->functionDecl(i);
        ASSERT(!unlinkedFunctionExecutable->name().isEmpty());
        globalObject->addFunction(callFrame, unlinkedFunctionExecutable->name());
        if (vm.typeProfiler() || vm.controlFlowProfiler()) {
            vm.functionHasExecutedCache()->insertUnexecutedRange(sourceID(), 
                unlinkedFunctionExecutable->typeProfilingStartOffset(), 
                unlinkedFunctionExecutable->typeProfilingEndOffset());
        }
    }

    for (auto& entry : variableDeclarations) {
        ASSERT(entry.value.isVar());
        globalObject->addVar(callFrame, Identifier::fromUid(&vm, entry.key.get()));
    }

    {
        JSGlobalLexicalEnvironment* globalLexicalEnvironment = jsCast<JSGlobalLexicalEnvironment*>(globalObject->globalScope());
        SymbolTable* symbolTable = globalLexicalEnvironment->symbolTable();
        ConcurrentJSLocker locker(symbolTable->m_lock);
        for (auto& entry : lexicalDeclarations) {
            if (UNLIKELY(entry.value.isConst() && !vm.globalConstRedeclarationShouldThrow() && !isStrictMode())) {
                if (symbolTable->contains(locker, entry.key.get()))
                    continue;
            }
            ScopeOffset offset = symbolTable->takeNextScopeOffset(locker);
            SymbolTableEntry newEntry(VarOffset(offset), entry.value.isConst() ? ReadOnly : 0);
            newEntry.prepareToWatch();
            symbolTable->add(locker, entry.key.get(), newEntry);
            
            ScopeOffset offsetForAssert = globalLexicalEnvironment->addVariables(1, jsTDZValue());
            RELEASE_ASSERT(offsetForAssert == offset);
        }
    }
    return 0;
}

void ProgramExecutable::visitChildren(JSCell* cell, SlotVisitor& visitor)
{
    ProgramExecutable* thisObject = jsCast<ProgramExecutable*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    ScriptExecutable::visitChildren(thisObject, visitor);
    visitor.append(&thisObject->m_unlinkedProgramCodeBlock);
    if (thisObject->m_programCodeBlock)
        thisObject->m_programCodeBlock->visitWeakly(visitor);
}

} // namespace JSC
