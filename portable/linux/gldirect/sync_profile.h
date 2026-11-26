#pragma once
#include <cstdint>
#include <ostream>

namespace pgr::gldirect {
enum class SubmissionReason { Present, Flush, Fence, ResourceBusy, ResourceWait, VisibilityReport, Readback };
inline const char* ReasonName(SubmissionReason reason) {
    switch(reason) {
    case SubmissionReason::Present: return "present";
    case SubmissionReason::Flush: return "flush";
    case SubmissionReason::Fence: return "fence";
    case SubmissionReason::ResourceBusy: return "resource_busy";
    case SubmissionReason::ResourceWait: return "resource_wait";
    case SubmissionReason::VisibilityReport: return "visibility_report";
    case SubmissionReason::Readback: return "readback";
    }
    return "unknown";
}
// EN: Producer events are drained into ordered submissions. Waits finishing
// after a flush belong to the next interval; clocks are disabled by default.
// PT-BR: Eventos seguem nas submissões ordenadas. Esperas após flush pertencem
// ao intervalo seguinte; os relógios ficam desativados por padrão.
struct SyncProfile {
    std::uint64_t visibility_reports=0, visibility_clears=0, sync_calls=0,
        sync_late_calls=0, flush_calls=0, resource_wait_calls=0,
        fence_wait_calls=0, gpu_busy_poll_count=0, readback_waits=0;
    double visibility_report_wait_ms=0, readback_wait_ms=0,
        resource_wait_ms=0, fence_wait_ms=0;
    void Add(const SyncProfile& p) {
        visibility_reports+=p.visibility_reports; visibility_clears+=p.visibility_clears;
        sync_calls+=p.sync_calls; sync_late_calls+=p.sync_late_calls;
        flush_calls+=p.flush_calls; resource_wait_calls+=p.resource_wait_calls;
        fence_wait_calls+=p.fence_wait_calls; gpu_busy_poll_count+=p.gpu_busy_poll_count;
        readback_waits+=p.readback_waits;
        visibility_report_wait_ms+=p.visibility_report_wait_ms; readback_wait_ms+=p.readback_wait_ms;
        resource_wait_ms+=p.resource_wait_ms; fence_wait_ms+=p.fence_wait_ms;
    }
    static const char* Columns() {
        return "visibility_reports,visibility_clears,sync_calls,sync_late_calls,flush_calls,resource_wait_calls,fence_wait_calls,gpu_busy_poll_count,readback_waits,visibility_report_wait_ms,readback_wait_ms,resource_wait_ms,fence_wait_ms";
    }
    void Csv(std::ostream& out) const {
        out<<visibility_reports<<','<<visibility_clears<<','<<sync_calls<<','<<sync_late_calls<<','
           <<flush_calls<<','<<resource_wait_calls<<','<<fence_wait_calls<<','<<gpu_busy_poll_count<<','
           <<readback_waits<<','<<visibility_report_wait_ms<<','<<readback_wait_ms<<','
           <<resource_wait_ms<<','<<fence_wait_ms;
    }
};
}
