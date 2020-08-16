#include "senders.h"
#include "tcp.h"

auto SenderCETracker::delay_bind_dispatch(
            [[maybe_unused]] const char *varName, 
            [[maybe_unused]] const char *localName, 
            [[maybe_unused]] TclObject *tracer) -> bool {
	return false;
}

auto SenderCETracker::get_dctcp_alpha() const -> double { return 1.0; }

auto SenderCETracker::is_ecn_active(Packet * pkt) const -> bool {
	return hdr_flags::access(pkt)->ecnecho();
}

NormalSenderCETracker::NormalSenderCETracker(TcpAgent * agent)
	: agent_{agent}
{}

void NormalSenderCETracker::ecn_slowdown() {
	agent().slowdown(TcpSlowdownFlags::CWND_HALF | TcpSlowdownFlags::SSTHRESH_HALF);
}

auto NormalSenderCETracker::opencwnd_multiplier() -> double {
	throw std::logic_error("Opencwnd adjustment is disabled!");
}

auto NormalSenderCETracker::is_opencwnd_adjustment_enabled() const -> bool {
	return false;
}

auto NormalSenderCETracker::should_slowdown_on_dup_ack() const -> bool {
	return false;
}
    
auto NormalSenderCETracker::always_report_ect() const -> bool {
	return false;
}

auto NormalSenderCETracker::agent() -> TcpAgent& {
    return *agent_;
}

EcnhatSenderCETracker::EcnhatSenderCETracker(TcpAgent::EcnProcessor * processor) 
    : ecnhat_recalc_seq_{0}
    , ecnhat_maxseq_{0}
    , ecnhat_num_marked_{0}
    , ecnhat_total{0}
    , processor_{processor} 
{
    ecnhat_smooth_alpha_ = true;
    ecnhat_alpha_ = 0.0;
    ecnhat_g_ = 0.125;
    if constexpr(std::is_base_of_v<TracedVar, TcpAgent::TracedInt>) {
        agent().bind("ecnhat_smooth_alpha_", &ecnhat_smooth_alpha_);
        agent().bind("ecnhat_alpha_", &ecnhat_alpha_);
        agent().bind("ecnhat_g_", &ecnhat_g_);
	}
}

auto EcnhatSenderCETracker::always_report_ect() const -> bool {
	return true;
}

void EcnhatSenderCETracker::ecn_slowdown() {
	agent().slowdown(TcpSlowdownFlags::CWND_ECNHAT | TcpSlowdownFlags::SSTHRESH_ECNHAT);
}

auto EcnhatSenderCETracker::opencwnd_multiplier() -> double {
	throw std::logic_error("Opencwnd adjustment is disabled!");
}

auto EcnhatSenderCETracker::should_slowdown_on_dup_ack() const -> bool {
	return true;
}

/*
 * Mohammad: Update ecnhat alpha based on the ecn bit in the received packet.
 *
 * This procedure is called only when ecnhat_ is 1.
 */
void EcnhatSenderCETracker::update_ecnhat_alpha(Packet *pkt)
{
	auto const ecnbit = is_ecn_active(pkt);

	auto const ackno = hdr_tcp::access(pkt)->ackno();

	if (!ecnhat_smooth_alpha_)
		ecnhat_alpha_ = (1 - ecnhat_g_) * ecnhat_alpha_ + ecnhat_g_ * ecnbit;
	else {
		int acked_bytes = ackno - agent().highest_ack_;
		if (acked_bytes <= 0)
			acked_bytes = agent().size_;
		//printf("size_ = %d, acked_bytes = %d\n",size_, acked_bytes);
		//ecnhat_total++;
		ecnhat_total += acked_bytes;
		if (ecnbit) {
			//ecnhat_num_marked++;
			ecnhat_num_marked_ += acked_bytes;
			processor_->ecnhat_beta_ = 1;
		}
		if (ackno > ecnhat_recalc_seq_) { //update roughly per-RTT
			double temp_alpha;
			ecnhat_recalc_seq_ = ecnhat_maxseq_;
			if (ecnhat_total > 0) {
				temp_alpha = ((double) ecnhat_num_marked_) / ecnhat_total;
			} else { 
				temp_alpha = 0.0;
			}

			//printf("%f %f %f %f\n", Scheduler::instance().clock(), (double) cwnd_, temp_alpha, ecnhat_alpha_);
			ecnhat_alpha_ = (1 - ecnhat_g_) * ecnhat_alpha_ + ecnhat_g_ * temp_alpha;
			ecnhat_num_marked_ = 0;
			ecnhat_total = 0;
		}
	}
}

void EcnhatSenderCETracker::on_foutput(int highest) {
    if (highest > ecnhat_maxseq_)
        ecnhat_maxseq_ = highest;
}

auto EcnhatSenderCETracker::processor() -> TcpAgent::EcnProcessor& {
    return const_cast<TcpAgent::EcnProcessor&>(
            const_cast<EcnhatSenderCETracker const *>(this)->processor());
}

auto EcnhatSenderCETracker::processor() const -> TcpAgent::EcnProcessor const& {
    return *processor_;
}

auto EcnhatSenderCETracker::agent() -> TcpAgent& {
    return const_cast<TcpAgent&>(
            const_cast<EcnhatSenderCETracker const *>(this)->agent());
}

auto EcnhatSenderCETracker::agent() const -> TcpAgent const & {
    return processor().agent();
}

TcpFriendlyEcnhatSenderCETracker::TcpFriendlyEcnhatSenderCETracker(
		TcpAgent::EcnProcessor * processor
	) : super{processor}, target_wnd_{0}
{}

auto TcpFriendlyEcnhatSenderCETracker::opencwnd_multiplier() -> double {
	ecnhat_tcp_friendly_increase_ = ((int(agent().t_srtt_) >> agent().T_SRTT_BITS)*agent().tcp_tick_ / 0.0004);
	return ecnhat_tcp_friendly_increase_;
}

auto TcpFriendlyEcnhatSenderCETracker::is_opencwnd_adjustment_enabled() const -> bool {
	return true;
}

void TcpFriendlyEcnhatSenderCETracker::ecn_slowdown() {
	target_wnd_ = agent().cwnd_;
	ecnhat_tcp_friendly_increase_ = 1.5/(2.0/get_dctcp_alpha() - 0.5);
	super::ecn_slowdown();
}
