/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2012 Michael Pruett <michael@68k.org>
 * Copyright (C) 2014, 2015, 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(INDEXED_DATABASE)
#include "IDBBindingUtilities.h"

#include "IDBIndexInfo.h"
#include "IDBKey.h"
#include "IDBKeyData.h"
#include "IDBKeyPath.h"
#include "IDBValue.h"
#include "IndexKey.h"
#include "JSDOMBinding.h"
#include "JSDOMConvert.h"
#include "JSDOMStringList.h"
#include "Logging.h"
#include "MessagePort.h"
#include "ScriptExecutionContext.h"
#include "SerializedScriptValue.h"
#include "SharedBuffer.h"
#include "ThreadSafeDataBuffer.h"
#include <runtime/ArrayBuffer.h>
#include <runtime/DateInstance.h>
#include <runtime/ObjectConstructor.h>

using namespace JSC;

namespace WebCore {

static bool get(ExecState& exec, JSValue object, const String& keyPathElement, JSValue& result)
{
    if (object.isString() && keyPathElement == "length") {
        result = jsNumber(object.toString(&exec)->length());
        return true;
    }
    if (!object.isObject())
        return false;
    Identifier identifier = Identifier::fromString(&exec.vm(), keyPathElement);
    if (!asObject(object)->hasProperty(&exec, identifier))
        return false;
    result = asObject(object)->get(&exec, identifier);
    return true;
}

static bool canSet(JSValue object, const String& keyPathElement)
{
    UNUSED_PARAM(keyPathElement);
    return object.isObject();
}

static bool set(ExecState& exec, JSValue& object, const String& keyPathElement, JSValue jsValue)
{
    if (!canSet(object, keyPathElement))
        return false;
    Identifier identifier = Identifier::fromString(&exec.vm(), keyPathElement);
    asObject(object)->putDirect(exec.vm(), identifier, jsValue);
    return true;
}

JSValue toJS(ExecState& state, JSGlobalObject& globalObject, IDBKey* key)
{
    if (!key) {
        // This must be undefined, not null.
        // Spec: http://dvcs.w3.org/hg/IndexedDB/raw-file/tip/Overview.html#idl-def-IDBKeyRange
        return jsUndefined();
    }

    VM& vm = state.vm();
    Locker<JSLock> locker(vm.apiLock());
    auto scope = DECLARE_THROW_SCOPE(vm);

    switch (key->type()) {
    case KeyType::Array: {
        auto& inArray = key->array();
        unsigned size = inArray.size();
        auto outArray = constructEmptyArray(&state, 0, &globalObject, size);
        RETURN_IF_EXCEPTION(scope, JSValue());
        for (size_t i = 0; i < size; ++i)
            outArray->putDirectIndex(&state, i, toJS(state, globalObject, inArray.at(i).get()));
        return outArray;
    }
    case KeyType::Binary: {
        auto* data = key->binary().data();
        if (!data) {
            ASSERT_NOT_REACHED();
            return jsNull();
        }

        auto arrayBuffer = ArrayBuffer::create(data->data(), data->size());
        Structure* structure = globalObject.arrayBufferStructure(arrayBuffer->sharingMode());
        if (!structure)
            return jsNull();

        return JSArrayBuffer::create(state.vm(), structure, WTFMove(arrayBuffer));
    }
    case KeyType::String:
        return jsStringWithCache(&state, key->string());
    case KeyType::Date:
        // FIXME: This should probably be toJS<IDLDate>(...) as per:
        // http://w3c.github.io/IndexedDB/#request-convert-a-key-to-a-value
        return toJS<IDLNullable<IDLDate>>(state, key->date());
    case KeyType::Number:
        return jsNumber(key->number());
    case KeyType::Min:
    case KeyType::Max:
    case KeyType::Invalid:
        ASSERT_NOT_REACHED();
        return jsUndefined();
    }

    ASSERT_NOT_REACHED();
    return jsUndefined();
}

static const size_t maximumDepth = 2000;

static RefPtr<IDBKey> createIDBKeyFromValue(ExecState& exec, JSValue value, Vector<JSArray*>& stack)
{
    if (value.isNumber() && !std::isnan(value.toNumber(&exec)))
        return IDBKey::createNumber(value.toNumber(&exec));

    if (value.isString())
        return IDBKey::createString(value.toString(&exec)->value(&exec));

    if (value.inherits(DateInstance::info()) && !std::isnan(valueToDate(&exec, value)))
        return IDBKey::createDate(valueToDate(&exec, value));

    if (value.isObject()) {
        JSObject* object = asObject(value);
        if (isJSArray(object) || object->inherits(JSArray::info())) {
            JSArray* array = asArray(object);
            size_t length = array->length();

            if (stack.contains(array))
                return nullptr;

            if (stack.size() >= maximumDepth)
                return nullptr;

            stack.append(array);

            Vector<RefPtr<IDBKey>> subkeys;
            for (size_t i = 0; i < length; i++) {
                JSValue item = array->getIndex(&exec, i);
                RefPtr<IDBKey> subkey = createIDBKeyFromValue(exec, item, stack);
                if (!subkey)
                    subkeys.append(IDBKey::createInvalid());
                else
                    subkeys.append(subkey);
            }

            stack.removeLast();
            return IDBKey::createArray(subkeys);
        }

        if (auto* arrayBuffer = jsDynamicCast<JSArrayBuffer*>(value))
            return IDBKey::createBinary(*arrayBuffer);

        if (auto* arrayBufferView = jsDynamicCast<JSArrayBufferView*>(value))
            return IDBKey::createBinary(*arrayBufferView);
    }
    return nullptr;
}

static Ref<IDBKey> createIDBKeyFromValue(ExecState& exec, JSValue value)
{
    Vector<JSArray*> stack;
    RefPtr<IDBKey> key = createIDBKeyFromValue(exec, value, stack);
    if (key)
        return *key;
    return IDBKey::createInvalid();
}

static JSValue getNthValueOnKeyPath(ExecState& exec, JSValue rootValue, const Vector<String>& keyPathElements, size_t index)
{
    JSValue currentValue(rootValue);
    ASSERT(index <= keyPathElements.size());
    for (size_t i = 0; i < index; i++) {
        JSValue parentValue(currentValue);
        if (!get(exec, parentValue, keyPathElements[i], currentValue))
            return jsUndefined();
    }
    return currentValue;
}

static RefPtr<IDBKey> internalCreateIDBKeyFromScriptValueAndKeyPath(ExecState& exec, const JSValue& value, const String& keyPath)
{
    Vector<String> keyPathElements;
    IDBKeyPathParseError error;
    IDBParseKeyPath(keyPath, keyPathElements, error);
    ASSERT(error == IDBKeyPathParseError::None);

    JSValue jsValue = value;
    jsValue = getNthValueOnKeyPath(exec, jsValue, keyPathElements, keyPathElements.size());
    if (jsValue.isUndefined())
        return nullptr;
    return createIDBKeyFromValue(exec, jsValue);
}

static JSValue ensureNthValueOnKeyPath(ExecState& exec, JSValue rootValue, const Vector<String>& keyPathElements, size_t index)
{
    JSValue currentValue(rootValue);

    ASSERT(index <= keyPathElements.size());
    for (size_t i = 0; i < index; i++) {
        JSValue parentValue(currentValue);
        const String& keyPathElement = keyPathElements[i];
        if (!get(exec, parentValue, keyPathElement, currentValue)) {
            JSObject* object = constructEmptyObject(&exec);
            if (!set(exec, parentValue, keyPathElement, JSValue(object)))
                return jsUndefined();
            currentValue = JSValue(object);
        }
    }

    return currentValue;
}

static bool canInjectNthValueOnKeyPath(ExecState& exec, JSValue rootValue, const Vector<String>& keyPathElements, size_t index)
{
    if (!rootValue.isObject())
        return false;

    JSValue currentValue(rootValue);

    ASSERT(index <= keyPathElements.size());
    for (size_t i = 0; i < index; ++i) {
        JSValue parentValue(currentValue);
        const String& keyPathElement = keyPathElements[i];
        if (!get(exec, parentValue, keyPathElement, currentValue))
            return canSet(parentValue, keyPathElement);
    }
    return true;
}

bool injectIDBKeyIntoScriptValue(ExecState& exec, const IDBKeyData& keyData, JSValue value, const IDBKeyPath& keyPath)
{
    LOG(IndexedDB, "injectIDBKeyIntoScriptValue");

    ASSERT(WTF::holds_alternative<String>(keyPath));

    Vector<String> keyPathElements;
    IDBKeyPathParseError error;
    IDBParseKeyPath(WTF::get<String>(keyPath), keyPathElements, error);
    ASSERT(error == IDBKeyPathParseError::None);

    if (keyPathElements.isEmpty())
        return false;

    JSValue parent = ensureNthValueOnKeyPath(exec, value, keyPathElements, keyPathElements.size() - 1);
    if (parent.isUndefined())
        return false;

    auto key = keyData.maybeCreateIDBKey();
    if (!key)
        return false;

    if (!set(exec, parent, keyPathElements.last(), toJS(exec, *exec.lexicalGlobalObject(), key.get())))
        return false;

    return true;
}


RefPtr<IDBKey> maybeCreateIDBKeyFromScriptValueAndKeyPath(ExecState& exec, const JSValue& value, const IDBKeyPath& keyPath)
{
    if (WTF::holds_alternative<Vector<String>>(keyPath)) {
        auto& array = WTF::get<Vector<String>>(keyPath);
        Vector<RefPtr<IDBKey>> result;
        result.reserveInitialCapacity(array.size());
        for (auto& string : array) {
            RefPtr<IDBKey> key = internalCreateIDBKeyFromScriptValueAndKeyPath(exec, value, string);
            if (!key)
                return nullptr;
            result.uncheckedAppend(WTFMove(key));
        }
        return IDBKey::createArray(WTFMove(result));
    }

    return internalCreateIDBKeyFromScriptValueAndKeyPath(exec, value, WTF::get<String>(keyPath));
}

bool canInjectIDBKeyIntoScriptValue(ExecState& exec, const JSValue& scriptValue, const IDBKeyPath& keyPath)
{
    LOG(StorageAPI, "canInjectIDBKeyIntoScriptValue");

    ASSERT(WTF::holds_alternative<String>(keyPath));
    Vector<String> keyPathElements;
    IDBKeyPathParseError error;
    IDBParseKeyPath(WTF::get<String>(keyPath), keyPathElements, error);
    ASSERT(error == IDBKeyPathParseError::None);

    if (!keyPathElements.size())
        return false;

    return canInjectNthValueOnKeyPath(exec, scriptValue, keyPathElements, keyPathElements.size() - 1);
}

static JSValue deserializeIDBValueToJSValue(ExecState& state, JSC::JSGlobalObject& globalObject, const IDBValue& value)
{
    // FIXME: I think it's peculiar to use undefined to mean "null data" and null to mean "empty data".
    // But I am not changing this at the moment because at least some callers are specifically checking isUndefined.

    if (!value.data().data())
        return jsUndefined();

    auto& data = *value.data().data();
    if (data.isEmpty())
        return jsNull();

    auto serializedValue = SerializedScriptValue::createFromWireBytes(Vector<uint8_t>(data));

    state.vm().apiLock().lock();
    Vector<RefPtr<MessagePort>> messagePorts;
    JSValue result = serializedValue->deserialize(state, &globalObject, messagePorts, value.blobURLs(), value.blobFilePaths(), NonThrowing);
    state.vm().apiLock().unlock();

    return result;
}

JSValue deserializeIDBValueToJSValue(ExecState& state, const IDBValue& value)
{
    return deserializeIDBValueToJSValue(state, *state.lexicalGlobalObject(), value);
}

JSC::JSValue toJS(JSC::ExecState* state, JSDOMGlobalObject* globalObject, const IDBValue& value)
{
    ASSERT(state);
    return deserializeIDBValueToJSValue(*state, *globalObject, value);
}

Ref<IDBKey> scriptValueToIDBKey(ExecState& exec, const JSValue& scriptValue)
{
    return createIDBKeyFromValue(exec, scriptValue);
}

JSC::JSValue idbKeyDataToScriptValue(JSC::ExecState& exec, const IDBKeyData& keyData)
{
    RefPtr<IDBKey> key = keyData.maybeCreateIDBKey();
    return toJS(exec, *exec.lexicalGlobalObject(), key.get());
}

JSC::JSValue toJS(JSC::ExecState* state, JSDOMGlobalObject* globalObject, const IDBKeyData& keyData)
{
    ASSERT(state);
    ASSERT(globalObject);

    return toJS(*state, *globalObject, keyData.maybeCreateIDBKey().get());
}

static Vector<IDBKeyData> createKeyPathArray(ExecState& exec, JSValue value, const IDBIndexInfo& info)
{
    auto visitor = WTF::makeVisitor([&](const String& string) -> Vector<IDBKeyData> {
        auto idbKey = internalCreateIDBKeyFromScriptValueAndKeyPath(exec, value, string);
        if (!idbKey)
            return { };

        Vector<IDBKeyData> keys;
        if (info.multiEntry() && idbKey->type() == IndexedDB::Array) {
            for (auto& key : idbKey->array())
                keys.append(key.get());
        } else
            keys.append(idbKey.get());
        return keys;
    }, [&](const Vector<String>& vector) -> Vector<IDBKeyData> {
        Vector<IDBKeyData> keys;
        for (auto& entry : vector) {
            auto key = internalCreateIDBKeyFromScriptValueAndKeyPath(exec, value, entry);
            if (!key)
                return { };
            keys.append(key.get());
        }
        return keys;
    });

    return WTF::visit(visitor, info.keyPath());
}

void generateIndexKeyForValue(ExecState& exec, const IDBIndexInfo& info, JSValue value, IndexKey& outKey)
{
    auto keyDatas = createKeyPathArray(exec, value, info);

    if (keyDatas.isEmpty())
        return;

    outKey = IndexKey(WTFMove(keyDatas));
}

JSValue toJS(ExecState& state, JSDOMGlobalObject& globalObject, const Optional<IDBKeyPath>& keyPath)
{
    if (!keyPath)
        return jsNull();

    auto visitor = WTF::makeVisitor([&](const String& string) -> JSValue {
        return toJS<IDLDOMString>(state, globalObject, string);
    }, [&](const Vector<String>& vector) -> JSValue {
        return toJS<IDLSequence<IDLDOMString>>(state, globalObject, vector);
    });
    return WTF::visit(visitor, keyPath.value());
}

} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
