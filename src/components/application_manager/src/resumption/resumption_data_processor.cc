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

#include <algorithm>

#include "application_manager/application_manager.h"
#include "application_manager/commands/command_impl.h"
#include "application_manager/display_capabilities_builder.h"
#include "application_manager/event_engine/event_observer.h"
#include "application_manager/message_helper.h"
#include "application_manager/resumption/resumption_data_processor.h"
#include "application_manager/smart_object_keys.h"

namespace resumption {

using app_mngr::AppFile;
using app_mngr::ApplicationSharedPtr;
using app_mngr::ButtonSubscriptions;
using app_mngr::ChoiceSetMap;
using app_mngr::MessageHelper;
namespace strings = app_mngr::strings;
namespace event_engine = app_mngr::event_engine;
namespace commands = app_mngr::commands;

CREATE_LOGGERPTR_GLOBAL(logger_, "Resumption")

ResumptionDataProcessor::ResumptionDataProcessor(
    app_mngr::ApplicationManager& application_manager)
    : event_engine::EventObserver(application_manager.event_dispatcher())
    , application_manager_(application_manager)
    , resumption_status_lock_()
    , register_callbacks_lock_()
    , request_app_ids_lock_() {}

ResumptionDataProcessor::~ResumptionDataProcessor() {}

void ResumptionDataProcessor::Restore(ApplicationSharedPtr application,
                                      smart_objects::SmartObject& saved_app,
                                      ResumeCtrl::ResumptionCallBack callback) {
  LOG4CXX_AUTO_TRACE(logger_);

  if (!HasDataToRestore(saved_app) &&
      !HasGlobalPropertiesToRestore(saved_app) &&
      !HasSubscriptionsToRestore(saved_app)) {
    LOG4CXX_DEBUG(logger_, "No data to restore, resumption is successful");
    callback(mobile_apis::Result::SUCCESS, "Data resumption succesful");
    return;
  }

  AddFiles(application, saved_app);
  AddSubmenues(application, saved_app);
  AddCommands(application, saved_app);
  AddChoicesets(application, saved_app);
  SetGlobalProperties(application, saved_app);
  AddSubscriptions(application, saved_app);
  AddWindows(application, saved_app);

  resumption_status_lock_.AcquireForReading();
  const auto app_id = application->app_id();
  bool is_requests_list_empty = true;
  if (resumption_status_.find(app_id) != resumption_status_.end()) {
    is_requests_list_empty =
        resumption_status_[app_id].list_of_sent_requests.empty();
  }
  resumption_status_lock_.Release();

  if (!is_requests_list_empty) {
    sync_primitives::AutoWriteLock lock(register_callbacks_lock_);
    register_callbacks_[app_id] = callback;
  } else {
    LOG4CXX_DEBUG(
        logger_,
        "No requests to HMI for " << app_id << " , resumption is successful");
    callback(mobile_apis::Result::SUCCESS, "Data resumption successful");
  }
}

bool ResumptionDataProcessor::HasDataToRestore(
    const smart_objects::SmartObject& saved_app) const {
  LOG4CXX_AUTO_TRACE(logger_);

  const bool has_data_to_restore =
      !saved_app[strings::application_submenus].empty() ||
      !saved_app[strings::application_commands].empty() ||
      !saved_app[strings::application_choice_sets].empty() ||
      !saved_app[strings::windows_info].empty();

  LOG4CXX_DEBUG(logger_,
                std::boolalpha << "Application has data to restore: "
                               << has_data_to_restore);

  return has_data_to_restore;
}

bool ResumptionDataProcessor::HasGlobalPropertiesToRestore(
    const smart_objects::SmartObject& saved_app) const {
  LOG4CXX_AUTO_TRACE(logger_);

  const smart_objects::SmartObject& global_properties =
      saved_app[strings::application_global_properties];

  const bool has_gl_props_to_restore =
      !global_properties[strings::help_prompt].empty() ||
      !global_properties[strings::keyboard_properties].empty() ||
      !global_properties[strings::menu_icon].empty() ||
      !global_properties[strings::menu_title].empty() ||
      !global_properties[strings::timeout_prompt].empty() ||
      !global_properties[strings::vr_help].empty() ||
      !global_properties[strings::vr_help_title].empty();

  LOG4CXX_DEBUG(logger_,
                std::boolalpha
                    << "Application has global properties to restore: "
                    << has_gl_props_to_restore);

  return has_gl_props_to_restore;
}

bool ResumptionDataProcessor::HasSubscriptionsToRestore(
    const smart_objects::SmartObject& saved_app) const {
  LOG4CXX_AUTO_TRACE(logger_);

  const smart_objects::SmartObject& subscriptions =
      saved_app[strings::application_subscriptions];

  const bool has_ivi_subscriptions =
      !subscriptions[strings::application_vehicle_info].empty();

  const bool has_button_subscriptions =
      !(subscriptions[strings::application_buttons].length() == 1 &&
        static_cast<hmi_apis::Common_ButtonName::eType>(
            subscriptions[strings::application_buttons][0].asInt()) ==
            hmi_apis::Common_ButtonName::CUSTOM_BUTTON);

  const bool has_waypoints_subscriptions =
      subscriptions[strings::subscribed_for_way_points].asBool();

  const bool has_appservice_subscriptions =
      subscriptions.keyExists(app_mngr::hmi_interface::app_service) &&
      !subscriptions[app_mngr::hmi_interface::app_service].empty();

  const bool has_system_capability_subscriptions =
      subscriptions.keyExists(strings::system_capability) &&
      !subscriptions[strings::system_capability].empty();

  const bool has_subscriptions_to_restore =
      has_ivi_subscriptions || has_button_subscriptions ||
      has_waypoints_subscriptions || has_appservice_subscriptions ||
      has_system_capability_subscriptions;

  LOG4CXX_DEBUG(logger_,
                std::boolalpha << "Application has subscriptions to restore: "
                               << has_subscriptions_to_restore);

  return has_subscriptions_to_restore;
}

bool ResumptionRequestIDs::operator<(const ResumptionRequestIDs& other) const {
  return correlation_id < other.correlation_id ||
         function_id < other.function_id;
}

utils::Optional<uint32_t> ResumptionDataProcessor::GetAppIdByRequestId(
    const hmi_apis::FunctionID::eType function_id, const int32_t corr_id) {
  auto predicate =
      [function_id,
       corr_id](const std::pair<ResumptionRequestIDs, std::uint32_t>& item) {
        return item.first.function_id == function_id &&
               item.first.correlation_id == corr_id;
      };

  sync_primitives::AutoReadLock lock(request_app_ids_lock_);
  auto app_id_ptr =
      std::find_if(request_app_ids_.begin(), request_app_ids_.end(), predicate);

  if (app_id_ptr == request_app_ids_.end()) {
    LOG4CXX_ERROR(logger_,
                  "application id for correlation id "
                      << corr_id << " and function id: " << function_id
                      << " was not found.");
    return utils::Optional<uint32_t>::OptionalEmpty::EMPTY;
  }
  return utils::Optional<uint32_t>(app_id_ptr->second);
}

std::vector<ResumptionRequest>::iterator ResumptionDataProcessor::GetRequest(
    const uint32_t app_id,
    const hmi_apis::FunctionID::eType function_id,
    const int32_t corr_id) {
  sync_primitives::AutoWriteLock lock(resumption_status_lock_);
  std::vector<ResumptionRequest>& list_of_sent_requests =
      resumption_status_[app_id].list_of_sent_requests;

  if (resumption_status_.find(app_id) == resumption_status_.end()) {
    LOG4CXX_ERROR(logger_,
                  "No resumption status info found for app_id: " << app_id);
    return list_of_sent_requests.end();
  }

  auto request_iter =
      std::find_if(list_of_sent_requests.begin(),
                   list_of_sent_requests.end(),
                   [function_id, corr_id](const ResumptionRequest& request) {
                     return request.request_ids.correlation_id == corr_id &&
                            request.request_ids.function_id == function_id;
                   });

  return request_iter;
}

utils::Optional<ResumeCtrl::ResumptionCallBack>
ResumptionDataProcessor::GetResumptionCallback(const uint32_t app_id) {
  sync_primitives::AutoReadLock lock(register_callbacks_lock_);
  auto it = register_callbacks_.find(app_id);
  if (it == register_callbacks_.end()) {
    LOG4CXX_WARN(logger_, "Callback for app_id: " << app_id << " not found");
    return utils::Optional<
        ResumeCtrl::ResumptionCallBack>::OptionalEmpty::EMPTY;
  }

  return utils::Optional<ResumeCtrl::ResumptionCallBack>(it->second);
}

void ResumptionDataProcessor::ProcessResponseFromHMI(
    const smart_objects::SmartObject& response,
    const hmi_apis::FunctionID::eType function_id,
    const int32_t corr_id) {
  auto found_app_id = GetAppIdByRequestId(function_id, corr_id);
  if (!found_app_id) {
    return;
  }
  const uint32_t app_id = *found_app_id;
  LOG4CXX_DEBUG(logger_, "app_id is: " << app_id);

  LOG4CXX_DEBUG(logger_,
                "Now processing event with function id: "
                    << function_id << " correlation id: " << corr_id);

  auto request = GetRequest(app_id, function_id, corr_id);

  resumption_status_lock_.AcquireForWriting();
  ApplicationResumptionStatus& status = resumption_status_[app_id];
  auto& list_of_sent_requests =
      resumption_status_[app_id].list_of_sent_requests;

  if (list_of_sent_requests.end() == request) {
    LOG4CXX_ERROR(logger_, "Request not found");
    return;
  }

  if (IsResponseSuccessful(response)) {
    status.successful_requests.push_back(*request);
  } else {
    status.error_requests.push_back(*request);
  }

  if (hmi_apis::FunctionID::VehicleInfo_SubscribeVehicleData == function_id) {
    CheckVehicleDataResponse(request->message, response, status);
  }

  if (hmi_apis::FunctionID::UI_CreateWindow == function_id) {
    CheckCreateWindowResponse(request->message, response);
  }

  list_of_sent_requests.erase(request);

  if (!list_of_sent_requests.empty()) {
    LOG4CXX_DEBUG(logger_,
                  "Resumption app "
                      << app_id << " not finished . Amount of requests left : "
                      << list_of_sent_requests.size());
    resumption_status_lock_.Release();
    return;
  }

  const bool successful_resumption =
      status.error_requests.empty() &&
      status.unsuccessful_vehicle_data_subscriptions_.empty();

  resumption_status_lock_.Release();

  auto found_callback = GetResumptionCallback(app_id);
  if (!found_callback) {
    return;
  }
  auto callback = *found_callback;

  if (successful_resumption) {
    LOG4CXX_DEBUG(logger_, "Resumption for app " << app_id << " successful");
    callback(mobile_apis::Result::SUCCESS, "Data resumption successful");
    application_manager_.state_controller().ResumePostponedWindows(app_id);
  }

  if (!successful_resumption) {
    LOG4CXX_ERROR(logger_, "Resumption for app " << app_id << " failed");
    callback(mobile_apis::Result::RESUME_FAILED, "Data resumption failed");
    RevertRestoredData(application_manager_.application(app_id));
    application_manager_.state_controller().DropPostponedWindows(app_id);
  }

  sync_primitives::AutoWriteLock status_lock(resumption_status_lock_);
  resumption_status_.erase(app_id);

  sync_primitives::AutoWriteLock app_ids_lock(request_app_ids_lock_);
  request_app_ids_.erase({function_id, corr_id});

  sync_primitives::AutoWriteLock callbacks_lock(register_callbacks_lock_);
  register_callbacks_.erase(app_id);
}

void ResumptionDataProcessor::HandleOnTimeOut(
    const uint32_t corr_id, const hmi_apis::FunctionID::eType function_id) {
  LOG4CXX_AUTO_TRACE(logger_);
  LOG4CXX_DEBUG(logger_,
                "Handling timeout with corr id: "
                    << corr_id << " and function_id: " << function_id);

  auto error_response = MessageHelper::CreateNegativeResponseFromHmi(
      function_id,
      corr_id,
      hmi_apis::Common_Result::GENERIC_ERROR,
      std::string());
  ProcessResponseFromHMI(*error_response, function_id, corr_id);
}

void ResumptionDataProcessor::on_event(const event_engine::Event& event) {
  LOG4CXX_AUTO_TRACE(logger_);
  LOG4CXX_DEBUG(
      logger_,
      "Handling response message from HMI "
          << event.id() << " "
          << event.smart_object()[strings::params][strings::correlation_id]
                 .asInt());
  ProcessResponseFromHMI(
      event.smart_object(), event.id(), event.smart_object_correlation_id());
}

std::vector<ResumptionRequest> GetAllFailedRequests(
    uint32_t app_id,
    const std::map<std::int32_t, ApplicationResumptionStatus>&
        resumption_status,
    sync_primitives::RWLock& resumption_status_lock) {
  resumption_status_lock.AcquireForReading();
  std::vector<ResumptionRequest> failed_requests;
  std::vector<ResumptionRequest> missed_requests;
  auto it = resumption_status.find(app_id);
  if (it != resumption_status.end()) {
    failed_requests = it->second.error_requests;
    missed_requests = it->second.list_of_sent_requests;
  }
  resumption_status_lock.Release();

  failed_requests.insert(
      failed_requests.end(), missed_requests.begin(), missed_requests.end());
  return failed_requests;
}

void ResumptionDataProcessor::RevertRestoredData(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);
  LOG4CXX_DEBUG(logger_, "Reverting for app: " << application->app_id());
  DeleteSubmenues(application);
  DeleteCommands(application);
  DeleteChoicesets(application);
  DeleteGlobalProperties(application);
  DeleteSubscriptions(application);
  DeleteWindowsSubscriptions(application);

