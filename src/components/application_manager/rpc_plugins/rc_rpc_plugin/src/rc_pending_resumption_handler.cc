#include "rc_rpc_plugin/rc_pending_resumption_handler.h"
#include "rc_rpc_plugin/rc_helpers.h"

namespace rc_rpc_plugin {
RCPendingResumptionHandler::RCPendingResumptionHandler(
    application_manager::ApplicationManager& application_manager)
    : ExtensionPendingResumptionHandler(application_manager)
    , rpc_service_(application_manager.GetRPCService()) {}

void RCPendingResumptionHandler::on_event(
    const application_manager::event_engine::Event& event) {
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
}

void RCPendingResumptionHandler::HandleResumptionSubscriptionRequest(
    application_manager::AppExtension& extension,
    resumption::Subscriber& subscriber,
    application_manager::Application& app) {
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
  }
}

void rc_rpc_plugin::RCPendingResumptionHandler::
    ClearPendingResumptionRequests() {
  // remove all freazed subscriptions
  // remove saved resumption requeasts
}

bool RCPendingResumptionHandler::IsPending(const ModuleUid subscription) const {
  auto it = std::find(pending_subscriptions_.begin(),
                      pending_subscriptions_.end(),
                      subscription);
  return it != pending_subscriptions_.end();
}

}  // namespace rc_rpc_plugin
