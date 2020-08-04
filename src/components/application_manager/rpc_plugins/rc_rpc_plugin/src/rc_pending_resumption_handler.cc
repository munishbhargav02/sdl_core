#include "rc_rpc_plugin/rc_pending_resumption_handler.h"
namespace rc_rpc_plugin {
void RCPendingResumptionHandler::on_event(const application_manager::event_engine::Event& event) {
  // should not be implemented for RC
}

void RCPendingResumptionHandler::HandleResumptionSubscriptionRequest(application_manager::AppExtension& extension,
                                                                     resumption::Subscriber& subscriber,
                                                                     application_manager::Application& app) {

  // Check if required module is waiting for response
  // If required module is waiting for response
  //    subscribe the subscriber for response for module waiting for reseponse
  //    return
  // Send GetInteriorDataSubscription to not waiting for response modules
  // save waiting for response modules
  // subscribe the subscriber for just sent subscriptions
}


void rc_rpc_plugin::RCPendingResumptionHandler::ClearPendingResumptionRequests() {
  // should not be implemented for RC
}

}  // namespace rc_rpc_plugin
