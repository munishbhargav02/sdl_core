/*
 Copyright (c) 2018, Ford Motor Company
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following
 disclaimer in the documentation and/or other materials provided with the
 distribution.

 Neither the name of the copyright holders nor the names of their contributors
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

#include "template_plugin/mobile_command_factory.h"
#include "interfaces/MOBILE_API.h"

#include "template_plugin/commands/mobile/register_app_interface_request.h"

CREATE_LOGGERPTR_GLOBAL(logger_, "ApplicationManager")
namespace template_plugin {
using namespace application_manager;

CommandCreator& MobileCommandFactory::get_command_creator(
    const mobile_apis::FunctionID::eType id,
    const mobile_apis::messageType::eType message_type) const {
  CommandCreatorFactory factory(
      application_manager_, rpc_service_, hmi_capabilities_, policy_handler_);
  switch (id) {
    case mobile_apis::FunctionID::RegisterAppInterfaceID: {
      return factory.GetCreator<commands::RegisterAppInterfaceRequest>();
    }
    default: {}
  }

  return factory.GetCreator<InvalidCommand>();
}

CommandCreator& MobileCommandFactory::get_creator_factory(
    const mobile_apis::FunctionID::eType id,
    const mobile_apis::messageType::eType message_type,
    const app_mngr::commands::Command::CommandSource source) const {
  switch (message_type) {
    case mobile_api::messageType::request: {
      if (app_mngr::commands::Command::CommandSource::SOURCE_MOBILE == source) {
        return get_command_creator(id, message_type);
      }
      break;
    }
    case mobile_api::messageType::response: {
      if (app_mngr::commands::Command::CommandSource::SOURCE_SDL == source) {
        return get_command_creator(id, message_type);
      }
      break;
    }
    case mobile_api::messageType::notification: {
      if (app_mngr::commands::Command::CommandSource::SOURCE_SDL == source) {
        return get_notification_creator(id);
      } else if (app_mngr::commands::Command::CommandSource::SOURCE_MOBILE ==
                 source) {
        return get_notification_from_mobile_creator(id);
      }
      break;
    }
    default: {}
  }
  CommandCreatorFactory factory(
      application_manager_, rpc_service_, hmi_capabilities_, policy_handler_);
  return factory.GetCreator<InvalidCommand>();
}


MobileCommandFactory::MobileCommandFactory(
    ApplicationManager& application_manager,
    rpc_service::RPCService& rpc_service,
    HMICapabilities& hmi_capabilities,
    policy::PolicyHandlerInterface& policy_handler)
    : application_manager_(application_manager)
    , rpc_service_(rpc_service)
    , hmi_capabilities_(hmi_capabilities)
    , policy_handler_(policy_handler) {}

bool MobileCommandFactory::IsAbleToProcess(
    const int32_t function_id,
    const application_manager::commands::Command::CommandSource message_source)
    const {
  auto id = static_cast<mobile_apis::FunctionID::eType>(function_id);
  return get_command_creator(id, mobile_apis::messageType::INVALID_ENUM)
             .CanBeCreated() ||
         get_notification_creator(id).CanBeCreated();
}

CommandSharedPtr MobileCommandFactory::CreateCommand(
    const app_mngr::commands::MessageSharedPtr& message,
    app_mngr::commands::Command::CommandSource source) {
  mobile_apis::messageType::eType message_type =
      static_cast<mobile_apis::messageType::eType>(
          (*message)[strings::params][strings::message_type].asInt());

  mobile_apis::FunctionID::eType function_id =
      static_cast<mobile_apis::FunctionID::eType>(
          (*message)[strings::params][strings::function_id].asInt());

  LOG4CXX_DEBUG(
      logger_,
      "MobileCommandFactory::CreateCommand function_id: " << function_id);

  return get_creator_factory(function_id, message_type, source).create(message);
}

}  // namespace template_plugin
