/*
 * Copyright (C) 2015, 2016 Apple Inc. All rights reserved.
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

#if ENABLE(APPLE_PAY)

#include "MessageReceiver.h"
#include <WebCore/PaymentCoordinatorClient.h>
#include <WebCore/PaymentHeaders.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>

namespace IPC {
class DataReference;
}

namespace WebCore {
class PaymentCoordinator;
class PaymentContact;
}

namespace WebKit {

class WebPage;

class WebPaymentCoordinator final : public WebCore::PaymentCoordinatorClient, private IPC::MessageReceiver {
public:
    explicit WebPaymentCoordinator(WebPage&);
    ~WebPaymentCoordinator();

private:
    // WebCore::PaymentCoordinatorClient.
    bool supportsVersion(unsigned version) override;
    bool canMakePayments() override;
    void canMakePaymentsWithActiveCard(const String& merchantIdentifier, const String& domainName, std::function<void (bool)> completionHandler) override;
    void openPaymentSetup(const String& merchantIdentifier, const String& domainName, std::function<void (bool)> completionHandler) override;
    bool showPaymentUI(const WebCore::URL& originatingURL, const Vector<WebCore::URL>& linkIconURLs, const WebCore::PaymentRequest&) override;
    void completeMerchantValidation(const WebCore::PaymentMerchantSession&) override;
    void completeShippingMethodSelection(WebCore::PaymentAuthorizationStatus, Optional<WebCore::PaymentRequest::TotalAndLineItems> newTotalAndItems) override;
    void completeShippingContactSelection(WebCore::PaymentAuthorizationStatus, const Vector<WebCore::PaymentRequest::ShippingMethod>&, Optional<WebCore::PaymentRequest::TotalAndLineItems> newTotalAndItems) override;
    void completePaymentMethodSelection(Optional<WebCore::PaymentRequest::TotalAndLineItems>) override;
    void completePaymentSession(WebCore::PaymentAuthorizationStatus) override;
    void abortPaymentSession() override;

    void paymentCoordinatorDestroyed() override;

    // IPC::MessageReceiver.
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;

    // Message handlers.
    void validateMerchant(const String& validationURLString);
    void didAuthorizePayment(const WebCore::Payment&);
    void didSelectShippingMethod(const WebCore::PaymentRequest::ShippingMethod&);
    void didSelectShippingContact(const WebCore::PaymentContact&);
    void didSelectPaymentMethod(const WebCore::PaymentMethod&);
    void didCancelPayment();
    void canMakePaymentsWithActiveCardReply(uint64_t requestID, bool canMakePayments);
    void openPaymentSetupReply(uint64_t requestID, bool result);

    WebCore::PaymentCoordinator& paymentCoordinator();

    WebPage& m_webPage;

    HashMap<uint64_t, std::function<void (bool)>> m_pendingCanMakePaymentsWithActiveCardCallbacks;
    HashMap<uint64_t, std::function<void (bool)>> m_pendingOpenPaymentSetupCallbacks;
};

}
#endif