  resumption_status_lock_.AcquireForWriting();
  resumption_status_.erase(application->app_id());
  resumption_status_lock_.Release();

  register_callbacks_lock_.AcquireForWriting();
  register_callbacks_.erase(application->app_id());
  register_callbacks_lock_.Release();
}

void ResumptionDataProcessor::SubscribeToResponse(
    const int32_t app_id, const ResumptionRequest& request) {
  LOG4CXX_AUTO_TRACE(logger_);
  LOG4CXX_DEBUG(logger_,
                "App " << app_id << " subscribe on "
                       << request.request_ids.function_id << " "
                       << request.request_ids.correlation_id);
  subscribe_on_event(request.request_ids.function_id,
                     request.request_ids.correlation_id);

  resumption_status_lock_.AcquireForWriting();
  resumption_status_[app_id].list_of_sent_requests.push_back(request);
  resumption_status_lock_.Release();

  request_app_ids_lock_.AcquireForWriting();
  request_app_ids_.insert(std::make_pair(request.request_ids, app_id));
  request_app_ids_lock_.Release();
}

void ResumptionDataProcessor::ProcessMessageToHMI(
    smart_objects::SmartObjectSPtr message, bool subscribe_on_response) {
  LOG4CXX_AUTO_TRACE(logger_);
  if (subscribe_on_response) {
    auto function_id = static_cast<hmi_apis::FunctionID::eType>(
        (*message)[strings::params][strings::function_id].asInt());

    const int32_t hmi_correlation_id =
        (*message)[strings::params][strings::correlation_id].asInt();

    const int32_t app_id =
        (*message)[strings::msg_params][strings::app_id].asInt();

    ResumptionRequest wait_for_response;
    wait_for_response.request_ids.correlation_id = hmi_correlation_id;
    wait_for_response.request_ids.function_id = function_id;
    wait_for_response.message = *message;

    SubscribeToResponse(app_id, wait_for_response);
  }
  if (!application_manager_.GetRPCService().ManageHMICommand(message)) {
    LOG4CXX_ERROR(logger_, "Unable to send request");
  }
}

