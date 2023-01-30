#include "state_management.h"
#include <iostream>
#include "contains.h"

namespace ara::sm {

    void StateManagement::Work() {
        std::cout << "Starting work" << std::endl;
        /** @brief Start-up Sequence - Figure 7.1 SWS_StateManagement */
        if (stateClient != nullptr) {
            if (stateClient->GetInitialMachineStateTransitionResult() == ErrorType::kSuccess) {
                stateClient->SmSetState(FunctionGroupStateType::On);
            }
            /* Restart machine if Initial MachineState Transition not successful */
            else stateClient->MachineSetState(MachineStateType::Restart);
        }

        while (!killFlag) {
            if (internalState == FunctionGroupStateType::Off) {
                Off_Actions();
            }
            else if (internalState == FunctionGroupStateType::On) {
                On_Actions();
            }
            else if (internalState == FunctionGroupStateType::Update) {
                UpdateRequestHandlerUpdate();
            }
            UpdateSMState();
        }
        std::cout << "Finished work" << std::endl;
    }

    void StateManagement::On_Actions() {
        UpdateRequestHandlerOn();
        TriggerInHandler();
        TriggerInOutHandler();
    }

    void StateManagement::Off_Actions() {
        UpdateRequestHandlerOff();
        TriggerInHandler();
        TriggerInOutHandler();
    }

    void StateManagement::TriggerInHandler() {
        if (triggerIn.IsTrigger()) {
            if (stateClient != nullptr) {
                stateClient->SmSetState(triggerInOut.GetDesiredState());
            }
            triggerIn.DiscardTrigger();
        }
    }

    void StateManagement::TriggerInOutHandler() {
        if (triggerInOut.IsTrigger()) {

            if (stateClient != nullptr) {
                stateClient->SmSetState(triggerInOut.GetDesiredState());
                internalState = stateClient->SmGetState();
            }
            triggerInOut.SetNotifier(ara::sm::ErrorType::kSuccess, internalState);
            triggerInOut.DiscardTrigger();
        }
    }

    void StateManagement::UpdateSMState() {
        if (stateClient != nullptr) {
            internalState = stateClient->SmGetState();
        }
        if (executionClient != nullptr) {
            executionClient->ReportApplicationState(internalState);
        }
        triggerOut.SetNotifier(internalState);
    }


    void StateManagement::UpdateRequestHandlerOff() {
        com::UpdateRequest::RequestMsg requestMsg =  myUpdateRequest.GetRequestMsg();
        if (requestMsg.status) {
            ErrorType newUpdateStatus;
            switch (requestMsg.type) {
                case com::UpdateRequest::RequestType::kRequestUpdateSession:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kResetMachine:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kStopUpdateSession:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kPrepareUpdate:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kVerifyUpdate:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kPrepareRollback:
                    newUpdateStatus = ErrorType::kRejected;
                    break;
                default:
                    newUpdateStatus = ErrorType::kInvalidValue;
                    break;
            }
            myUpdateRequest.SendResponse(newUpdateStatus);
        }
    }

    void StateManagement::UpdateRequestHandlerOn() {
        com::UpdateRequest::RequestMsg requestMsg =  myUpdateRequest.GetRequestMsg();
        if (requestMsg.status) {
            ErrorType newUpdateStatus;
            switch (requestMsg.type) {
                case com::UpdateRequest::RequestType::kRequestUpdateSession:
                    exec::ExecErrc setStateError;
                    setStateError = stateClient->SmSetState(FunctionGroupStateType::Update);
                    newUpdateStatus = (setStateError == exec::ExecErrc::kSuccess) ?
                                      ErrorType::kSuccess : ErrorType::kFailed;
                    break;
                case com::UpdateRequest::RequestType::kResetMachine:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kStopUpdateSession:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kPrepareUpdate:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kVerifyUpdate:
                    /* intentional fallback */
                case com::UpdateRequest::RequestType::kPrepareRollback:
                    newUpdateStatus = ErrorType::kRejected;
                    break;
                default:
                    newUpdateStatus = ErrorType::kInvalidValue;
                    break;
            }
            myUpdateRequest.SendResponse(newUpdateStatus);
        }
    }

