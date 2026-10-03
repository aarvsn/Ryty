#include "prx/libScePlayerInvitationDialog/libScePlayerInvitationDialog.h"

#include <mutex>
#include "prx/libc/include/general/LogMacros.hpp"

namespace {

std::mutex g_dialog_mutex;
PlayerInvitationDialogStatus g_dialog_status = PlayerInvitationDialogStatus::None;

std::int32_t status() {
    return static_cast<std::int32_t>(g_dialog_status);
}

}

extern "C" {

std::int32_t APS5_VABI scePlayerInvitationDialogInitialize(void) {
    std::lock_guard lock(g_dialog_mutex);
    APS5_LOG_OUT("status=%d", status());
    if (g_dialog_status == PlayerInvitationDialogStatus::None) {
        g_dialog_status = PlayerInvitationDialogStatus::Initialized;
    }
    return 0;
}

std::int32_t APS5_VABI scePlayerInvitationDialogOpen(const ScePlayerInvitationDialogParam* param) {
    std::lock_guard lock(g_dialog_mutex);
    APS5_LOG_OUT("param=%p status=%d", static_cast<const void*>(param), status());
    if (g_dialog_status == PlayerInvitationDialogStatus::None) {
        g_dialog_status = PlayerInvitationDialogStatus::Initialized;
    }
    g_dialog_status = PlayerInvitationDialogStatus::Running;
    return 0;
}

std::int32_t APS5_VABI scePlayerInvitationDialogUpdateStatus(void) {
    std::lock_guard lock(g_dialog_mutex);
    APS5_LOG_OUT("status=%d", status());
    if (g_dialog_status == PlayerInvitationDialogStatus::Running) {
        g_dialog_status = PlayerInvitationDialogStatus::Finished;
    }
    return status();
}

std::int32_t APS5_VABI scePlayerInvitationDialogGetStatus(void) {
    std::lock_guard lock(g_dialog_mutex);
    APS5_LOG_OUT("status=%d", status());
    return status();
}

std::int32_t APS5_VABI scePlayerInvitationDialogClose(void) {
    std::lock_guard lock(g_dialog_mutex);
    APS5_LOG_OUT("status=%d", status());
    if (g_dialog_status == PlayerInvitationDialogStatus::Running) {
        g_dialog_status = PlayerInvitationDialogStatus::Finished;
    }
    return 0;
}

std::int32_t APS5_VABI scePlayerInvitationDialogTerminate(void) {
    std::lock_guard lock(g_dialog_mutex);
    APS5_LOG_OUT("status=%d", status());
    g_dialog_status = PlayerInvitationDialogStatus::None;
    return 0;
}

}