void ResumptionDataProcessor::ProcessMessagesToHMI(
    const smart_objects::SmartObjectList& messages) {
  LOG4CXX_AUTO_TRACE(logger_);
  for (const auto& message : messages) {
    const bool is_request_message =
        application_manager::MessageType::kRequest ==
        (*message)[strings::params][strings::message_type].asInt();

    ProcessMessageToHMI(message, is_request_message);
  }
}

void ResumptionDataProcessor::AddFiles(
    app_mngr::ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);
  if (!saved_app.keyExists(strings::application_files)) {
    LOG4CXX_ERROR(logger_, "application_files section is not exists");
    return;
  }

  const auto application_files =
      saved_app[strings::application_files].asArray();

  for (const auto file_data : *application_files) {
    const bool is_persistent = file_data.keyExists(strings::persistent_file) &&
                               file_data[strings::persistent_file].asBool();
    if (is_persistent) {
      AppFile file;
      file.is_persistent = is_persistent;
      file.is_download_complete =
          file_data[strings::is_download_complete].asBool();
      file.file_name = file_data[strings::sync_file_name].asString();
      file.file_type = static_cast<mobile_apis::FileType::eType>(
          file_data[strings::file_type].asInt());
      application->AddFile(file);
    }
  }
}

void ResumptionDataProcessor::AddWindows(
    application_manager::ApplicationSharedPtr application,
    const ns_smart_device_link::ns_smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);

  if (!saved_app.keyExists(strings::windows_info)) {
    LOG4CXX_ERROR(logger_, "windows_info section does not exist");
    return;
  }

  const auto& windows_info = saved_app[strings::windows_info];
  auto request_list = MessageHelper::CreateUICreateWindowRequestsToHMI(
      application, application_manager_, windows_info);

  ProcessMessagesToHMI(request_list);
}

