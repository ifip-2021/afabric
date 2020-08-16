
#ifndef ns_tcp_full_h
#define ns_tcp_full_h

#include <memory>
#include <limits>
#include <common/ip.h>
#include "flags.h"
#include "tcp.h"
#include "rq.h"

/*
 * most of these defines are directly from
 * tcp_var.h or tcp_fsm.h in "real" TCP
 */


/*
 * these are used in 'tcp_flags_' member variable
 */

#define TF_ACKNOW       0x0001          /* ack peer immediately */
#define TF_DELACK       0x0002          /* ack, but try to delay it */
#define TF_NODELAY      0x0004          /* don't delay packets to coalesce */
#define TF_NOOPT        0x0008          /* don't use tcp options */
#define TF_SENTFIN      0x0010          /* have sent FIN */
#define	TF_RCVD_TSTMP	0x0100		/* timestamp rcv'd in SYN */
#define	TF_NEEDFIN	0x0800		/* send FIN (implicit state) */

/* these are simulator-specific */
#define	TF_NEEDCLOSE	0x10000		/* perform close on empty */

/*
 * these are used in state_ member variable
 */

enum class TcpState : int {
    CLOSED          = 0,       /* closed */
    LISTEN          = 1,       /* listening for connection */
    SYN_SENT        = 2,       /* active, have sent syn */
    SYN_RECEIVED    = 3,       /* have sent and received syn */
    ESTABLISHED     = 4,       /* established */
    CLOSE_WAIT      = 5,       /* rcvd fin, waiting for app close */
    FIN_WAIT_1      = 6,       /* have closed, sent fin */
    CLOSING         = 7,       /* closed xchd FIN; await FIN ACK */
    LAST_ACK        = 8,       /* had fin and close; await FIN ACK */
    FIN_WAIT_2      = 9,       /* have closed, fin is acked */
    NSTATES
};

inline auto is_state_valid(TcpState state) {
    return state >=TcpState{0} && (state < TcpState::NSTATES);
}

inline auto have_received_FIN(TcpState s) {
    return s == TcpState::CLOSING || s == TcpState::CLOSED || s == TcpState::CLOSE_WAIT;
}

inline auto have_received_SYN(TcpState s) {
    return static_cast<int>(s) >= static_cast<int>(TcpState::SYN_RECEIVED);
}

#define TCPIP_BASE_PKTSIZE      40      /* base TCP/IP header in real life */

/* bits for the tcp_flags field below */
/* from tcp.h in the "real" implementation */
/* RST and URG are not used in the simulator */

enum class TcpFlags : int {
    NONE = 0x0,
    FIN = 0x01,   /* FIN: closing a connection */
    SYN = 0x02,   /* SYN: starting a connection */
    PUSH = 0x08,  /* PUSH: used here to "deliver" data */
    ACK = 0x10,   /* ACK: ack number is valid */
    ECE = 0x40,   /* ECE: CE echo flag */
    CWR = 0x80    /* CWR: congestion window reduced */
};

inline void operator|=(TcpFlags &lhs, TcpFlags rhs) {
    reinterpret_cast<int&>(lhs) |= static_cast<int>(rhs);
}

inline void operator&=(TcpFlags &lhs, TcpFlags rhs) {
    reinterpret_cast<int&>(lhs) &= static_cast<int>(rhs);
}

inline TcpFlags operator|(TcpFlags lhs, TcpFlags rhs) {
    return TcpFlags{static_cast<int>(lhs) | static_cast<int>(rhs)};
}

inline TcpFlags operator~(TcpFlags flags) {
    return TcpFlags{~static_cast<int>(flags)};
}

inline TcpFlags operator&(TcpFlags lhs, TcpFlags rhs) {
    return TcpFlags{static_cast<int>(lhs) & static_cast<int>(rhs)};
}

inline auto contains_all(TcpFlags me, TcpFlags what) -> bool {
    return (me & what) == what;
}