    void StateManagement::UpdateRequestHandlerUpdate() {
        com::UpdateRequest::RequestMsg requestMsg =  myUpdateRequest.GetRequestMsg();
        if (requestMsg.status) {
            ErrorType newUpdateStatus;
            exec::ExecErrc setStateError;
//            FunctionGroupListType fgList;
            switch (requestMsg.type) {
                case com::UpdateRequest::RequestType::kRequestUpdateSession:
                    newUpdateStatus = ErrorType::kNotAllowedMultipleUpdateSessions;
                    break;
                case com::UpdateRequest::RequestType::kStopUpdateSession:
                    if (stateClient != nullptr) {
                        setStateError = stateClient->SmSetState(FunctionGroupStateType::On);
                        newUpdateStatus = (setStateError == exec::ExecErrc::kSuccess) ?
                                          ErrorType::kSuccess : ErrorType::kFailed;
                    }
                    else {
                        newUpdateStatus = ErrorType::kFailed;
                    }

                    break;
                case com::UpdateRequest::RequestType::kResetMachine:
                        // persist all information within the machine before reset
                        if (stateClient != nullptr) {
                            setStateError = stateClient->MachineSetState(MachineStateType::Restart);
                            newUpdateStatus = (setStateError == exec::ExecErrc::kSuccess) ?
                                              ErrorType::kSuccess : ErrorType::kFailed;
                        }
                        else {
                            newUpdateStatus = ErrorType::kFailed;
                        }
                    break;
                case com::UpdateRequest::RequestType::kPrepareUpdate:
                    if (CheckFunctionGroupList(myUpdateRequest.GetFunctionGroupList())) {
                        newUpdateStatus = SetAllFunctionGroupsState(FunctionGroupStateType::Update);
                    }
                    else {
                        newUpdateStatus = ErrorType::kFailed;
                    }
                    break;
                case com::UpdateRequest::RequestType::kVerifyUpdate:
                    if (myUpdateRequest.GetUpdateStatus() == ErrorType::kSuccess) {
                        if (CheckFunctionGroupList(myUpdateRequest.GetFunctionGroupList())) {
                            // verify update
                            newUpdateStatus = ErrorType::kSuccess;
                        } else {
                            newUpdateStatus = ErrorType::kFailed;
                        }
                    }
                    else {
                        newUpdateStatus = ErrorType::kRejected;
                    }
                    break;
                case com::UpdateRequest::RequestType::kPrepareRollback:
                    if (myUpdateRequest.GetUpdateStatus() == ErrorType::kFailed) {
                        if (CheckFunctionGroupList(myUpdateRequest.GetFunctionGroupList())) {
                            //todo prepare rollback
                            newUpdateStatus = ErrorType::kSuccess;
                        } else {
                            newUpdateStatus = ErrorType::kFailed;
                        }
                    }
                    else {
                        newUpdateStatus = ErrorType::kRejected;
                    }
            }
            myUpdateRequest.SendResponse(newUpdateStatus);
        }
    }

    bool StateManagement::CheckFunctionGroupList(FunctionGroupListType const &fgList) {

        std::vector<std::string> functionGroupListVec = std::ref(functionGroupList);
        return std::all_of(fgList.begin(), fgList.end(),
                           [&](const std::string &functionGroup)
                            { return VecContainsElement(functionGroupListVec, functionGroup); } );
    }

    void StateManagement::Kill() {
        killFlag = true;
    }

    StateManagement::StateManagement(exec::StateClient* sc, exec::ExecutionClient* ec) :
        myUpdateRequest{com::UpdateRequest()},
        myNetworkHandle{com::NetworkHandle()},
        triggerOut{com::TriggerOut()},
        triggerIn{com::TriggerIn()},
        triggerInOut{com::TriggerInOut()},
        internalState{FunctionGroupStateType::Off},
        stateClient{sc},
        executionClient{ec},
        killFlag{false} {}

    ErrorType StateManagement::SetAllFunctionGroupsState(FunctionGroupStateType fgState) {
        if (stateClient != nullptr) {
            for (const std::string& fgName: functionGroupList) {
                auto setStateError = stateClient->SetState(fgName, fgState);
                if (setStateError != exec::ExecErrc::kSuccess) {
                    return ErrorType::kFailed;
                }
            }
        }
        else
        {
            return ErrorType::kFailed;
        }
        return ErrorType::kSuccess;
    }
}
