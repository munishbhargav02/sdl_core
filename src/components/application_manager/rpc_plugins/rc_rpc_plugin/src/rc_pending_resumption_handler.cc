#include "rc_rpc_plugin/rc_pending_resumption_handler.h"
#include "rc_rpc_plugin/rc_helpers.h"
#include "rc_rpc_plugin/rc_module_constants.h"

namespace rc_rpc_plugin {

CREATE_LOGGERPTR_GLOBAL(logger_, "RCPendingResumptionHandler")

RCPendingResumptionHandler::RCPendingResumptionHandler(
    application_manager::ApplicationManager& application_manager)
    : ExtensionPendingResumptionHandler(application_manager)
    , rpc_service_(application_manager.GetRPCService()) {}

void RCPendingResumptionHandler::on_event(
    const application_manager::event_engine::Event& event) {
  LOG4CXX_AUTO_TRACE(logger_);
  sync_primitives::AutoLock lock(pending_resumption_lock_);
  // remove saved resumption request
  // if event success
  //    find freazed resumptions that covered by event
  //    raize events for this freezed resumptions
  // freezed_subscription = freezed.pop
  // request_ref = CreateResumptionRequest(freezed_subscription)
  //      request_ref[application_manager::strings::params]
  //                 [application_manager::strings::correlation_id]
  //                    .asUInt();
  // auto resumption_request = MakeResumptionRequest(corr_id, function_id,
  // *request);
  // save (resumption_request)
  // subscribe_on_event(function_id, corr_id);
  auto cid = event.smart_object_correlation_id();
  if (pending_requests_.find(cid) == pending_requests_.end()) {
    LOG4CXX_DEBUG(logger_, "correlation id: " << cid << " NOT found");
    return;
  }

  auto current_request = pending_requests_.at(cid);
  pending_requests_.erase(cid);
  auto module_uid = GetModuleUid(current_request);

  LOG4CXX_DEBUG(logger_,
                "Received event with function id: "
                    << event.id() << " and correlation id: " << cid
                    << " module type: " << module_uid.first
                    << " module id: " << module_uid.second);

  auto& response = event.smart_object();
  const bool succesful_response = IsResponseSuccessful(response);

  if (succesful_response) {
    LOG4CXX_DEBUG(logger_, "Resumption of subscriptions is successful");
    ProcessSuccessfulResponse(event, module_uid);
  } else {
    LOG4CXX_DEBUG(logger_, "Resumption of subscriptions is NOT successful");
    ProcessNextFreezedResumption(module_uid);
  }
}

void RCPendingResumptionHandler::HandleResumptionSubscriptionRequest(
    application_manager::AppExtension& extension,
    resumption::Subscriber& subscriber,
    application_manager::Application& app) {
  UNUSED(extension);
  LOG4CXX_AUTO_TRACE(logger_);
  sync_primitives::AutoLock lock(pending_resumption_lock_);
  const uint32_t app_id = app.app_id();
  LOG4CXX_TRACE(logger_, "app id " << app_id);

  auto rc_extension = RCHelpers::GetRCExtension(app);
  auto subscriptions = rc_extension->InteriorVehicleDataSubscriptions();
  // subscriptions = RemoveSubscriptionThatDoNotNeedTheRequests(subscriptions);
  std::vector<ModuleUid> already_pending;
  std::vector<ModuleUid> need_to_subscribe;
  for (auto subscription : subscriptions) {
    if (IsPending(subscription)) {
      already_pending.push_back(subscription);
    } else {
      need_to_subscribe.push_back(subscription);
    }
  }

  for (auto subscription : already_pending) {
    // resumption request = getrequestfor(subscription)
    // create hmi request for subscription
    // freeze hmi request for subscription
    // subscriber(freezed subscription)
    const auto cid = application_manager_.GetNextHMICorrelationID();
    auto subscription_request = CreateSubscriptionRequest(subscription, cid);
    auto fid = GetFunctionId(subscription_request);
    auto resumption_request =
        MakeResumptionRequest(cid, fid, subscription_request);
    freezed_resumptions_[subscription].push(resumption_request);
    subscriber(app_id, resumption_request);
    LOG4CXX_DEBUG(logger_,
                  "Freezed request with function id: "
                      << fid << " and correlation_id: " << cid);
  }

  for (auto subscription : need_to_subscribe) {
    // request_ref = CreateResumptionRequest(subscription)
    // const uint32_t corr_id =
    //      request_ref[application_manager::strings::params]
    //                 [application_manager::strings::correlation_id]
    //                    .asUInt();
    // auto resumption_request = MakeResumptionRequest(corr_id, function_id,
    // *request); save (resumption_request) subscribe_on_event(function_id,
    // corr_id); subscriber(app.app_id(), resumption_request);
    // rpc_service_.ManageHMICommand(request);
    pending_subscriptions_.push_back(subscription);
    const auto cid = application_manager_.GetNextHMICorrelationID();
    auto subscription_request = CreateSubscriptionRequest(subscription, cid);
    auto fid = GetFunctionId(subscription_request);
    auto resumption_request =
        MakeResumptionRequest(cid, fid, subscription_request);
    pending_requests_.emplace(cid, *subscription_request);
    LOG4CXX_DEBUG(logger_,
                  "Subscribing for event with function id: "
                      << fid << " correlation id: " << cid);
    subscribe_on_event(fid, cid);
    subscriber(app_id, resumption_request);
    LOG4CXX_DEBUG(logger_,
                  "Sending request with function id: "
                      << fid << " and correlation_id: " << cid);
    application_manager_.GetRPCService().ManageHMICommand(subscription_request);
  }
}

void RCPendingResumptionHandler::ClearPendingResumptionRequests() {
  // remove all freazed subscriptions
  // remove saved resumption requeasts
  LOG4CXX_AUTO_TRACE(logger_);
  sync_primitives::AutoLock lock(pending_resumption_lock_);

  for (auto const& it : pending_requests_) {
    const auto timed_out_pending_request_fid =
        static_cast<hmi_apis::FunctionID::eType>(
            it.second[app_mngr::strings::params][app_mngr::strings::function_id]
                .asInt());
    unsubscribe_from_event(timed_out_pending_request_fid);
  }

  pending_requests_.clear();
  freezed_resumptions_.clear();
  pending_subscriptions_.clear();
}

void RCPendingResumptionHandler::ProcessSuccessfulResponse(
    const application_manager::event_engine::Event& event,
    const ModuleUid& module_uid) {
  LOG4CXX_AUTO_TRACE(logger_);

  auto& response = event.smart_object();
  auto cid = event.smart_object_correlation_id();

  RaiseEvent4Response(response, cid);

  unsubscribe_from_event(event.id());

  const auto& it_queue_freezed = freezed_resumptions_.find(module_uid);
  if (it_queue_freezed != freezed_resumptions_.end()) {
    LOG4CXX_DEBUG(logger_, "Freezed resumptions found");
    auto& queue_freezed = it_queue_freezed->second;
    while (!queue_freezed.empty()) {
      const auto& resumption_request = queue_freezed.front();
      cid = resumption_request
                .message[app_mngr::strings::params]
                        [app_mngr::strings::correlation_id]
                .asInt();
      queue_freezed.pop();
      RaiseEvent4Response(response, cid);
    }
    freezed_resumptions_.erase(it_queue_freezed);
  }
}

void RCPendingResumptionHandler::ProcessNextFreezedResumption(
    const ModuleUid& module_uid) {
  LOG4CXX_AUTO_TRACE(logger_);

  auto pop_front_freezed_resumptions = [this](const ModuleUid& module_uid) {
    const auto& it_queue_freezed = freezed_resumptions_.find(module_uid);
    if (it_queue_freezed == freezed_resumptions_.end()) {
      return std::shared_ptr<QueueFreezedResumptions::value_type>(nullptr);
    }
    auto& queue_freezed = it_queue_freezed->second;
    if (queue_freezed.empty()) {
      freezed_resumptions_.erase(it_queue_freezed);
      return std::shared_ptr<QueueFreezedResumptions::value_type>(nullptr);
    }
    auto freezed_resumption =
        std::make_shared<QueueFreezedResumptions::value_type>(
            queue_freezed.front());
    queue_freezed.pop();
    if (queue_freezed.empty()) {
      freezed_resumptions_.erase(it_queue_freezed);
    }
    return freezed_resumption;
  };

  auto freezed_resumption = pop_front_freezed_resumptions(module_uid);
  if (!freezed_resumption) {
    LOG4CXX_DEBUG(logger_, "Not freezed resumptions found");
    return;
  }

  auto& resumption_request = *freezed_resumption;
  auto subscription_request =
      std::make_shared<smart_objects::SmartObject>(resumption_request.message);
  const auto fid = GetFunctionId(subscription_request);
  const auto cid =
      resumption_request
          .message[app_mngr::strings::params][app_mngr::strings::correlation_id]
          .asInt();
  LOG4CXX_DEBUG(logger_,
                "Subscribing for event with function id: "
                    << fid << " correlation id: " << cid);
  subscribe_on_event(fid, cid);
  pending_requests_.emplace(cid, *subscription_request);
  LOG4CXX_DEBUG(logger_,
                "Sending request with fid: " << fid << " and cid: " << cid);
  application_manager_.GetRPCService().ManageHMICommand(subscription_request);
}

smart_objects::SmartObjectSPtr
RCPendingResumptionHandler::CreateSubscriptionRequest(
    const ModuleUid& module, const uint32_t correlation_id) const {
  auto request =
      RCHelpers::CreateUnsubscribeRequestToHMI(module, correlation_id);
  auto& msg_params = (*request)[application_manager::strings::msg_params];
  msg_params[message_params::kSubscribe] = true;
  return request;
}

void RCPendingResumptionHandler::RaiseEvent4Response(
    const smart_objects::SmartObject& subscription_response,
    const uint32_t correlation_id) const {
  smart_objects::SmartObject event_message = subscription_response;
  event_message[app_mngr::strings::params][app_mngr::strings::correlation_id] =
      correlation_id;

  smart_objects::SmartObject& module_data =
      event_message[app_mngr::strings::msg_params][message_params::kModuleData];
  if (module_data.keyExists(message_params::kAudioControlData)) {
    smart_objects::SmartObject& audio_control_data =
        module_data[message_params::kAudioControlData];
    if (audio_control_data.keyExists(message_params::kKeepContext)) {
      audio_control_data.erase(message_params::kKeepContext);
    }
  }

  app_mngr::event_engine::Event event(
      hmi_apis::FunctionID::RC_GetInteriorVehicleData);
  event.set_smart_object(event_message);
  event.raise(application_manager_.event_dispatcher());
}

const hmi_apis::FunctionID::eType RCPendingResumptionHandler::GetFunctionId(
    smart_objects::SmartObjectSPtr subscription_request) const {
  auto& request_ref = *subscription_request;
  const auto function_id = static_cast<hmi_apis::FunctionID::eType>(
      request_ref[app_mngr::strings::params][app_mngr::strings::function_id]
          .asInt());

  return function_id;
}

ModuleUid RCPendingResumptionHandler::GetModuleUid(
    smart_objects::SmartObject subscription_request) const {
  smart_objects::SmartObject& msg_params =
      subscription_request[app_mngr::strings::msg_params];
  return ModuleUid(msg_params[message_params::kModuleType].asString(),
                   msg_params[message_params::kModuleId].asString());
}

bool RCPendingResumptionHandler::IsResponseSuccessful(
    const smart_objects::SmartObject& response) {
  const hmi_apis::Common_Result::eType result_code =
      static_cast<hmi_apis::Common_Result::eType>(
          response[app_mngr::strings::params][app_mngr::hmi_response::code]
              .asInt());
  return (result_code == hmi_apis::Common_Result::SUCCESS ||
          result_code == hmi_apis::Common_Result::WARNINGS);
}

bool RCPendingResumptionHandler::IsPending(const ModuleUid subscription) const {
  auto it = std::find(pending_subscriptions_.begin(),
                      pending_subscriptions_.end(),
                      subscription);
  return it != pending_subscriptions_.end();
}

}  // namespace rc_rpc_plugin
