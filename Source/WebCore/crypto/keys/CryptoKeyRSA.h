/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

#pragma once

#include "CryptoKey.h"
#include <wtf/Function.h>

#if ENABLE(SUBTLE_CRYPTO)

#if OS(DARWIN) && !PLATFORM(EFL) && !PLATFORM(GTK)
typedef struct _CCRSACryptor *CCRSACryptorRef;
typedef CCRSACryptorRef PlatformRSAKey;
#endif

#if PLATFORM(GTK) || PLATFORM(EFL)
typedef struct _PlatformRSAKeyGnuTLS PlatformRSAKeyGnuTLS;
typedef PlatformRSAKeyGnuTLS *PlatformRSAKey;
#endif

namespace WebCore {

class CryptoKeyDataRSAComponents;
class CryptoKeyPair;
class PromiseWrapper;
class ScriptExecutionContext;

struct JsonWebKey;

class RsaKeyAlgorithm : public KeyAlgorithm {
public:
    RsaKeyAlgorithm(const String& name, size_t modulusLength, Vector<uint8_t>&& publicExponent)
        : KeyAlgorithm(name)
        , m_modulusLength(modulusLength)
        , m_publicExponent(WTFMove(publicExponent))
    {
    }

    KeyAlgorithmClass keyAlgorithmClass() const override { return KeyAlgorithmClass::RSA; }

    size_t modulusLength() const { return m_modulusLength; }
    const Vector<uint8_t>& publicExponent() const { return m_publicExponent; }

private:
    size_t m_modulusLength;
    Vector<uint8_t> m_publicExponent;
};

class RsaHashedKeyAlgorithm final : public RsaKeyAlgorithm {
public:
    RsaHashedKeyAlgorithm(const String& name, size_t modulusLength, Vector<uint8_t>&& publicExponent, const String& hash)
        : RsaKeyAlgorithm(name, modulusLength, WTFMove(publicExponent))
        , m_hash(hash)
    {
    }

    KeyAlgorithmClass keyAlgorithmClass() const final { return KeyAlgorithmClass::HRSA; }

    const String& hash() const { return m_hash; }

private:
    String m_hash;
};

class CryptoKeyRSA final : public CryptoKey {
public:
    static Ref<CryptoKeyRSA> create(CryptoAlgorithmIdentifier identifier, CryptoAlgorithmIdentifier hash, bool hasHash, CryptoKeyType type, PlatformRSAKey platformKey, bool extractable, CryptoKeyUsageBitmap usage)
    {
        return adoptRef(*new CryptoKeyRSA(identifier, hash, hasHash, type, platformKey, extractable, usage));
    }
    static RefPtr<CryptoKeyRSA> create(CryptoAlgorithmIdentifier, CryptoAlgorithmIdentifier hash, bool hasHash, const CryptoKeyDataRSAComponents&, bool extractable, CryptoKeyUsageBitmap);
    virtual ~CryptoKeyRSA();

    bool isRestrictedToHash(CryptoAlgorithmIdentifier&) const;

    size_t keySizeInBits() const;

    using KeyPairCallback = WTF::Function<void(CryptoKeyPair&)>;
    using VoidCallback = WTF::Function<void()>;
    static void generatePair(CryptoAlgorithmIdentifier, CryptoAlgorithmIdentifier hash, bool hasHash, unsigned modulusLength, const Vector<uint8_t>& publicExponent, bool extractable, CryptoKeyUsageBitmap, KeyPairCallback&&, VoidCallback&& failureCallback, ScriptExecutionContext*);
    static RefPtr<CryptoKeyRSA> importJwk(CryptoAlgorithmIdentifier, Optional<CryptoAlgorithmIdentifier> hash, JsonWebKey&&, bool extractable, CryptoKeyUsageBitmap);

    PlatformRSAKey platformKey() const { return m_platformKey; }
    JsonWebKey exportJwk() const;

    CryptoAlgorithmIdentifier hashAlgorithmIdentifier() const { return m_hash; }

private:
    CryptoKeyRSA(CryptoAlgorithmIdentifier, CryptoAlgorithmIdentifier hash, bool hasHash, CryptoKeyType, PlatformRSAKey, bool extractable, CryptoKeyUsageBitmap);

    CryptoKeyClass keyClass() const final { return CryptoKeyClass::RSA; }

    std::unique_ptr<KeyAlgorithm> buildAlgorithm() const final;
    std::unique_ptr<CryptoKeyData> exportData() const final;

    PlatformRSAKey m_platformKey;

    bool m_restrictedToSpecificHash;
    CryptoAlgorithmIdentifier m_hash;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CRYPTO_KEY(CryptoKeyRSA, CryptoKeyClass::RSA)

SPECIALIZE_TYPE_TRAITS_KEY_ALGORITHM(RsaKeyAlgorithm, KeyAlgorithmClass::RSA)

SPECIALIZE_TYPE_TRAITS_KEY_ALGORITHM(RsaHashedKeyAlgorithm, KeyAlgorithmClass::HRSA)

#endif // ENABLE(SUBTLE_CRYPTO)