void ResumptionDataProcessor::AddSubmenues(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);

  if (!saved_app.keyExists(strings::application_submenus)) {
    LOG4CXX_ERROR(logger_, "application_submenus section is not exists");
    return;
  }

  const smart_objects::SmartObject& app_submenus =
      saved_app[strings::application_submenus];

  for (size_t i = 0; i < app_submenus.length(); ++i) {
    const smart_objects::SmartObject& submenu = app_submenus[i];
    application->AddSubMenu(submenu[strings::menu_id].asUInt(), submenu);
  }

  ProcessMessagesToHMI(MessageHelper::CreateAddSubMenuRequestsToHMI(
      application, application_manager_));
}

utils::Optional<ResumptionRequest> FindResumptionSubmenuRequest(
    uint32_t menu_id, std::vector<ResumptionRequest>& requests) {
  using namespace utils;

  auto request_it = std::find_if(
      requests.begin(),
      requests.end(),
      [menu_id](const ResumptionRequest& request) {
        auto& msg_params = request.message[strings::msg_params];
        if (hmi_apis::FunctionID::UI_AddSubMenu ==
            request.request_ids.function_id) {
          uint32_t failed_menu_id = msg_params[strings::menu_id].asUInt();
          return failed_menu_id == menu_id;
        }
        return false;
      });
  if (requests.end() != request_it) {
    return Optional<ResumptionRequest>(*request_it);
  }
  return Optional<ResumptionRequest>::OptionalEmpty::EMPTY;
}

