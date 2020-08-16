#ifndef ns_tcp_ecn_senders_h
#define ns_tcp_ecn_senders_h

#include "tcp.h"
#include "ecn/common.h"

class NormalSenderCETracker : public SenderCETracker {
public:
    explicit NormalSenderCETracker(TcpAgent * agent);
    void on_receive([[maybe_unused]] Packet * pkt) override {}
    void on_foutput([[maybe_unused]] int highest) override {}
    void ecn_slowdown() override;

    auto opencwnd_multiplier() -> double override;
    auto is_opencwnd_adjustment_enabled() const -> bool override;

    auto should_slowdown_on_dup_ack() const -> bool override;

    auto always_report_ect() const -> bool override;

protected:
    auto agent() -> TcpAgent&;

private:
    TcpAgent * const agent_;
};

class EcnhatSenderCETracker : public SenderCETracker {
public:
    explicit EcnhatSenderCETracker(TcpAgent::EcnProcessor * procesor);
    void on_timeout() override;
    void on_foutput(int highest) override;
    void on_receive(Packet * pkt) override;
    auto delay_bind_dispatch(
            const char *varName, const char *localName, TclObject *tracer
        ) -> bool override;
    void delay_bind_init_all() override;
    auto get_dctcp_alpha() const -> double override;
    void ecn_slowdown() override;

    auto is_opencwnd_adjustment_enabled() const -> bool override;
    auto opencwnd_multiplier() -> double override;

    auto should_slowdown_on_dup_ack() const -> bool override;

    auto always_report_ect() const -> bool override;

private:
    void update_ecnhat_alpha(Packet * pkt);

protected:
    auto processor() -> TcpAgent::EcnProcessor&;
    auto processor() const -> TcpAgent::EcnProcessor const&;
    auto agent() -> TcpAgent&;
    auto agent() const -> TcpAgent const&;

private:
    /* Mohammad: added for Ecn-Hat */
    int ecnhat_recalc_seq_;
    int ecnhat_maxseq_;
    int ecnhat_num_marked_;
    int ecnhat_smooth_alpha_;
    double ecnhat_g_;
    double ecnhat_alpha_;
    int ecnhat_total;

    TcpAgent::EcnProcessor * const processor_; 
};

class TcpFriendlyEcnhatSenderCETracker : public EcnhatSenderCETracker {
    using super = EcnhatSenderCETracker;
public:
    explicit TcpFriendlyEcnhatSenderCETracker(TcpAgent::EcnProcessor * procesor);
    void ecn_slowdown() override;

    auto opencwnd_multiplier() -> double override;
    auto is_opencwnd_adjustment_enabled() const -> bool override;
private:
    double ecnhat_tcp_friendly_increase_;
    double target_wnd_;
};

#endif // ns_tcp_ecn_senders_h
