#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HeapDiagnostics.hpp"

uint32_t Need_sceLibc = 1;

extern "C" {

    int APS5_VABI std_execute_once_nid_postfix(int* flag, int (*func)(void*, void*, void**), void* arg) {
        (void)flag;
        (void)func;
        (void)arg;
        NotImplemented_nid_no_patch(__func__);
        return 0;
    }

    void APS5_VABI LibcHeapGetTraceInfo_nid_postfix(LibcHeapInfo* info) {
        LibcHeapTraceInfo_nid_no_patch(info);
    }

    int APS5_VABI LibcHeapErrorReportForGame_nid_postfix(
        uint64_t msp, uint64_t ptr, uint64_t error,
        uint64_t arg3, uint64_t arg4, uint64_t arg5
    ) {
        (void)msp; (void)ptr; (void)error;
        (void)arg3; (void)arg4; (void)arg5;
        NotImplemented_nid_no_patch(__func__);
        return 0;
    }

APS5_EXPORT("Pu0Ecyk-7FU", libcUnknown_Pu0Ecyk_M7FU);
int APS5_VABI libcUnknown_Pu0Ecyk_M7FU() {
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

}
