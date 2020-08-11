/*
 Copyright (c) 2020, Ford Motor Company
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following
 disclaimer in the documentation and/or other materials provided with the
 distribution.

 Neither the name of the Ford Motor Company nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include "vehicle_info_plugin/vehicle_info_pending_resumption_handler.h"
#include <boost/range/algorithm/set_algorithm.hpp>
#include <functional>
#include "application_manager/event_engine/event_observer.h"
#include "application_manager/message_helper.h"
#include "application_manager/resumption/resumption_data_processor.h"
#include "utils/helpers.h"
#include "vehicle_info_plugin/custom_vehicle_data_manager.h"

namespace vehicle_info_plugin {
CREATE_LOGGERPTR_GLOBAL(logger_, "VehicleInfoPlugin")

using hmi_apis::FunctionID::VehicleInfo_SubscribeVehicleData;

uint32_t get_corr_id_from_message(const smart_objects::SmartObject& message) {
  using namespace application_manager;
  return message[strings::params][strings::correlation_id].asInt();
}

template <class T>
std::string Stringify(const T& container) {
  std::stringstream ss;
  for (const auto& val : container) {
    ss << val << " ";
  }
  return ss.str();
}

VehicleInfoPendingResumptionHandler::VehicleDataList SubscriptionsFromResponse(
    const smart_objects::SmartObject& response,
    std::function<bool(const smart_objects::SmartObject& vehicle_data)>
        vehicle_data_check) {
  namespace strings = application_manager::strings;
  VehicleInfoPendingResumptionHandler::VehicleDataList result;
  const auto response_params = response[strings::msg_params];
  const auto response_keys = response_params.enumerate();
  for (auto key : response_keys) {
    if (vehicle_data_check(response_params[key])) {
      result.insert(key);
    }
  }
  return result;
}

bool IsResponseSuccessful(const smart_objects::SmartObject& response) {
  namespace strings = application_manager::strings;
  return !response[strings::params].keyExists(strings::error_msg);
}

void FillResponseWithMissedVD(
    const VehicleInfoPendingResumptionHandler::VehicleDataList& vehicle_data,
    smart_objects::SmartObject* response) {
  DCHECK(response)
  namespace strings = application_manager::strings;
  auto& msg_params = (*response)[strings::msg_params];
  for (const auto& vd : vehicle_data) {
    smart_objects::SmartObject vd_result(smart_objects::SmartType_Map);
    vd_result[strings::result_code] =
        hmi_apis::Common_VehicleDataResultCode::VDRC_SUCCESS;
    msg_params[vd] = vd_result;
  }
}

VehicleInfoPendingResumptionHandler::VehicleDataList
SuccessfulSubscriptionsFromResponse(
    const smart_objects::SmartObject& response) {
  LOG4CXX_AUTO_TRACE(logger_);
  using namespace application_manager;

  VehicleInfoPendingResumptionHandler::VehicleDataList result;
  if (!IsResponseSuccessful(response)) {
    return result;
  }

  auto successful_vehicle_data =
      [](const smart_objects::SmartObject& vehicle_data) {
        constexpr auto kSuccess =
            hmi_apis::Common_VehicleDataResultCode::VDRC_SUCCESS;
        const auto vd_result_code = vehicle_data[strings::result_code].asInt();
        return kSuccess == vd_result_code;
      };
  return SubscriptionsFromResponse(response, successful_vehicle_data);
}

VehicleInfoPendingResumptionHandler::VehicleInfoPendingResumptionHandler(
    application_manager::ApplicationManager& application_manager,
    CustomVehicleDataManager& custom_vehicle_data_manager)
    : ExtensionPendingResumptionHandler(application_manager)
    , custom_vehicle_data_manager_(custom_vehicle_data_manager) {}

void VehicleInfoPendingResumptionHandler::OnResumptionRevert() {
  LOG4CXX_AUTO_TRACE(logger_);
  sync_primitives::AutoLock lock(pending_resumption_lock_);
  TriggerPendingResumption();
}

void VehicleInfoPendingResumptionHandler::RaiseFinishedPendingResumption(
    const PendingSubscriptionsResumption& pending_resumption) {
  LOG4CXX_AUTO_TRACE(logger_);
  using namespace application_manager;

  auto app = application_manager_.application(pending_resumption.app_id_);
  if (!app) {
    LOG4CXX_DEBUG(logger_,
                  "Application not found " << pending_resumption.app_id_);
    return;
  }
  auto& ext = VehicleInfoAppExtension::ExtractVIExtension(*app);
  ext.RemovePendingSubscriptions();
  for (const auto& subscription : pending_resumption.restored_vehicle_data_) {
    LOG4CXX_DEBUG(logger_,
                  "Subscribe " << app->app_id() << "  to " << subscription);
    ext.subscribeToVehicleInfo(subscription);
  }

  auto fake_response =
      CreateFakeResponseFromHMI(pending_resumption.subscription_results_,
                                pending_resumption.fake_corr_id_);
  event_engine::Event event(VehicleInfo_SubscribeVehicleData);
  event.set_smart_object(fake_response);
  LOG4CXX_DEBUG(logger_, "Raise fake response for resumption data processor");
  event.raise(application_manager_.event_dispatcher());
}

void VehicleInfoPendingResumptionHandler::SendHMIRequestForNotSubscribed(
    const PendingSubscriptionsResumption& pending_resumption) {
  LOG4CXX_AUTO_TRACE(logger_);
  const auto remaining_subscriptions = pending_resumption.NotSubscribedData();
  auto request = CreateSubscribeRequestToHMI(remaining_subscriptions);
  const auto corr_id = get_corr_id_from_message(*request);
  subscribe_on_event(VehicleInfo_SubscribeVehicleData, corr_id);
  application_manager_.GetRPCService().ManageHMICommand(request);
}

void VehicleInfoPendingResumptionHandler::ProcessNextPendingResumption(
    const smart_objects::SmartObject& response_message) {
  LOG4CXX_AUTO_TRACE(logger_);
  if (pending_requests_.empty()) {
    LOG4CXX_DEBUG(logger_, "No more pending resumptions");
    return;
  }
  auto& pending = pending_requests_.front();
  if (pending.waiting_for_hmi_response_) {
    LOG4CXX_DEBUG(logger_,
                  "Requests was already sent to HMI for " << pending.app_id_);
    return;
  }
  const auto successful_subscriptions =
      SuccessfulSubscriptionsFromResponse(response_message);
  pending.FillRestoredData(successful_subscriptions);

  if (!pending.IsSuccessfullyDone()) {
    SendHMIRequestForNotSubscribed(pending);
    pending.waiting_for_hmi_response_ = true;
    return;
  }
  auto pending_copy = pending;
  pending_requests_.pop_front();
  RaiseFinishedPendingResumption(pending_copy);
  ProcessNextPendingResumption(response_message);
}

void VehicleInfoPendingResumptionHandler::TriggerPendingResumption() {
  LOG4CXX_AUTO_TRACE(logger_);
  if (pending_requests_.empty()) {
    LOG4CXX_DEBUG(logger_, "No pending resumptions");
    return;
  }
  auto& pending_resumption = pending_requests_.front();
  if (pending_resumption.waiting_for_hmi_response_) {
    LOG4CXX_DEBUG(logger_,
                  "Pending resumption for  "
                      << pending_resumption.app_id_
                      << " is already waiting for HMI response");
    return;
  }
  SendHMIRequestForNotSubscribed(pending_resumption);
  pending_resumption.waiting_for_hmi_response_ = true;
}

void VehicleInfoPendingResumptionHandler::on_event(
    const application_manager::event_engine::Event& event) {
  LOG4CXX_AUTO_TRACE(logger_);
  sync_primitives::AutoLock lock(pending_resumption_lock_);
  using namespace application_manager;
  if (pending_requests_.empty()) {
    LOG4CXX_DEBUG(logger_, "Not waiting for any response");
    return;
  }

  auto current_pending = pending_requests_.front();

  auto response_message = event.smart_object();
  smart_objects::SmartObject converted_msg_params(
      response_message[strings::msg_params]);

  custom_vehicle_data_manager_.CreateMobileMessageParams(converted_msg_params);
  response_message[strings::msg_params] = converted_msg_params;
  pending_requests_.pop_front();

  const auto vs_count_in_response =
      response_message[application_manager::strings::msg_params].length();
  if (IsResponseSuccessful(response_message) && vs_count_in_response == 0) {
    FillResponseWithMissedVD(current_pending.requested_vehicle_data_,
                             &response_message);
  }

  current_pending.FillSubscriptionResults(response_message);

  RaiseFinishedPendingResumption(current_pending);

  ProcessNextPendingResumption(response_message);
}

VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption
VehicleInfoPendingResumptionHandler::SubscribeToFakeRequest(
    const uint32_t app_id,
    const VehicleDataList& subscriptions,
    resumption::Subscriber& subscriber) {
  LOG4CXX_AUTO_TRACE(logger_);
  const auto fake_request = CreateSubscribeRequestToHMI(subscriptions);
  const auto fake_corr_id = get_corr_id_from_message(*fake_request);
  auto resumption_request = MakeResumptionRequest(
      fake_corr_id,
      hmi_apis::FunctionID::VehicleInfo_SubscribeVehicleData,
      *fake_request);
  LOG4CXX_DEBUG(logger_,
                "Subscribe subscriber "
                    << app_id
                    << " to fake request with corr id = " << fake_corr_id);
  subscriber(app_id, resumption_request);
  PendingSubscriptionsResumption pending_request(
      app_id, fake_corr_id, subscriptions);
  return pending_request;
}

void VehicleInfoPendingResumptionHandler::HandleResumptionSubscriptionRequest(
    application_manager::AppExtension& extension,
    resumption::Subscriber& subscriber,
    application_manager::Application& app) {
  LOG4CXX_AUTO_TRACE(logger_);
  sync_primitives::AutoLock lock(pending_resumption_lock_);
  LOG4CXX_TRACE(logger_, "app id " << app.app_id());
  UNUSED(extension);
  auto& ext = VehicleInfoAppExtension::ExtractVIExtension(app);

  const auto subscriptions = ext.PendingSubscriptions().GetData();
  if (subscriptions.empty()) {
    LOG4CXX_DEBUG(logger_, "Subscriptions is empty");
    return;
  }
  LOG4CXX_TRACE(logger_,
                "resume subscriptions to : " << Stringify(subscriptions));
  auto pending_request =
      SubscribeToFakeRequest(app.app_id(), subscriptions, subscriber);

  pending_requests_.push_back(pending_request);
  LOG4CXX_DEBUG(
      logger_,
      "Add to pending resumptins corr_id = " << pending_request.fake_corr_id_);
  if (pending_requests_.size() == 1) {
    TriggerPendingResumption();
  }
  // If there was pending resumption before, it will be triggered on HMI
  // response
}

smart_objects::SmartObjectSPtr
VehicleInfoPendingResumptionHandler::CreateSubscribeRequestToHMI(
    const VehicleDataList& subscriptions) {
  sync_primitives::AutoLock lock(pending_resumption_lock_);
  using namespace application_manager;
  smart_objects::SmartObject msg_params =
      smart_objects::SmartObject(smart_objects::SmartType_Map);

  for (const auto& ivi_data : subscriptions) {
    msg_params[ivi_data] = true;
  }

  smart_objects::SmartObjectSPtr request =
      application_manager::MessageHelper::CreateModuleInfoSO(
          VehicleInfo_SubscribeVehicleData, application_manager_);
  (*request)[strings::msg_params] = msg_params;

  return request;
}

smart_objects::SmartObject
VehicleInfoPendingResumptionHandler::CreateFakeResponseFromHMI(
    const std::map<std::string, smart_objects::SmartObject>& subscriptions,
    const uint32_t fake_corrlation_id) {
  LOG4CXX_AUTO_TRACE(logger_);
  namespace strings = application_manager::strings;

  auto response =
      application_manager::MessageHelper::CreateResponseMessageFromHmi(
          VehicleInfo_SubscribeVehicleData,
          fake_corrlation_id,
          hmi_apis::Common_Result::SUCCESS);
  auto& message = *response;
  smart_objects::SmartObject msg_params(smart_objects::SmartType_Map);
  for (const auto& subscription : subscriptions) {
    msg_params[subscription.first] = subscription.second;
    LOG4CXX_DEBUG(logger_,
                  "fake response data : "
                      << subscription.first << " result = "
                      << subscription.second[strings::result_code].asInt());
  }

  message[strings::msg_params] = msg_params;
  return *response;
}

bool VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption::
    IsSuccessfullyDone() const {
  return requested_vehicle_data_.size() == restored_vehicle_data_.size();
}

bool VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption::
    DataWasRequested(const std::string& vd) const {
  bool result =
      (requested_vehicle_data_.end() != requested_vehicle_data_.find(vd));
  return result;
}

VehicleInfoPendingResumptionHandler::VehicleDataList
VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption::
    NotSubscribedData() const {
  VehicleDataList not_subscribed;
  boost::set_difference(requested_vehicle_data_,
                        restored_vehicle_data_,
                        std::inserter(not_subscribed, not_subscribed.end()));
  return not_subscribed;
}

void VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption::
    FillSubscriptionResults() {
  namespace strings = application_manager::strings;
  for (const auto& key : restored_vehicle_data_) {
    smart_objects::SmartObject vd_result(smart_objects::SmartType_Map);
    vd_result[strings::result_code] =
        hmi_apis::Common_VehicleDataResultCode::VDRC_SUCCESS;
    subscription_results_[key] = vd_result;
  }

  const auto not_subscribed = NotSubscribedData();
  for (const auto& key : not_subscribed) {
    smart_objects::SmartObject vd_result(smart_objects::SmartType_Map);
    vd_result[strings::result_code] =
        hmi_apis::Common_VehicleDataResultCode::VDRC_DATA_NOT_SUBSCRIBED;
    subscription_results_[key] = vd_result;
  }
}

void VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption::
    FillRestoredData(const VehicleDataList& successful_subscriptions) {
  for (auto& subscribed : successful_subscriptions) {
    if (DataWasRequested(subscribed)) {
      restored_vehicle_data_.insert(subscribed);
    }
  }
}

void VehicleInfoPendingResumptionHandler::PendingSubscriptionsResumption::
    FillSubscriptionResults(const smart_objects::SmartObject& response) {
  LOG4CXX_AUTO_TRACE(logger_);
  using namespace application_manager;

  auto successful_subscriptions = SuccessfulSubscriptionsFromResponse(response);

  LOG4CXX_DEBUG(logger_,
                "Requested data : " << Stringify(requested_vehicle_data_));
  LOG4CXX_DEBUG(logger_,
                "Successful subscription in response : "
                    << Stringify(successful_subscriptions));

  FillRestoredData(successful_subscriptions);

  LOG4CXX_DEBUG(logger_,
                "Restored data : " << Stringify(restored_vehicle_data_));

  FillSubscriptionResults();

  auto msg_params = response[strings::msg_params];
  auto keys = msg_params.enumerate();
  for (auto key : keys) {
    if (DataWasRequested(key)) {
      subscription_results_[key] = msg_params[key];
    }
  }
}

}  // namespace vehicle_info_plugin
