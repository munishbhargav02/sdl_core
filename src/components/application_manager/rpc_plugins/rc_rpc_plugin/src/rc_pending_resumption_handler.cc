#include "rc_rpc_plugin/rc_pending_resumption_handler.h"
#include "rc_rpc_plugin/rc_helpers.h"

namespace rc_rpc_plugin {
void RCPendingResumptionHandler::on_event(const application_manager::event_engine::Event& event) {
//  remove save resumption requeast
}

void RCPendingResumptionHandler::HandleResumptionSubscriptionRequest(application_manager::AppExtension& extension,
                                                                     resumption::Subscriber& subscriber,
                                                                     application_manager::Application& app) {

  auto rc_extension = RCHelpers::GetRCExtension(app);
  auto subscriptions = rc_extension->InteriorVehicleDataSubscriptions();
  std::vector<ModuleUid> already_pending;
  std::vector<ModuleUid> need_to_subscribe;
  for (auto subscription: subscriptions) {
    if (IsPending(subscription)) {
      already_pending.push_back(subscription);
    } else {
      need_to_subscribe.push_back(subscription);
    }
  }

  for (auto subscription: already_pending) {
    // resumption request = getrequestfor(subscription)
    // subscriber(resumption request corr_id, resumption request)
  }

  for (auto subscription: need_to_subscribe) {
    // request_ref = CreateResumptionRequest(subscription)
    // const uint32_t corr_id =
    //      request_ref[application_manager::strings::params]
    //                 [application_manager::strings::correlation_id]
    //                    .asUInt();
    // auto resumption_request = MakeResumptionRequest(corr_id, function_id, *request);
    // save (resumption_request)
    // subscribe_on_event(function_id, corr_id);
    // subscriber(app.app_id(), resumption_request);
    // rpc_service_.ManageHMICommand(request);
  }

}


void rc_rpc_plugin::RCPendingResumptionHandler::ClearPendingResumptionRequests() {
        // should not be implemented for RC
  }

bool RCPendingResumptionHandler::IsPending(const ModuleUid subscription) const {
  auto it = std::find(pending_subscriptions_.begin(), pending_subscriptions_.end(), subscription);
  return it !=pending_subscriptions_.end();
}

}  // namespace rc_rpc_plugin
