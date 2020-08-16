#ifndef ns_tcp_ecn_tcp_full_receivers_h
#define ns_tcp_ecn_tcp_full_receivers_h

#include "ecn/common.h"
#include "ip.h"

class BaseReceiverCETracker : public ReceiverCETracker {
public:
    BaseReceiverCETracker();

    auto need_ce_echo() const -> bool override;

    auto has_pending() const -> bool override;

    void on_syn_ack(hdr_flags * fh) override;

    void flip_ce() override;

    void ce_set_packet_echo(hdr_flags * fh) const override;

protected:
    bool recent_ce_;
};

class NormalReceiverCETracker : public BaseReceiverCETracker {
public:
    void on_new_packet(Packet * packet, bool has_data) override;
private:
    void update_ce();
    void update_no_ce();};

// TODO rethink it
class AFabricReceiverCETracker : public BaseReceiverCETracker {
public:
    AFabricReceiverCETracker();

    void on_new_packet(Packet * packet, bool has_data) override;

    auto has_pending() const -> bool override;

    void ce_set_packet_echo(hdr_flags * fh) const override;

private:
    void update_ce(int prio);
    void update_no_ce(int prio);
    void update(bool new_ce, int prio);

    auto is_transition(bool new_ce, int prio) const -> bool;
    void update_priority(int prio);
    auto is_priority_valid(int prio) const -> bool;

private:
    bool is_low_prio_;
    bool ce_transition_;
};

class EcnhatReceiverCETracker : public BaseReceiverCETracker {
public:
    EcnhatReceiverCETracker();

    void on_new_packet(Packet * packet, bool has_data) override;

    auto has_pending() const -> bool override;

private:
    void update_ce();
    void update_no_ce();
    void update(bool new_ce);

    auto is_transition(bool new_ce) const -> bool;

private:
    bool ce_transition_; /* Mohammad: was there a transition in
                   recent_ce by last ACK. for DCTCP receiver
                   state machine. */
};

#endif // ns_tcp_ecn_tcp_full_receivers_h
