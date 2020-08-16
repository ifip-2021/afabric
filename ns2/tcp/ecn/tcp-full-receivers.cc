#include "tcp-full-receivers.h"

#include "tcp-full.h"

BaseReceiverCETracker::BaseReceiverCETracker() : recent_ce_{false} {}

auto BaseReceiverCETracker::need_ce_echo() const -> bool { return recent_ce_; }

auto BaseReceiverCETracker::has_pending() const -> bool { return false; }

void BaseReceiverCETracker::on_syn_ack(hdr_flags * fh) {
    if (fh->ecnecho() && !fh->cong_action() && fh->ce()) {
        recent_ce_ = true;
    }
}

void BaseReceiverCETracker::flip_ce() { recent_ce_ = !recent_ce_; };

void BaseReceiverCETracker::ce_set_packet_echo(hdr_flags * fh) const {
    fh->ecnecho() = 1;
}

void NormalReceiverCETracker::on_new_packet(
        Packet * packet, [[maybe_unused]] bool has_data) {
    auto const fh = hdr_flags::access(packet);
    if (fh->ce() && fh->ect()) {
        update_ce();
    } else if (fh->cwr()) {
        update_no_ce();
    }
}

void NormalReceiverCETracker::update_ce() {
    // no CWR from peer yet... arrange to
    // keep sending ECNECHO
    recent_ce_ = true;
}

void NormalReceiverCETracker::update_no_ce() {
    // got CWR response from peer.. stop
    // sending ECNECHO bits
    recent_ce_ = false;
}

AFabricReceiverCETracker::AFabricReceiverCETracker()
    : is_low_prio_{true}
    , ce_transition_{false}
{}

void AFabricReceiverCETracker::on_new_packet(Packet * packet, bool has_data) {
    auto const fh = hdr_flags::access(packet);
    if (fh->ce() && fh->ect()) {
        update_ce(hdr_ip::access(packet)->prio());
    } else if (has_data && !fh->ce() && fh->ect()) {
        update_no_ce(hdr_ip::access(packet)->prio());
    }
}

auto AFabricReceiverCETracker::has_pending() const -> bool { 
    return ce_transition_; 
}

void AFabricReceiverCETracker::ce_set_packet_echo(hdr_flags * fh) const {
    fh->ecnecho() = 1;
    fh->ecn_low_prio() = is_low_prio_;
}

void AFabricReceiverCETracker::update_ce(int prio) {
    update(true, prio);
}

void AFabricReceiverCETracker::update_no_ce(int prio) {
    update(false, prio);
}

void AFabricReceiverCETracker::update(bool new_ce, int prio) {
    ce_transition_ = is_transition(new_ce, prio);
    update_priority(prio);
    recent_ce_ = new_ce && is_priority_valid(prio);
}

auto AFabricReceiverCETracker::is_transition(bool new_ce, int prio) const -> bool {
    return (is_priority_valid(prio) && recent_ce_ != new_ce) 
        || (is_low_prio_ && prio != FullTcpAgent::get_lowest_priority());
}

void AFabricReceiverCETracker::update_priority(int prio) {
    is_low_prio_ = is_low_prio_ && prio == FullTcpAgent::get_lowest_priority();
}

auto AFabricReceiverCETracker::is_priority_valid(int prio) const -> bool {
    return is_low_prio_ || prio != FullTcpAgent::get_lowest_priority();
}

EcnhatReceiverCETracker::EcnhatReceiverCETracker() : ce_transition_{false} {}

void EcnhatReceiverCETracker::on_new_packet(Packet * packet, bool has_data) {
    auto const fh = hdr_flags::access(packet);
    if (fh->ce() && fh->ect()) {
        update_ce();
    } else if (has_data && !fh->ce() && fh->ect()) {
        update_no_ce();
    }
}

auto EcnhatReceiverCETracker::has_pending() const -> bool { 
    return ce_transition_; }

void EcnhatReceiverCETracker::update_ce() {
    update(true);
}

void EcnhatReceiverCETracker::update_no_ce() {
    update(false);
}

void EcnhatReceiverCETracker::update(bool new_ce) {
    ce_transition_ = is_transition(new_ce);
    recent_ce_ = new_ce;
}

auto EcnhatReceiverCETracker::is_transition(bool new_ce) const -> bool {
    return recent_ce_ != new_ce;
}