void ResumptionDataProcessor::DeleteSubmenues(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);

  auto failed_requests = GetAllFailedRequests(
      application->app_id(), resumption_status_, resumption_status_lock_);

  auto accessor = application->sub_menu_map();
  const auto& sub_menu_map = accessor.GetData();

  for (const auto& smenu : sub_menu_map) {
    auto failed_submenu_request =
        FindResumptionSubmenuRequest(smenu.first, failed_requests);
    if (!failed_submenu_request) {
      MessageHelper::SendDeleteSubmenuRequest(
          smenu.second, application, application_manager_);
    }
    application->RemoveSubMenu(smenu.first);
  }
}

void ResumptionDataProcessor::AddCommands(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);
  if (!saved_app.keyExists(strings::application_commands)) {
    LOG4CXX_ERROR(logger_, "application_commands section is not exists");
    return;
  }

  const smart_objects::SmartObject& app_commands =
      saved_app[strings::application_commands];

  for (size_t cmd_num = 0; cmd_num < app_commands.length(); ++cmd_num) {
    const smart_objects::SmartObject& command = app_commands[cmd_num];
    const uint32_t cmd_id = command[strings::cmd_id].asUInt();
    const uint32_t consecutive_num =
        commands::CommandImpl::CalcCommandInternalConsecutiveNumber(
            application);

    application->AddCommand(consecutive_num, command);
    application->help_prompt_manager().OnVrCommandAdded(
        cmd_id, command, true);  // is_resumption =true
  }

  ProcessMessagesToHMI(MessageHelper::CreateAddCommandRequestToHMI(
      application, application_manager_));
}

utils::Optional<ResumptionRequest> FindCommandResumptionRequest(
    uint32_t command_id, std::vector<ResumptionRequest>& requests) {
  using namespace utils;

  auto request_it = std::find_if(
      requests.begin(),
      requests.end(),
      [command_id](const ResumptionRequest& request) {
        auto& msg_params = request.message[strings::msg_params];
        const bool is_vr_command = hmi_apis::FunctionID::VR_AddCommand ==
                                       request.request_ids.function_id &&
                                   hmi_apis::Common_VRCommandType::Command ==
                                       msg_params[strings::type].asInt();
        const bool is_ui_command = hmi_apis::FunctionID::UI_AddCommand ==
                                   request.request_ids.function_id;

        if (is_vr_command || is_ui_command) {
          uint32_t cmd_id = msg_params[strings::cmd_id].asUInt();
          return cmd_id == command_id;
        }

        return false;
      });

  if (requests.end() != request_it) {
    return Optional<ResumptionRequest>(*request_it);
  }
  return Optional<ResumptionRequest>::OptionalEmpty::EMPTY;
}

void ResumptionDataProcessor::DeleteCommands(ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);

  auto failed_requests = GetAllFailedRequests(
      application->app_id(), resumption_status_, resumption_status_lock_);

  auto is_vr_command_failed = [](const ResumptionRequest& failed_command) {
    return failed_command.message[strings::msg_params].keyExists(
        strings::vr_commands);
  };

  auto accessor = application->commands_map();
  const auto& commands_map = accessor.GetData();

  for (const auto& cmd : commands_map) {
    auto failed_command =
        FindCommandResumptionRequest(cmd.first, failed_requests);

    if (!failed_command || (!is_vr_command_failed(*failed_command))) {
      auto delete_VR_command_msg = MessageHelper::CreateDeleteVRCommandRequest(
          cmd.second,
          application,
          application_manager_.GetNextHMICorrelationID());
      application_manager_.GetRPCService().ManageHMICommand(
          delete_VR_command_msg);
    }
    if (!failed_command || (is_vr_command_failed(*failed_command))) {
      auto delete_UI_command_msg = MessageHelper::CreateDeleteUICommandRequest(
          cmd.second,
          application->app_id(),
          application_manager_.GetNextHMICorrelationID());
      application_manager_.GetRPCService().ManageHMICommand(
          delete_UI_command_msg);
    }

    application->RemoveCommand(cmd.first);
    application->help_prompt_manager().OnVrCommandDeleted(cmd.first, true);
  }
}

