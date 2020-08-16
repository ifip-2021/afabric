#include "tcp-full-senders.h"
#include "ecn/senders.h"
#include "tcp-full.h"

AfabricEcnhatSenderCETracker::AfabricEcnhatSenderCETracker(
        FullTcpAgent::EcnProcessor * processor)
    : EcnhatSenderCETracker(processor) 
{}

auto AfabricEcnhatSenderCETracker::agent() const -> FullTcpAgent const& {
    return static_cast<FullTcpAgent const &>(EcnhatSenderCETracker::agent());
}

auto AfabricEcnhatSenderCETracker::is_ecn_active(Packet * pkt) const -> bool {
    return super::is_ecn_active(pkt) 
        && (!hdr_flags::access(pkt)->ecn_low_prio() || 
                !agent().signal_on_empty_);
}
