#ifndef ns_tcp_ecn_tcp_full_senders_h
#define ns_tcp_ecn_tcp_full_senders_h

#include "ecn/senders.h"
#include "flags.h"
#include "tcp-full.h"

class AfabricEcnhatSenderCETracker : public EcnhatSenderCETracker {
    using super = EcnhatSenderCETracker;
public:
    explicit AfabricEcnhatSenderCETracker(FullTcpAgent::EcnProcessor * procesor);

    auto is_ecn_active(Packet * pkt) const -> bool override;

protected:
    auto agent() -> FullTcpAgent&;
    auto agent() const -> FullTcpAgent const&;

};

#endif // ns_tcp_ecn_tcp_full_senders_h