inline auto contains_any(TcpFlags me, TcpFlags what) -> bool {
    return (me & what) != TcpFlags::NONE;
}

#define	TCP_PAWS_IDLE	(24 * 24 * 60 * 60)	/* 24 days in secs */

enum class PrioScheme : int {
    CAPPED_BYTES_SENT = 0,
    FLOW_SIZE = 1,
    REMAINING_SIZE = 2,
    BYTES_SENT = 3,
    BATCHED_REMAINING_SIZE = 4,
    UNKNOWN = 5,
    LAZY_REMAINING_SIZE = 6,
    LAZY_REMAINING_SIZE_BYTES_SENT = 7,
};

class FullTcpAgent;
class DelAckTimer : public TimerHandler {
public:
	DelAckTimer(FullTcpAgent *a) : TimerHandler(), a_(a) { }
protected:
	virtual void expire(Event *);
	FullTcpAgent *a_;
};

class FullTcpAgent : public TcpAgent {
    using super = TcpAgent;
    class EcnProcessor;
    friend class AfabricEcnhatSenderCETracker;

public:
	FullTcpAgent();
	~FullTcpAgent() override;

    static int get_lowest_priority();

    void recv(Packet *pkt, Handler*) override;
    void timeout(int tno) override; 	// tcp_timers() in real code
    void close() override { usrclosed(); }
    void advanceby(int) override;	// over-rides tcp base version
	virtual void advance_bytes(int);	// unique to full-tcp
	virtual void soft_reset();	// for persistent-connections
    void sendmsg(int nbytes, const char *flags = 0) override;
    int& size() override { return maxseg_; } //FullTcp uses maxseg_ for size_
	int command(int argc, const char*const* argv) override;
    void reset() override;       		// reset to a known point
protected:
    friend class EcnProcessor;  // TODO: remove
	void delay_bind_init_all() override;
	int delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer) override;
    auto ecn_processor() -> EcnProcessor&;
	/* Shuang: priority dropping */
	virtual int calc_no_deadline_priority(int seq, int maxseq);
    virtual int calc_deadline_priority();
	virtual int into_bounded_priority(int prio);
	virtual int get_num_bytes_remaining();
	/* Wei: PIAS priority */
	virtual int piasPrio(int bytes_sent);

	PrioScheme prio_scheme_;
	int prio_num_; //number of priorities; 0: unlimited
	int prio_cap_[7];
    int enable_pias_;   //wei: enable PIAS or not
	int pias_prio_num_;	//wei: number of priorities used by PIAS (no more than 8)
    int pias_thresh_[7];    //wei: demotion thresholds of PIAS
	int pias_debug_;	//wei: debug mode for PIAS
	int startseq_;
	int last_prio_;
	int seq_bound_;
	int prob_cap_;  //change to prob mode after #prob_cap_ timeout
	int prob_count_; //current #timeouts
	bool prob_mode_;
	int last_sqtotal_;
	int cur_sqtotal_;
	int nominal_deadline;
	int use_deadline;
	int deadline; // time remain in us at the beginning
	double start_time; //start time
	int early_terminated_; //early terminated
    int true_flow_size_; // true flow size (i.e., including all chunks)
    int use_true_remaining_size_;
    int rtx_on_eof_;
    int reset_window_on_eof_;
    double no_eof_minrto_;
    double eof_minrto_;

	int closed_;
	int ts_option_size_;	// header bytes in a ts option
	int pipe_;		// estimate of pipe occupancy (for Sack)
	int pipectrl_;		// use pipe-style control
	int rtxbytes_;		// retransmitted bytes last recovery
	int open_cwnd_on_pack_;	// open cwnd on a partial ack?
	int segs_per_ack_;  // for window updates
	int spa_thresh_;    // rcv_nxt < spa_thresh? -> 1 seg per ack
	int nodelay_;       // disable sender-side Nagle?
	int fastrecov_;	    // are we in fast recovery?
	int deflate_on_pack_;	// deflate on partial acks (reno:yes)
	int data_on_syn_;   // send data on initial SYN?
	double last_send_time_;	// time of last send

	/* Mohammad: state-variable for robust
	   FCT measurement.
	*/
	int flow_remaining_; /* Number of bytes yet to be received from
			       the current flow (at the receiver). This is
			       set by TCL when starting a flow. Receiver will
			       set immediate ACKs when nothing remains to
			       notify sender of flow completion. */

	//abd
    std::optional<double> last_eof_time_ = std::nullopt;

	// Mohammad: if non-zero, set dupack threshold to max(3, dynamic_dupack_ * cwnd)_
	double dynamic_dupack_;

	int close_on_empty_;	// close conn when buffer empty
	int signal_on_empty_;	// signal when buffer is empty
	int reno_fastrecov_;	// do reno-style fast recovery?
	int infinite_send_;	// Always something to send
	int tcprexmtthresh_;    // fast retransmit threshold
	int iss_;       // initial send seq number
	int irs_;	// initial recv'd # (peer's iss)
	int dupseg_fix_;    // fix bug with dup segs and dup acks?
	int dupack_reset_;  // zero dupacks on dataful dup acks?
	int halfclose_;	    // allow simplex closes?
	int nopredict_;	    // disable header predication
	int dsack_;	    // do DSACK as well as SACK?
	double delack_interval_;
        int debug_;                     // Turn on/off debug output

	int headersize() override;   // a tcp header w/opts
	TcpFlags outflags();     // state-specific tcp header flags
	int rcvseqinit(int, int); // how to set rcv_nxt_
	int predict_ok(Packet*); // predicate for recv-side header prediction
	int idle_restart();	// should I restart after idle?
	int fast_retransmit(int);  // do a fast-retransmit on specified seg
	inline double now() { return Scheduler::instance().clock(); }
	virtual void newstate(TcpState ns);

	virtual void bufferempty();   		// called when sender buffer is empty

	void finish();
	void reset_rtx_timer(int);  	// adjust the rtx timer

	virtual void timeout_action();	// what to do on rtx timeout
	 void dupack_action() override;	// what to do on dup acks
	virtual void pack_action(Packet*);	// action on partial acks
	virtual void ack_action(Packet*);	// action on acks
	 void send_much(int force, TcpXmissionReason reason, int maxburst = 0) override;
	virtual auto build_options(hdr_tcp*) -> int;	// insert opts, return len
	virtual TcpFlags reass(Packet*);		// reassemble: pass to ReassemblyQueue
	virtual void process_sack(hdr_tcp*);	// process a SACK
	virtual int send_allowed(int);		// ok to send this seq#?
	virtual int nxt_tseq() {
		return t_seqno_;		// next seq# to send
	}
	virtual void sent(int seq, int amt) {
		if (seq == t_seqno_)
			t_seqno_ += amt;
		pipe_ += amt;
		if (seq < int(maxseq_))
			rtxbytes_ += amt;
	}
	virtual void oldack() {			// what to do on old ack
		dupacks_ = 0;
	}

	virtual void extra_ack() {		// dup ACKs after threshold
		if (reno_fastrecov_)
			cwnd_++;
	}

	virtual void sendpacket(int seqno, int ackno, TcpFlags pflags, int datalen, TcpXmissionReason reason, Packet *p=0);
    using TcpAgent::connect;
	void connect();     		// do active open
	void listen() override;      		// do passive open
	void usrclosed();   		// user requested a close
	virtual int need_send();    		// send ACK/win-update now?
    // output 1 packet
	virtual int foutput(int seqno, TcpXmissionReason reason = TcpXmissionReason::NORMAL);
	void newack(Packet* pkt);	// process an ACK
	int pack(Packet* pkt);		// is this a partial ack?
	void dooptions(Packet*);	// process option(s)
	DelAckTimer delack_timer_;	// other timers in tcp.h
	void cancel_timers() override;		// cancel all timers
	void prpkt(Packet*);		// print packet (debugging helper)
	static const char * flagstr(int);		// print header flags as systatic static static static static mbols
	static char const * statestr(TcpState s);		// print states as symbols


	/*
	* the following are part of a tcpcb in "real" RFC793 TCP
	*/
	int maxseg_;        /* MSS */
	int flags_;     /* controls next output() call */
	TcpState state_;     /* enumerated type: FSM state */
	TcpState last_state_; /* FSM state at last pkt recv */
	int rcv_nxt_;       /* next sequence number expected */

	ReassemblyQueue rq_;    /* TCP reassembly queue */
	/*
	* the following are part of a tcpcb in "real" RFC1323 TCP
	*/
	int last_ack_sent_; /* ackno field from last segment we sent */

	double recent_;		// ts on SYN written by peer
	double recent_age_;	// my time when recent_ was set

    int link_rate_; // link rate in Gbps

	/*
	 * setting iw, specific to tcp-full, called
	 * by TcpAgent::reset()
	 */
	void set_initial_window() override;

    int calc_pias_priority(int seqno, int datalen, hdr_ip *iph);

    virtual int calc_unbounded_no_deadline_priority(int seq, int maxseq);

    int get_expiration_time_us() const;
};

