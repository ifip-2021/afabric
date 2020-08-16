#ifndef ns_tcp_ecn_common_h
#define ns_tcp_ecn_common_h

#include "flags.h"
#include "packet.h"

struct SenderCETracker {
    virtual void on_receive(Packet * pkt) = 0;
    virtual void on_foutput(int highest) = 0;

    virtual void on_timeout() { }

    virtual void delay_bind_init_all() { }

    virtual auto delay_bind_dispatch(
        const char *varName, const char *localName, TclObject *tracer) -> bool;

    virtual auto get_dctcp_alpha() const -> double;

    virtual void ecn_slowdown() = 0;;

    virtual auto is_opencwnd_adjustment_enabled() const -> bool = 0;
    virtual auto opencwnd_multiplier() -> double = 0;

    virtual auto should_slowdown_on_dup_ack() const -> bool = 0;

    virtual auto always_report_ect() const -> bool = 0;

	virtual auto is_ecn_active(Packet * pkt) const -> bool;

    virtual ~SenderCETracker() = default;
};

struct ReceiverCETracker {
    // Checks the presense of CE bit in the packet
    virtual void on_new_packet(Packet * packet, bool has_data) = 0;
    virtual void on_syn_ack(hdr_flags * fh) = 0;
    virtual auto need_ce_echo() const -> bool = 0;
    virtual auto has_pending() const -> bool = 0;
    virtual void ce_set_packet_echo(hdr_flags * fh) const = 0;
    // TODO: this is specific to ecnhat!
    virtual void flip_ce() = 0;
    virtual ~ReceiverCETracker() = default;
};

#endif // ns_tcp_ecn_common_h