void ResumptionDataProcessor::AddChoicesets(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);
  if (!saved_app.keyExists(strings::application_choice_sets)) {
    LOG4CXX_ERROR(logger_, "There is no any choicesets");
    return;
  }

  const smart_objects::SmartObject& app_choice_sets =
      saved_app[strings::application_choice_sets];

  for (size_t i = 0; i < app_choice_sets.length(); ++i) {
    const smart_objects::SmartObject& choice_set = app_choice_sets[i];
    const int32_t choice_set_id =
        choice_set[strings::interaction_choice_set_id].asInt();
    application->AddChoiceSet(choice_set_id, choice_set);
  }

  ProcessMessagesToHMI(MessageHelper::CreateAddVRCommandRequestFromChoiceToHMI(
      application, application_manager_));
}

utils::Optional<ResumptionRequest> FindResumptionChoiceSetRequest(
    uint32_t command_id, std::vector<ResumptionRequest>& requests) {
  using namespace utils;

  auto request_it =
      std::find_if(requests.begin(),
                   requests.end(),
                   [command_id](const ResumptionRequest& request) {
                     auto& msg_params = request.message[strings::msg_params];
                     if (msg_params.keyExists(strings::cmd_id) &&
                         (msg_params[strings::type] ==
                          hmi_apis::Common_VRCommandType::Choice)) {
                       uint32_t cmd_id = msg_params[strings::cmd_id].asUInt();
                       return cmd_id == command_id;
                     }
                     return false;
                   });
  if (requests.end() != request_it) {
    return Optional<ResumptionRequest>(*request_it);
  }
  return Optional<ResumptionRequest>::OptionalEmpty::EMPTY;
}

void ResumptionDataProcessor::DeleteChoicesets(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);

  auto failed_requests = GetAllFailedRequests(
      application->app_id(), resumption_status_, resumption_status_lock_);

  auto accessor = application->choice_set_map();
  const auto& choices = accessor.GetData();
  for (const auto& choice : choices) {
    auto failed_choice_set =
        FindResumptionChoiceSetRequest(choice.first, failed_requests);
    if (!failed_choice_set) {
      MessageHelper::SendDeleteChoiceSetRequest(
          choice.second, application, application_manager_);
    }
    application->RemoveChoiceSet(choice.first);
  }
}

void ResumptionDataProcessor::SetGlobalProperties(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);
  if (!saved_app.keyExists(strings::application_global_properties)) {
    LOG4CXX_DEBUG(logger_,
                  "application_global_properties section is not exists");
    return;
  }

  const smart_objects::SmartObject& properties_so =
      saved_app[strings::application_global_properties];
  application->load_global_properties(properties_so);

  ProcessMessagesToHMI(MessageHelper::CreateGlobalPropertiesRequestsToHMI(
      application, application_manager_));
}

void ResumptionDataProcessor::DeleteGlobalProperties(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);
  const uint32_t app_id = application->app_id();
  const auto result =
      application_manager_.ResetAllApplicationGlobalProperties(app_id);

  resumption_status_lock_.AcquireForReading();
  std::vector<ResumptionRequest> requests;
  if (resumption_status_.find(app_id) != resumption_status_.end()) {
    requests = resumption_status_[app_id].successful_requests;
  }
  resumption_status_lock_.Release();

  auto check_if_successful =
      [requests](hmi_apis::FunctionID::eType function_id) {
        for (auto& resumption_request : requests) {
          auto request_func =
              resumption_request.message[strings::params][strings::function_id]
                  .asInt();
          if (request_func == function_id) {
            return true;
          }
        }
        return false;
      };

  if (result.HasUIPropertiesReset() &&
      check_if_successful(hmi_apis::FunctionID::UI_SetGlobalProperties)) {
    smart_objects::SmartObjectSPtr msg_params =
        MessageHelper::CreateUIResetGlobalPropertiesRequest(result,
                                                            application);
    auto msg = MessageHelper::CreateMessageForHMI(
        hmi_apis::messageType::request,
        application_manager_.GetNextHMICorrelationID());
    (*msg)[strings::params][strings::function_id] =
        hmi_apis::FunctionID::UI_SetGlobalProperties;
    (*msg)[strings::msg_params] = *msg_params;
    ProcessMessageToHMI(msg, false);
  }

  if (result.HasTTSPropertiesReset() &&
      check_if_successful(hmi_apis::FunctionID::TTS_SetGlobalProperties)) {
    smart_objects::SmartObjectSPtr msg_params =
        MessageHelper::CreateTTSResetGlobalPropertiesRequest(result,
                                                             application);
    auto msg = MessageHelper::CreateMessageForHMI(
        hmi_apis::messageType::request,
        application_manager_.GetNextHMICorrelationID());
    (*msg)[strings::params][strings::function_id] =
        hmi_apis::FunctionID::TTS_SetGlobalProperties;

    (*msg)[strings::msg_params] = *msg_params;
    ProcessMessageToHMI(msg, false);
  }
}

