/*
 * Copyright (C) 2013, 2016 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "JSCryptoAlgorithmDictionary.h"

#if ENABLE(SUBTLE_CRYPTO)

#include "CryptoAlgorithmAesCbcParamsDeprecated.h"
#include "CryptoAlgorithmAesKeyGenParamsDeprecated.h"
#include "CryptoAlgorithmHmacKeyParamsDeprecated.h"
#include "CryptoAlgorithmHmacParamsDeprecated.h"
#include "CryptoAlgorithmRegistry.h"
#include "CryptoAlgorithmRsaKeyGenParamsDeprecated.h"
#include "CryptoAlgorithmRsaKeyParamsWithHashDeprecated.h"
#include "CryptoAlgorithmRsaOaepParamsDeprecated.h"
#include "CryptoAlgorithmRsaSsaParamsDeprecated.h"
#include "ExceptionCode.h"
#include "JSCryptoOperationData.h"
#include "JSDOMBinding.h"
#include "JSDOMConvert.h"
#include "JSDictionary.h"

using namespace JSC;

namespace WebCore {

enum class HashRequirement {
    Optional,
    Required, 
};

bool JSCryptoAlgorithmDictionary::getAlgorithmIdentifier(ExecState* exec, JSValue value, CryptoAlgorithmIdentifier& algorithmIdentifier)
{
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    // typedef (Algorithm or DOMString) AlgorithmIdentifier;

    String algorithmName;

    if (value.isString())
        algorithmName = value.toString(exec)->value(exec);
    else if (value.isObject()) {
        if (value.getObject()->inherits(StringObject::info()))
            algorithmName = asString(asStringObject(value)->internalValue())->value(exec);
        else {
            // FIXME: This doesn't perform some checks mandated by WebIDL for dictionaries:
            // - null and undefined input should be treated as if all elements in the dictionary were undefined;
            // - undefined elements should be treated as having a default value, or as not present if there isn't such;
            // - RegExp and Date objects cannot be converted to dictionaries.
            //
            // This is partially because we don't implement it elsewhere in WebCore yet, and partially because
            // WebCrypto doesn't yet clearly specify what to do with non-present values in most cases anyway.

            JSDictionary dictionary(exec, value.getObject());
            dictionary.get("name", algorithmName);
        }
    }

    RETURN_IF_EXCEPTION(scope, false);

    if (!algorithmName.containsOnlyASCII()) {
        throwSyntaxError(exec, scope);
        return false;
    }

    if (!CryptoAlgorithmRegistry::singleton().getIdentifierForName(algorithmName, algorithmIdentifier)) {
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return false;
    }

    return true;
}

static JSValue getProperty(ExecState* exec, JSObject* object, const char* name)
{
    return object->get(exec, Identifier::fromString(exec, name));
}

static bool getHashAlgorithm(JSDictionary& dictionary, CryptoAlgorithmIdentifier& result, HashRequirement isRequired)
{
    // FXIME: Teach JSDictionary how to return JSValues, and use that to get hash element value.

    ExecState* exec = dictionary.execState();
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSObject* object = dictionary.initializerObject();

    Identifier identifier = Identifier::fromString(exec, "hash");

    JSValue hash = getProperty(exec, object, "hash");
    RETURN_IF_EXCEPTION(scope, false);

    if (hash.isUndefinedOrNull()) {
        if (isRequired == HashRequirement::Required)
            setDOMException(exec, NOT_SUPPORTED_ERR);
        return false;
    }

    return JSCryptoAlgorithmDictionary::getAlgorithmIdentifier(exec, hash, result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createAesCbcParams(ExecState* exec, JSValue value)
{
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(exec, scope);
        return nullptr;
    }

    JSValue iv = getProperty(exec, value.getObject(), "iv");
    RETURN_IF_EXCEPTION(scope, nullptr);

    auto result = adoptRef(*new CryptoAlgorithmAesCbcParamsDeprecated);

    CryptoOperationData ivData;
    auto success = cryptoOperationDataFromJSValue(exec, iv, ivData);
    ASSERT(scope.exception() || success);
    if (!success)
        return nullptr;

    if (ivData.second != 16) {
        throwException(exec, scope, createError(exec, "AES-CBC initialization data must be 16 bytes"));
        return nullptr;
    }

    memcpy(result->iv.data(), ivData.first, ivData.second);

    return WTFMove(result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createAesKeyGenParams(ExecState& state, JSValue value)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(&state, scope);
        return nullptr;
    }

    auto result = adoptRef(*new CryptoAlgorithmAesKeyGenParamsDeprecated);

    JSValue lengthValue = getProperty(&state, value.getObject(), "length");
    RETURN_IF_EXCEPTION(scope, nullptr);

    result->length = convert<IDLUnsignedShort>(state, lengthValue, EnforceRange);

    return WTFMove(result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createHmacParams(ExecState& state, JSValue value)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(&state, scope);
        return nullptr;
    }

    JSDictionary jsDictionary(&state, value.getObject());
    auto result = adoptRef(*new CryptoAlgorithmHmacParamsDeprecated);

    auto success = getHashAlgorithm(jsDictionary, result->hash, HashRequirement::Required);
    ASSERT_UNUSED(scope, scope.exception() || success);
    if (!success)
        return nullptr;

    return WTFMove(result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createHmacKeyParams(ExecState& state, JSValue value)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(&state, scope);
        return nullptr;
    }

    JSDictionary jsDictionary(&state, value.getObject());
    auto result = adoptRef(*new CryptoAlgorithmHmacKeyParamsDeprecated);

    auto success = getHashAlgorithm(jsDictionary, result->hash, HashRequirement::Required);
    ASSERT(scope.exception() || success);
    if (!success)
        return nullptr;

    result->hasLength = jsDictionary.get("length", result->length);
    RETURN_IF_EXCEPTION(scope, nullptr);

    return WTFMove(result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createRsaKeyGenParams(ExecState& state, JSValue value)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(&state, scope);
        return nullptr;
    }

    JSDictionary jsDictionary(&state, value.getObject());
    auto result = adoptRef(*new CryptoAlgorithmRsaKeyGenParamsDeprecated);

    JSValue modulusLengthValue = getProperty(&state, value.getObject(), "modulusLength");
    RETURN_IF_EXCEPTION(scope, nullptr);

    // FIXME: Why no EnforceRange? Filed as <https://www.w3.org/Bugs/Public/show_bug.cgi?id=23779>.
    result->modulusLength = convert<IDLUnsignedLong>(state, modulusLengthValue, NormalConversion);
    RETURN_IF_EXCEPTION(scope, nullptr);

    JSValue publicExponentValue = getProperty(&state, value.getObject(), "publicExponent");
    RETURN_IF_EXCEPTION(scope, nullptr);

    RefPtr<Uint8Array> publicExponentArray = toUint8Array(publicExponentValue);
    if (!publicExponentArray) {
        throwTypeError(&state, scope, ASCIILiteral("Expected a Uint8Array in publicExponent"));
        return nullptr;
    }
    result->publicExponent.append(publicExponentArray->data(), publicExponentArray->byteLength());

    result->hasHash = getHashAlgorithm(jsDictionary, result->hash, HashRequirement::Optional); 

    return WTFMove(result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createRsaKeyParamsWithHash(ExecState&, JSValue)
{
    // WebCrypto RSA algorithms currently do not take any parameters to importKey.
    return adoptRef(*new CryptoAlgorithmRsaKeyParamsWithHashDeprecated);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createRsaOaepParams(ExecState* exec, JSValue value)
{
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(exec, scope);
        return nullptr;
    }

    JSDictionary jsDictionary(exec, value.getObject());
    auto result = adoptRef(*new CryptoAlgorithmRsaOaepParamsDeprecated);

    auto success = getHashAlgorithm(jsDictionary, result->hash, HashRequirement::Required);
    ASSERT(scope.exception() || success);
    if (!success)
        return nullptr;

    JSValue labelValue = getProperty(exec, value.getObject(), "label");
    RETURN_IF_EXCEPTION(scope, nullptr);

    result->hasLabel = !labelValue.isUndefinedOrNull();
    if (!result->hasLabel)
        return WTFMove(result);

    CryptoOperationData labelData;
    success = cryptoOperationDataFromJSValue(exec, labelValue, labelData);
    ASSERT(scope.exception() || success);
    if (!success)
        return nullptr;

    result->label.append(labelData.first, labelData.second);

    return WTFMove(result);
}

static RefPtr<CryptoAlgorithmParametersDeprecated> createRsaSsaParams(ExecState& state, JSValue value)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) {
        throwTypeError(&state, scope);
        return nullptr;
    }

    JSDictionary jsDictionary(&state, value.getObject());
    auto result = adoptRef(*new CryptoAlgorithmRsaSsaParamsDeprecated);

    auto success = getHashAlgorithm(jsDictionary, result->hash, HashRequirement::Required);
    ASSERT(scope.exception() || success);
    if (!success)
        return nullptr;

    return WTFMove(result);
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForEncrypt(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue value)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::RSA_OAEP:
        return createRsaOaepParams(exec, value);
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::AES_CBC:
        return createAesCbcParams(exec, value);
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::AES_KW:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::HMAC:
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForDecrypt(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue value)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::RSA_OAEP:
        return createRsaOaepParams(exec, value);
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::AES_CBC:
        return createAesCbcParams(exec, value);
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::AES_KW:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::HMAC:
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForSign(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue value)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
        return createRsaSsaParams(*exec, value);
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::HMAC:
        return createHmacParams(*exec, value);
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForVerify(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue value)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
        return createRsaSsaParams(*exec, value);
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::HMAC:
        return createHmacParams(*exec, value);
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForDigest(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
    case CryptoAlgorithmIdentifier::HMAC:
    case CryptoAlgorithmIdentifier::DH:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForGenerateKey(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue value)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
        return createRsaKeyGenParams(*exec, value);
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
        return createAesKeyGenParams(*exec, value);
    case CryptoAlgorithmIdentifier::HMAC:
        return createHmacKeyParams(*exec, value);
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForDeriveKey(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
    case CryptoAlgorithmIdentifier::HMAC:
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForDeriveBits(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
    case CryptoAlgorithmIdentifier::HMAC:
    case CryptoAlgorithmIdentifier::DH:
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForImportKey(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue value)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
        return createRsaKeyParamsWithHash(*exec, value);
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::HMAC:
        return createHmacParams(*exec, value);
    case CryptoAlgorithmIdentifier::DH:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<CryptoAlgorithmParametersDeprecated> JSCryptoAlgorithmDictionary::createParametersForExportKey(ExecState* exec, CryptoAlgorithmIdentifier algorithm, JSValue)
{
    switch (algorithm) {
    case CryptoAlgorithmIdentifier::RSAES_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSASSA_PKCS1_v1_5:
    case CryptoAlgorithmIdentifier::RSA_PSS:
    case CryptoAlgorithmIdentifier::RSA_OAEP:
    case CryptoAlgorithmIdentifier::ECDSA:
    case CryptoAlgorithmIdentifier::ECDH:
    case CryptoAlgorithmIdentifier::AES_CTR:
    case CryptoAlgorithmIdentifier::AES_CBC:
    case CryptoAlgorithmIdentifier::AES_CMAC:
    case CryptoAlgorithmIdentifier::AES_GCM:
    case CryptoAlgorithmIdentifier::AES_CFB:
    case CryptoAlgorithmIdentifier::AES_KW:
    case CryptoAlgorithmIdentifier::HMAC:
    case CryptoAlgorithmIdentifier::DH:
        return adoptRef(*new CryptoAlgorithmParametersDeprecated);
    case CryptoAlgorithmIdentifier::SHA_1:
    case CryptoAlgorithmIdentifier::SHA_224:
    case CryptoAlgorithmIdentifier::SHA_256:
    case CryptoAlgorithmIdentifier::SHA_384:
    case CryptoAlgorithmIdentifier::SHA_512:
    case CryptoAlgorithmIdentifier::CONCAT:
    case CryptoAlgorithmIdentifier::HKDF_CTR:
    case CryptoAlgorithmIdentifier::PBKDF2:
        setDOMException(exec, NOT_SUPPORTED_ERR);
        return nullptr;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return nullptr;
}

}

#endif // ENABLE(SUBTLE_CRYPTO)