class NewRenoFullTcpAgent : public FullTcpAgent {

public:
	NewRenoFullTcpAgent();
protected:
	int	save_maxburst_;		// saved value of maxburst_
	int	recov_maxburst_;	// maxburst lim during recovery
	void pack_action(Packet*);
	void ack_action(Packet*);
};

class TahoeFullTcpAgent : public FullTcpAgent {
protected:
	void dupack_action();
};

class SackFullTcpAgent : public FullTcpAgent {
public:
	SackFullTcpAgent() :
		sq_(sack_min_), sack_min_(-1), h_seqno_(-1) { }
	~SackFullTcpAgent() { rq_.clear(); }
protected:
	void delay_bind_init_all() override;
	int delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer) override;

    [[nodiscard]]
    int calc_unbounded_no_deadline_priority(int seq, int maxseq) override;

	void pack_action(Packet*) override;
	void ack_action(Packet*) override;
    void dupack_action() override;
    void process_sack(hdr_tcp*) override;
    void timeout_action() override;

    int nxt_tseq() override;
	virtual int hdrsize(int nblks);
    int send_allowed(int) override;
	void sent(int seq, int amt) override {
		if (seq == h_seqno_)
			h_seqno_ += amt;
		FullTcpAgent::sent(seq, amt);
	}
    int get_num_bytes_remaining() override;

    int build_options(hdr_tcp*) override;	// insert opts, return len
	int clear_on_timeout_;	// clear sender's SACK queue on RTX timeout?
	int sack_option_size_;	// base # bytes for sack opt (no blks)
	int sack_block_size_;	// # bytes in a sack block (def: 8)
	int max_sack_blocks_;	// max # sack blocks to send
	int sack_rtx_bthresh_;	// hole-fill byte threshold
	int sack_rtx_cthresh_;	// hole-fill counter threshold
	int sack_rtx_threshmode_;	// hole-fill mode setting


	void	reset() override;
	//XXX not implemented?
	//void	sendpacket(int seqno, int ackno, int pflags, int datalen, int reason, Packet *p=0);

	ReassemblyQueue sq_;	// SACK queue, used by sender
	int sack_min_;		// first seq# in sack queue, initializes sq_
	int h_seqno_;		// next seq# to hole-fill
};