void ResumptionDataProcessor::AddSubscriptions(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);

  AddButtonsSubscriptions(application, saved_app);
  AddPluginsSubscriptions(application, saved_app);
}

void ResumptionDataProcessor::AddButtonsSubscriptions(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);

  if (!saved_app.keyExists(strings::application_subscriptions)) {
    LOG4CXX_DEBUG(logger_, "application_subscriptions section is not exists");
    return;
  }

  const smart_objects::SmartObject& subscriptions =
      saved_app[strings::application_subscriptions];

  if (subscriptions.keyExists(strings::application_buttons)) {
    const smart_objects::SmartObject& subscriptions_buttons =
        subscriptions[strings::application_buttons];
    mobile_apis::ButtonName::eType btn;
    for (size_t i = 0; i < subscriptions_buttons.length(); ++i) {
      btn = static_cast<mobile_apis::ButtonName::eType>(
          (subscriptions_buttons[i]).asInt());
      application->SubscribeToButton(btn);
    }

    ButtonSubscriptions button_subscriptions =
        GetButtonSubscriptionsToResume(application);

    ProcessMessagesToHMI(
        MessageHelper::CreateOnButtonSubscriptionNotificationsForApp(
            application, application_manager_, button_subscriptions));
  }
}

ButtonSubscriptions ResumptionDataProcessor::GetButtonSubscriptionsToResume(
    ApplicationSharedPtr application) const {
  ButtonSubscriptions button_subscriptions =
      application->SubscribedButtons().GetData();
  auto it = button_subscriptions.find(mobile_apis::ButtonName::CUSTOM_BUTTON);

  if (it != button_subscriptions.end()) {
    button_subscriptions.erase(it);
  }

  return button_subscriptions;
}

void ResumptionDataProcessor::AddPluginsSubscriptions(
    ApplicationSharedPtr application,
    const smart_objects::SmartObject& saved_app) {
  LOG4CXX_AUTO_TRACE(logger_);

  for (auto& extension : application->Extensions()) {
    extension->ProcessResumption(
        saved_app,
        [this](const int32_t app_id, const ResumptionRequest request) {
          this->SubscribeToResponse(app_id, request);
        });
  }
}

void ResumptionDataProcessor::DeleteSubscriptions(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);
  DeleteButtonsSubscriptions(application);
  DeletePluginsSubscriptions(application);
}

void ResumptionDataProcessor::DeleteButtonsSubscriptions(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);
  const ButtonSubscriptions button_subscriptions =
      application->SubscribedButtons().GetData();
  for (auto& btn : button_subscriptions) {
    const auto hmi_btn = static_cast<hmi_apis::Common_ButtonName::eType>(btn);
    if (hmi_apis::Common_ButtonName::CUSTOM_BUTTON == hmi_btn) {
      continue;
    }
    auto notification = MessageHelper::CreateOnButtonSubscriptionNotification(
        application->hmi_app_id(), hmi_btn, false);
    // is_subscribed = false
    ProcessMessageToHMI(notification, false);
    application->UnsubscribeFromButton(btn);
  }
}

void ResumptionDataProcessor::DeleteWindowsSubscriptions(
    ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);

  const auto window_ids = application->GetWindowIds();
  for (const auto& window_id : window_ids) {
    const auto hmi_state = application->CurrentHmiState(window_id);
    if (mobile_apis::WindowType::WIDGET != hmi_state->window_type()) {
      continue;
    }

    LOG4CXX_DEBUG(logger_, "Reverting CreateWindow for: " << window_id);

    auto delete_request = MessageHelper::CreateUIDeleteWindowRequestToHMI(
        application, application_manager_, window_id);
    const bool subscribe_on_request_events = false;
    ProcessMessageToHMI(delete_request, subscribe_on_request_events);

    application->RemoveWindowInfo(window_id);
    application->RemoveHMIState(window_id,
                                app_mngr::HmiState::StateID::STATE_ID_REGULAR);
    application->remove_window_capability(window_id);
  }
}

