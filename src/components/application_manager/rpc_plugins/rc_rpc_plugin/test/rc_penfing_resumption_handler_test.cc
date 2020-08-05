/*
 * Copyright (c) 2018, Ford Motor Company
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of the Ford Motor Company nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#include "application_manager/smart_object_keys.h"
#include "gtest/gtest.h"
#include "rc_rpc_plugin/rc_pending_resumption_handler.h"

namespace rc_rpc_plugin_test {

class RCPendingResumptionHandlerTest : public ::testing::Test {

};



TEST_F(RCPendingResumptionHandler,
       HandleResumptionNosubscriptions) {
  // Create extension with no rc modules
  // call HandleResumptionSubscriptionRequest
  // expect no HMI calls , no subscriptions
}

TEST_F(RCPendingResumptionHandler,
       HandleResumptionOneSubscription) {
  // Create extension with one module
  // call HandleResumptionSubscriptionRequest
  // expect one HMI call , one subscription
}

TEST_F(RCPendingResumptionHandler,
       HandleResumptionMultipleSubscriptions) {
  // Create extension with several modules
  // call HandleResumptionSubscriptionRequest
  // expect  several HMI calls , several subscriptions
}

TEST_F(RCPendingResumptionHandler,
       HandleResumptionWithPendingSubscription) {
  // Create extension1 with one module
  // call HandleResumptionSubscriptionRequest
  // ASSERR one HMI call , assert subscription

  // Create extension2 with the same module as in extension1
  // call HandleResumptionSubscriptionRequest for extension2
  // EXPECT no HMI call, expect subscription
}

TEST_F(RCPendingResumptionHandler,
       HandleResumptionWithPendingSubscriptionAndNotPendingOne) {
  // Create extension1 with one module
  // call HandleResumptionSubscriptionRequest
  // ASSERR one HMI call , assert subscription

  // Create extension2 with the same module as in extension1 and another one module
  // call HandleResumptionSubscriptionRequest for extension2
  // EXPECT HMI call for new module
  // EXPECT no HMI call for first module
  // expect subscription for both modules
}

TEST_F(RCPendingResumptionHandler,
       Resumption2ApplicationsWithCommonDataSuccess) {
  // Create extension1 with one module1
  // call HandleResumptionSubscriptionRequest
  // ASSERR one HMI call , assert subscription
  // Create extension2 with the same module1 as in extension1
  // call HandleResumptionSubscriptionRequest for extension2
  // assert creating HMI message for module1 app2
  // assert subscription for module1
  // assert no HMI call for module1


  // call on_event with succesfull response to module1
  // expect raizing event with succesfull response to module1 for app2
}



TEST_F(RCPendingResumptionHandler,
       Resumption2ApplicationsWithCommonDataFailedRetry) {
  // Create extension1 with one module1
  // call HandleResumptionSubscriptionRequest
  // ASSERR one HMI call , assert subscription
  // Create extension2 with the same module1 as in extension1
  // call HandleResumptionSubscriptionRequest for extension2
  // assert creating HMI message for module1 app2
  // assert subscription for module1
  // assert no HMI call for module1


  // call on_event with unsuccessful response to module1
  // Expect HMI request with module1 subscription
}

}  // namespace rc_rpc_plugin_test