class MinTcpAgent : public SackFullTcpAgent {
public:
   void timeout_action() override;
   double rtt_timeout() override;
};

class DDTcpAgent : public SackFullTcpAgent {
    void slowdown(TcpSlowdownFlags how) override;

private:
	int get_num_bytes_remaining() override;

protected:
    int calc_unbounded_no_deadline_priority(int seq, int maxseq) override;
    int foutput(int seqno, TcpXmissionReason reason = TcpXmissionReason::NORMAL) override; // output 1 packet

private:
	int need_send() override;    		// send ACK/win-update now?
public:
    DDTcpAgent();
};

class FullTcpAgent::EcnProcessor : public TcpAgent::EcnProcessor {
    friend class FullTcpAgent;
    using super = TcpAgent::EcnProcessor;
public:
    enum class EcnSynAckAction : int {
        WINDOW_OF_ONE = 0,
        WAIT = 1,
        RETRY = 2
    };

    enum class EcnDecision {
        CONGESTION, NO_CONGESTION, CONGESTION_DROP
    };
public:
    explicit EcnProcessor(FullTcpAgent * agent);

    void delay_bind_init_all() override;
    auto delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer) -> bool override;

    auto predict_ok(Packet *pkt) const -> bool;
    void on_receive_predict_ok(Packet * packet, bool has_data);

    void modify_pflags_on_foutput(TcpFlags & pflags, bool is_retransmit);
    void on_sendpacket(TcpFlags & pflags, Packet * pkt, bool has_data);

    void on_receive_pure_incoming();
    void on_receive_ack_pre(Packet * packet, bool has_data);
    auto on_receive_syn_ack_pre(hdr_flags *fh) -> EcnDecision;
    void on_receive_newack(Packet * pkt) override;
    void on_receive(Packet * pkt) override;
    auto can_open_cwnd(Packet * pkt) const -> bool override;
    auto on_receive_syn_ack_ack(hdr_flags *fh) -> EcnDecision;
    void on_receive_syn_ack(hdr_flags *fh);
    void on_receive_ack(Packet * pkt, TcpFlags tiflags);
    // check for a ECN-SYN with ECE|CWR
    void on_receive_syn(hdr_flags *fh);

    void reset();

    /* setup ECN syn and ECN SYN+ACK packet headers */
    void on_foutput(int highest);

