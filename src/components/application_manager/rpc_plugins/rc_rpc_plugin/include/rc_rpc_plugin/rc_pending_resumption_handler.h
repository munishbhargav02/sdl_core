#pragma once

#include "application_manager/event_engine/event_observer.h"
#include "application_manager/resumption/extension_pending_resumption_handler.h"

namespace rc_rpc_plugin {

/**
 * @brief The RCPendingResumptionHandler class
 * responsibility to avoid duplication of subscription requests to HMI
 * if multiple applications are registering
 */
class RCPendingResumptionHandler
    : public resumption::ExtensionPendingResumptionHandler {


  void on_event(const application_manager::event_engine::Event& event) override;

  void HandleResumptionSubscriptionRequest(application_manager::AppExtension& extension,
                                           resumption::Subscriber& subscriber, application_manager::Application& app) override;
  void ClearPendingResumptionRequests() override;
};

}  // namespace rc_rpc_plugin
