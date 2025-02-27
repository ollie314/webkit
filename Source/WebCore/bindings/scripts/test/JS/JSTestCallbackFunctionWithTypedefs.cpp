/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestCallbackFunctionWithTypedefs.h"

#include "JSDOMConvert.h"
#include "ScriptExecutionContext.h"
#include <runtime/JSArray.h>
#include <runtime/JSLock.h>

using namespace JSC;

namespace WebCore {

JSTestCallbackFunctionWithTypedefs::JSTestCallbackFunctionWithTypedefs(JSObject* callback, JSDOMGlobalObject* globalObject)
    : TestCallbackFunctionWithTypedefs()
    , ActiveDOMCallback(globalObject->scriptExecutionContext())
    , m_data(new JSCallbackDataStrong(callback, this))
{
}

JSTestCallbackFunctionWithTypedefs::~JSTestCallbackFunctionWithTypedefs()
{
    ScriptExecutionContext* context = scriptExecutionContext();
    // When the context is destroyed, all tasks with a reference to a callback
    // should be deleted. So if the context is 0, we are on the context thread.
    if (!context || context->isContextThread())
        delete m_data;
    else
        context->postTask(DeleteCallbackDataTask(m_data));
#ifndef NDEBUG
    m_data = nullptr;
#endif
}

bool JSTestCallbackFunctionWithTypedefs::handleEvent(Vector<int32_t> sequenceArg, int32_t longArg)
{
    if (!canInvokeCallback())
        return true;

    Ref<JSTestCallbackFunctionWithTypedefs> protectedThis(*this);

    JSLockHolder lock(m_data->globalObject()->vm());

    ExecState* state = m_data->globalObject()->globalExec();
    MarkedArgumentBuffer args;
    args.append(toJS<IDLSequence<IDLNullable<IDLLong>>>(*state, *m_data->globalObject(), sequenceArg));
    args.append(toJS<IDLLong>(longArg));

    NakedPtr<JSC::Exception> returnedException;
    m_data->invokeCallback(args, JSCallbackData::CallbackType::Function, Identifier(), returnedException);
    if (returnedException)
        reportException(state, returnedException);
    return !returnedException;
}

JSC::JSValue toJS(JSC::ExecState*, JSDOMGlobalObject*, TestCallbackFunctionWithTypedefs& impl)
{
    if (!static_cast<JSTestCallbackFunctionWithTypedefs&>(impl).callbackData())
        return jsNull();

    return static_cast<JSTestCallbackFunctionWithTypedefs&>(impl).callbackData()->callback();

}

} // namespace WebCore