protected:
    auto agent() -> FullTcpAgent&;
    auto agent() const -> FullTcpAgent const&;

    auto get_send_tracker() -> SenderCETracker& override;
    auto get_send_tracker() const -> SenderCETracker const& override;

private:
    auto is_active() const -> bool;

    auto get_recv_tracker() -> ReceiverCETracker&;
    auto get_recv_tracker() const -> ReceiverCETracker const&;

    auto get_next_packet_ect(bool has_data, bool is_syn_ack) const -> bool;
    void set_ce_if_needed(TcpFlags& pflags) const;
    void inform_pacer_if_needed(hdr_ip *iph, bool has_data);

    void update_state_new_packet(Packet * packet, bool has_data);

    auto agent_has_pending_acks() const -> bool;
    void send_immediate_ack_with_previous_ce();
    void report_unexpected_ecnecho() const;
    void retry_syn_ack();
    auto handle_syn_ack_ack_ecn() -> EcnDecision;

    void copy_to_hdr_flags(TcpFlags pflags, hdr_flags * fh) const;

    static auto is_syn(TcpFlags pflags) -> bool;
    static auto is_some_syn(TcpFlags pflags) -> bool;
    static auto is_syn_ack(TcpFlags pflags) -> bool;

private:
    int ecn_syn_;       // Make SYN/ACK packets ECN-Capable?
    bool ecn_syn_next_;	// Make next SYN/ACK packet ECN-Capable?
                        // This is used for ecn_syn_wait = 2.
    /* Mohammad: state-variable to inform
     * pacer (TBF) of receiving ecnecho for the flow
     */
    int informpacer_;
    EcnSynAckAction ecn_syn_wait_;

    int afabric_ecn_enabled_;

    unique_ptr<ReceiverCETracker> const normal_recv_;
    unique_ptr<ReceiverCETracker> const ecnhat_recv_;
    unique_ptr<ReceiverCETracker> const afabric_recv_;

    unique_ptr<SenderCETracker> const afabric_send_;
};

#endif