void ResumptionDataProcessor::DeletePluginsSubscriptions(
    application_manager::ApplicationSharedPtr application) {
  LOG4CXX_AUTO_TRACE(logger_);

  resumption_status_lock_.AcquireForReading();
  auto it = resumption_status_.find(application->app_id());
  if (it == resumption_status_.end()) {
    resumption_status_lock_.Release();
    return;
  }

  const ApplicationResumptionStatus& status = it->second;
  smart_objects::SmartObject extension_subscriptions;
  for (auto ivi : status.successful_vehicle_data_subscriptions_) {
    LOG4CXX_DEBUG(logger_, "ivi " << ivi << " should be deleted");
    extension_subscriptions[ivi] = true;
  }
  resumption_status_lock_.Release();

  for (auto& extension : application->Extensions()) {
    extension->RevertResumption(extension_subscriptions);
  }
}

bool IsResponseSuccessful(const smart_objects::SmartObject& response) {
  if (!response[strings::params].keyExists(strings::error_msg)) {
    return true;
  }
  return false;
}

void ResumptionDataProcessor::CheckVehicleDataResponse(
    const smart_objects::SmartObject& request,
    const smart_objects::SmartObject& response,
    ApplicationResumptionStatus& status) {
  LOG4CXX_AUTO_TRACE(logger_);
  const auto request_keys = request[strings::msg_params].enumerate();

  if (!IsResponseSuccessful(response)) {
    LOG4CXX_TRACE(logger_, "Vehicle data request not succesfull");
    for (const auto key : request_keys) {
      status.unsuccessful_vehicle_data_subscriptions_.push_back(key);
    }
    return;
  }

  const auto& response_params = response[strings::msg_params];
  const auto response_keys = response_params.enumerate();
  for (auto& ivi : request_keys) {
    const auto it = response_keys.find(ivi);
    const auto kSuccess = hmi_apis::Common_VehicleDataResultCode::VDRC_SUCCESS;
    const auto vd_result_code =
        (response_keys.end() == it)
            ? kSuccess
            : response_params[ivi][strings::result_code].asInt();
    if (kSuccess != vd_result_code) {
      LOG4CXX_TRACE(logger_,
                    "ivi " << ivi << " was NOT successfuly subscribed");

      status.unsuccessful_vehicle_data_subscriptions_.push_back(ivi);
    } else {
      LOG4CXX_TRACE(logger_, "ivi " << ivi << " was successfuly subscribed");
      status.successful_vehicle_data_subscriptions_.push_back(ivi);
    }
  }
}

void ResumptionDataProcessor::CheckCreateWindowResponse(
    const smart_objects::SmartObject& request,
    const smart_objects::SmartObject& response) const {
  LOG4CXX_AUTO_TRACE(logger_);
  const auto correlation_id =
      response[strings::params][strings::correlation_id].asInt();

  const auto& msg_params = request[strings::msg_params];
  const auto app_id = msg_params[strings::app_id].asInt();

  auto application = application_manager_.application(app_id);
  if (!application) {
    LOG4CXX_ERROR(logger_,
                  "Application is not registered by hmi id: " << app_id);
    return;
  }

  const auto window_id = msg_params[strings::window_id].asInt();
  if (!IsResponseSuccessful(response)) {
    LOG4CXX_ERROR(logger_,
                  "UI_CreateWindow for correlation id: " << correlation_id
                                                         << " has failed");
    auto& builder = application->display_capabilities_builder();
    builder.ResetDisplayCapabilities();
    return;
  }

  smart_objects::SmartObject window_info(smart_objects::SmartType_Map);
  auto fill_optional_param = [&window_info,
                              &msg_params](const std::string& key) {
    if (msg_params.keyExists(key)) {
      window_info[key] = msg_params[key].asString();
    }
  };
  fill_optional_param(strings::associated_service_type);
  fill_optional_param(strings::duplicate_updates_from_window_id);

  const auto window_name = msg_params[strings::window_name].asString();
  window_info[strings::window_name] = window_name;
  application->SetWindowInfo(window_id, window_info);

  const auto window_type = static_cast<mobile_apis::WindowType::eType>(
      msg_params[strings::window_type].asInt());

  // State should be initialized with INVALID_ENUM value to let state
  // controller trigger OnHmiStatus notifiation sending
  auto initial_state = application_manager_.CreateRegularState(
      application,
      window_type,
      mobile_apis::HMILevel::INVALID_ENUM,
      mobile_apis::AudioStreamingState::INVALID_ENUM,
      mobile_apis::VideoStreamingState::INVALID_ENUM,
      mobile_apis::SystemContext::INVALID_ENUM);
  application->SetInitialState(window_id, window_name, initial_state);

  // Default HMI level for all windows except the main one is always NONE
  application_manager_.state_controller().OnAppWindowAdded(
      application, window_id, window_type, mobile_apis::HMILevel::HMI_NONE);
}

}  // namespace resumption
