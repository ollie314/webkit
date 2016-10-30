/*
 * Copyright (C) 2016 Apple Inc. All Rights Reserved.
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
#include "JSNode.h"

#if ENABLE(JIT)

#include "DOMJITAbstractHeapRepository.h"
#include "DOMJITCheckDOM.h"
#include "DOMJITHelpers.h"
#include "JSDOMWrapper.h"
#include "Node.h"
#include <domjit/DOMJITPatchpoint.h>
#include <domjit/DOMJITPatchpointParams.h>
#include <interpreter/FrameTracers.h>

using namespace JSC;

namespace WebCore {

enum class IsContainerGuardRequirement { Required, NotRequired };

template<typename WrappedNode>
static Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> createCallDOMGetterForOffsetAccess(ptrdiff_t offset, IsContainerGuardRequirement isContainerGuardRequirement)
{
    Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> patchpoint = JSC::DOMJIT::CallDOMGetterPatchpoint::create();
    patchpoint->numGPScratchRegisters = 1;
    patchpoint->setGenerator([=](CCallHelpers& jit, JSC::DOMJIT::PatchpointParams& params) {
        JSValueRegs result = params[0].jsValueRegs();
        GPRReg node = params[1].gpr();
        GPRReg globalObject = params[2].gpr();
        GPRReg scratch = params.gpScratch(0);
        JSValue globalObjectValue = params[2].value();

        CCallHelpers::JumpList nullCases;
        // Load a wrapped object. "node" should be already type checked by CheckDOM.
        jit.loadPtr(CCallHelpers::Address(node, JSNode::offsetOfWrapped()), scratch);

        if (isContainerGuardRequirement == IsContainerGuardRequirement::Required)
            nullCases.append(jit.branchTest32(CCallHelpers::Zero, CCallHelpers::Address(scratch, Node::nodeFlagsMemoryOffset()), CCallHelpers::TrustedImm32(Node::flagIsContainer())));

        jit.loadPtr(CCallHelpers::Address(scratch, offset), scratch);
        nullCases.append(jit.branchTestPtr(CCallHelpers::Zero, scratch));

        DOMJIT::toWrapper<WrappedNode>(jit, params, scratch, globalObject, result, DOMJIT::toWrapperSlow<WrappedNode>, globalObjectValue);
        CCallHelpers::Jump done = jit.jump();

        nullCases.link(&jit);
        jit.moveValue(jsNull(), result);
        done.link(&jit);
        return CCallHelpers::JumpList();
    });
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodeFirstChildDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodeFirstChildDOMJIT::callDOMGetter()
{
    const auto& heap = DOMJIT::AbstractHeapRepository::shared();
    auto patchpoint = createCallDOMGetterForOffsetAccess<Node>(CAST_OFFSET(Node*, ContainerNode*) + ContainerNode::firstChildMemoryOffset(), IsContainerGuardRequirement::Required);
    patchpoint->effect = JSC::DOMJIT::Effect::forDef(heap.Node_firstChild);
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodeLastChildDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodeLastChildDOMJIT::callDOMGetter()
{
    const auto& heap = DOMJIT::AbstractHeapRepository::shared();
    auto patchpoint = createCallDOMGetterForOffsetAccess<Node>(CAST_OFFSET(Node*, ContainerNode*) + ContainerNode::lastChildMemoryOffset(), IsContainerGuardRequirement::Required);
    patchpoint->effect = JSC::DOMJIT::Effect::forDef(heap.Node_lastChild);
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodeNextSiblingDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodeNextSiblingDOMJIT::callDOMGetter()
{
    const auto& heap = DOMJIT::AbstractHeapRepository::shared();
    auto patchpoint = createCallDOMGetterForOffsetAccess<Node>(Node::nextSiblingMemoryOffset(), IsContainerGuardRequirement::NotRequired);
    patchpoint->effect = JSC::DOMJIT::Effect::forDef(heap.Node_nextSibling);
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodePreviousSiblingDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodePreviousSiblingDOMJIT::callDOMGetter()
{
    const auto& heap = DOMJIT::AbstractHeapRepository::shared();
    auto patchpoint = createCallDOMGetterForOffsetAccess<Node>(Node::previousSiblingMemoryOffset(), IsContainerGuardRequirement::NotRequired);
    patchpoint->effect = JSC::DOMJIT::Effect::forDef(heap.Node_previousSibling);
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodeParentNodeDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodeParentNodeDOMJIT::callDOMGetter()
{
    const auto& heap = DOMJIT::AbstractHeapRepository::shared();
    auto patchpoint = createCallDOMGetterForOffsetAccess<ContainerNode>(Node::parentNodeMemoryOffset(), IsContainerGuardRequirement::NotRequired);
    patchpoint->effect = JSC::DOMJIT::Effect::forDef(heap.Node_parentNode);
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodeNodeTypeDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodeNodeTypeDOMJIT::callDOMGetter()
{
    Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> patchpoint = JSC::DOMJIT::CallDOMGetterPatchpoint::create();
    patchpoint->effect = JSC::DOMJIT::Effect::forPure();
    patchpoint->requireGlobalObject = false;
    patchpoint->setGenerator([=](CCallHelpers& jit, JSC::DOMJIT::PatchpointParams& params) {
        JSValueRegs result = params[0].jsValueRegs();
        GPRReg node = params[1].gpr();
        jit.load8(CCallHelpers::Address(node, JSC::JSCell::typeInfoTypeOffset()), result.payloadGPR());
        jit.and32(CCallHelpers::TrustedImm32(JSNodeTypeMask), result.payloadGPR());
        jit.boxInt32(result.payloadGPR(), result);
        return CCallHelpers::JumpList();
    });
    return patchpoint;
}

Ref<JSC::DOMJIT::Patchpoint> NodeOwnerDocumentDOMJIT::checkDOM()
{
    return DOMJIT::checkDOM<Node>();
}

Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> NodeOwnerDocumentDOMJIT::callDOMGetter()
{
    const auto& heap = DOMJIT::AbstractHeapRepository::shared();
    Ref<JSC::DOMJIT::CallDOMGetterPatchpoint> patchpoint = JSC::DOMJIT::CallDOMGetterPatchpoint::create();
    patchpoint->numGPScratchRegisters = 1;
    patchpoint->setGenerator([=](CCallHelpers& jit, JSC::DOMJIT::PatchpointParams& params) {
        JSValueRegs result = params[0].jsValueRegs();
        GPRReg node = params[1].gpr();
        GPRReg globalObject = params[2].gpr();
        JSValue globalObjectValue = params[2].value();
        GPRReg scratch = params.gpScratch(0);

        // If the given node is a Document, Node#ownerDocument returns null.
        auto notDocument = DOMJIT::branchIfNotDocumentWrapper(jit, node);
        jit.moveValue(jsNull(), result);
        auto done = jit.jump();

        notDocument.link(&jit);
        jit.loadPtr(CCallHelpers::Address(node, JSNode::offsetOfWrapped()), scratch);
        DOMJIT::loadDocument(jit, scratch, scratch);
        DOMJIT::toWrapper<Document>(jit, params, scratch, globalObject, result, DOMJIT::toWrapperSlow<Document>, globalObjectValue);
        done.link(&jit);
        return CCallHelpers::JumpList();
    });
    patchpoint->effect = JSC::DOMJIT::Effect::forDef(heap.Node_ownerDocument);
    return patchpoint;
}

}

#endif
