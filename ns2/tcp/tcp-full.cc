 /* Mortier <Richard.Mortier@cl.cam.ac.uk>
 *
 * Some warnings and comments:
 *	this version of TCP will not work correctly if the sequence number
 *	goes above 2147483648 due to sequence number wrap
 *
 *	this version of TCP by default sends data at the beginning of a
 *	connection in the "typical" way... That is,
 *		A   ------> SYN ------> B
 *		A   <----- SYN+ACK ---- B
 *		A   ------> ACK ------> B
 *		A   ------> data -----> B
 *
 *	there is no dynamic receiver's advertised window.   The advertised
 *	window is simulated by simply telling the sender a bound on the window
 *	size (wnd_).
 *
 *	in real TCP, a user process performing a read (via PRU_RCVD)
 *		calls tcp_output each time to (possibly) send a window
 *		update.  Here we don't have a user process, so we simulate
 *		a user process always ready to consume all the receive buffer
 *
 * Notes:
 *	wnd_, wnd_init_, cwnd_, ssthresh_ are in segment units
 *	sequence and ack numbers are in byte units
 *
 * Futures:
 *      there are different existing TCPs with respect to how
 *      ack's are handled on connection startup.  Some delay
 *      the ack for the first segment, which can cause connections
 *      to take longer to start up than if we be sure to ack it quickly.
 *
 *      some TCPs arrange for immediate ACK generation if the incoming segment
 *      contains the PUSH bit
 *
 *
 */

#include "ecn/common.h"
#include "tcp.h"
#include <memory>
[[maybe_unused]]
static const char rcsid[] =
    "@(#) $Header: /cvsroot/nsnam/ns-2/tcp/tcp-full.cc,v 1.128 2009/03/29 20:59:41 sallyfloyd Exp $ (LBL)";

#include <tools/BindCachingMixin.hpp>
#include "ip.h"
#include "tcp-full.h"
#include "flags.h"
#include "random.h"
#include "template.h"
#include "math.h"
#include "ecn/tcp-full-receivers.h"
#include "ecn/tcp-full-senders.h"
#include <iostream>
#include <fstream>
#include <fmt/core.h>

/*
 * Tcl Linkage for the following:
 *	Agent/TCP/FullTcp, Agent/TCP/FullTcp/Tahoe,
 *	Agent/TCP/FullTcp/Newreno, Agent/TCP/FullTcp/Sack
 *
 * See tcl/lib/ns-default.tcl for init methods for
 *	Tahoe, Newreno, and Sack
 */

static class FullTcpClass : public TclClass {
public:
	FullTcpClass() : TclClass("Agent/TCP/FullTcp") {}
	TclObject* create(int, const char*const*) {
		return (new FullTcpAgent());
	}
} class_full;

static class TahoeFullTcpClass : public TclClass {
public:
	TahoeFullTcpClass() : TclClass("Agent/TCP/FullTcp/Tahoe") {}
	TclObject* create(int, const char*const*) {
		// ns-default sets reno_fastrecov_ to false
		return (new TahoeFullTcpAgent());
	}
} class_tahoe_full;

static class NewRenoFullTcpClass : public TclClass {
public:
	NewRenoFullTcpClass() : TclClass("Agent/TCP/FullTcp/Newreno") {}
	TclObject* create(int, const char*const*) {
		// ns-default sets open_cwnd_on_pack_ to false
		return (new NewRenoFullTcpAgent());
	}
} class_newreno_full;

static class SackFullTcpClass : public TclClass {
public:
	SackFullTcpClass() : TclClass("Agent/TCP/FullTcp/Sack") {}
	TclObject* create(int, const char*const*) {
		// ns-default sets reno_fastrecov_ to false
		// ns-default sets open_cwnd_on_pack_ to false
		return (new SackFullTcpAgent());
	}
} class_sack_full;

static class MinTcpClass : public TclClass {
public:
	MinTcpClass() : TclClass("Agent/TCP/FullTcp/Sack/MinTCP") {}
	TclObject* create(int, const char*const*) {
		return (new MinTcpAgent());
	}
} class_min_full;

static class DDTcpClass : public TclClass {
public:
	DDTcpClass() : TclClass("Agent/TCP/FullTcp/Sack/DDTCP") {}
	TclObject* create(int, const char*const*) {
		return (new DDTcpAgent());
	}
} class_dd_full;

FullTcpAgent::FullTcpAgent() :
        TcpAgent(make_unique<EcnProcessor>(this)),
		prio_scheme_{PrioScheme::REMAINING_SIZE},
		prio_num_(0), 
        enable_pias_(0), pias_prio_num_(0), pias_debug_(0),
        startseq_(0), 
        last_prio_(0),
        seq_bound_(0),
        use_deadline(0),
		closed_(0), pipe_(-1), rtxbytes_(0), fastrecov_(FALSE),
        last_send_time_(-1.0),  
        infinite_send_(FALSE), 
        irs_(-1), 
        delack_timer_(this), 
        flags_(0),
        state_(TcpState::CLOSED),
		last_state_(TcpState::CLOSED),
        rq_(rcv_nxt_),
		last_ack_sent_(-1)
    { }

/*
 * Delayed-binding variable linkage
 */

void
FullTcpAgent::delay_bind_init_all()
{
    delay_bind_init_one("segsperack_");
    delay_bind_init_one("segsize_");
    delay_bind_init_one("tcprexmtthresh_");
    delay_bind_init_one("iss_");
    delay_bind_init_one("nodelay_");
    delay_bind_init_one("data_on_syn_");
    delay_bind_init_one("dupseg_fix_");
    delay_bind_init_one("dupack_reset_");
    delay_bind_init_one("close_on_empty_");
    delay_bind_init_one("signal_on_empty_");
    delay_bind_init_one("interval_");
    delay_bind_init_one("ts_option_size_");
    delay_bind_init_one("reno_fastrecov_");
    delay_bind_init_one("pipectrl_");
    delay_bind_init_one("open_cwnd_on_pack_");
    delay_bind_init_one("halfclose_");
    delay_bind_init_one("nopredict_");
    delay_bind_init_one("ecn_syn_");
    delay_bind_init_one("ecn_syn_wait_");
    delay_bind_init_one("debug_");
    delay_bind_init_one("spa_thresh_");

//
    delay_bind_init_one("flow_remaining_"); //Mohammad
	delay_bind_init_one("dynamic_dupack_");
//
	delay_bind_init_one("prio_scheme_"); // Shuang
    delay_bind_init_one("prio_num_"); //Shuang
    delay_bind_init_one("prio_cap0"); //Shuang
    delay_bind_init_one("prio_cap1"); //Shuang
    delay_bind_init_one("prio_cap2"); //Shuang
    delay_bind_init_one("prio_cap3"); //Shuang
    delay_bind_init_one("prio_cap4"); //Shuang
    delay_bind_init_one("prio_cap5"); //Shuang
    delay_bind_init_one("prio_cap6"); //Shuang
    delay_bind_init_one("prob_cap_");
    delay_bind_init_one("use_deadline");
    delay_bind_init_one("deadline"); //Shuang
    delay_bind_init_one("nominal_deadline"); //Shuang
    delay_bind_init_one("early_terminated_"); //Shuang

    delay_bind_init_one("enable_pias_"); //wei
    delay_bind_init_one("pias_prio_num_"); //wei
    delay_bind_init_one("pias_thresh_0"); //wei
    delay_bind_init_one("pias_thresh_1"); //wei
    delay_bind_init_one("pias_thresh_2"); //wei
    delay_bind_init_one("pias_thresh_3"); //wei
    delay_bind_init_one("pias_thresh_4"); //wei
    delay_bind_init_one("pias_thresh_5"); //wei
    delay_bind_init_one("pias_thresh_6"); //wei
    delay_bind_init_one("pias_debug_"); //wei

    delay_bind_init_one("link_rate_");
    delay_bind_init_one("true_flow_size_");
    delay_bind_init_one("use_true_remaining_size_");
    delay_bind_init_one("rtx_on_eof_");
    delay_bind_init_one("reset_window_on_eof_");
    delay_bind_init_one("no_eof_minrto_");
    delay_bind_init_one("eof_minrto_");

	super::delay_bind_init_all();

    reset();
}

int
FullTcpAgent::delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer)
{
        if (delay_bind(varName, localName, "segsperack_", &segs_per_ack_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "segsize_", &maxseg_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "tcprexmtthresh_", &tcprexmtthresh_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "iss_", &iss_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "spa_thresh_", &spa_thresh_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "nodelay_", &nodelay_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "data_on_syn_", &data_on_syn_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "dupseg_fix_", &dupseg_fix_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "dupack_reset_", &dupack_reset_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "close_on_empty_", &close_on_empty_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "signal_on_empty_", &signal_on_empty_, tracer)) return TCL_OK;
        if (delay_bind_time(varName, localName, "interval_", &delack_interval_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "ts_option_size_", &ts_option_size_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "reno_fastrecov_", &reno_fastrecov_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "pipectrl_", &pipectrl_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "open_cwnd_on_pack_", &open_cwnd_on_pack_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "halfclose_", &halfclose_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "nopredict_", &nopredict_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "ecn_syn_", &ecn_processor().ecn_syn_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "ecn_syn_wait_", reinterpret_cast<std::underlying_type_t<EcnProcessor::EcnSynAckAction> *>(&ecn_processor().ecn_syn_wait_), tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "debug_", &debug_, tracer)) return TCL_OK;
        if (delay_bind_bool(varName, localName, "use_deadline", &use_deadline, tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "flow_remaining_", &flow_remaining_, tracer)) return TCL_OK; // Mohammad
	if (delay_bind(varName, localName, "dynamic_dupack_", &dynamic_dupack_, tracer)) return TCL_OK; // Mohammad
	if (delay_bind(varName, localName, "link_rate_", &link_rate_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "true_flow_size_", &true_flow_size_, tracer)) return TCL_OK;
    if (delay_bind_bool(varName, localName, "use_true_remaining_size_", &use_true_remaining_size_, tracer)) return TCL_OK;
    if (delay_bind_bool(varName, localName, "rtx_on_eof_", &rtx_on_eof_, tracer)) return TCL_OK;
    if (delay_bind_bool(varName, localName, "reset_window_on_eof_", &reset_window_on_eof_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "no_eof_minrto_", &no_eof_minrto_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "eof_minrto_", &eof_minrto_, tracer)) return TCL_OK;

	if (delay_bind(varName, localName, "prio_scheme_", reinterpret_cast<std::underlying_type_t<PrioScheme> *>(&prio_scheme_), tracer)) return TCL_OK; // Shuang
	if (delay_bind(varName, localName, "prio_num_", &prio_num_, tracer)) return TCL_OK; //Shuang
	if (delay_bind(varName, localName, "prio_cap0", &prio_cap_[0], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap1", &prio_cap_[1], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap2", &prio_cap_[2], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap3", &prio_cap_[3], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap4", &prio_cap_[4], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap5", &prio_cap_[5], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap6", &prio_cap_[6], tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prob_cap_", &prob_cap_, tracer)) return TCL_OK; //Shuang
	if (delay_bind(varName, localName, "deadline", &deadline, tracer)) return TCL_OK; //Shuang
    if (delay_bind(varName, localName, "nominal_deadline", &nominal_deadline, tracer)) return TCL_OK; //Shuang
	if (delay_bind(varName, localName, "early_terminated_", &early_terminated_, tracer)) return TCL_OK; //Shuang

    if (delay_bind_bool(varName, localName, "enable_pias_", &enable_pias_, tracer)) return TCL_OK; //wei
    if (delay_bind(varName, localName, "pias_prio_num_", &pias_prio_num_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_0", &pias_thresh_[0], tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_1", &pias_thresh_[1], tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_2", &pias_thresh_[2], tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_3", &pias_thresh_[3], tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_4", &pias_thresh_[4], tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_5", &pias_thresh_[5], tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "pias_thresh_6", &pias_thresh_[6], tracer)) return TCL_OK;
    if (delay_bind_bool(varName, localName, "pias_debug_", &pias_debug_, tracer)) return TCL_OK;

        return TcpAgent::delay_bind_dispatch(varName, localName, tracer);
}

void
SackFullTcpAgent::delay_bind_init_all()
{
    delay_bind_init_one("clear_on_timeout_");
    delay_bind_init_one("sack_rtx_cthresh_");
    delay_bind_init_one("sack_rtx_bthresh_");
    delay_bind_init_one("sack_block_size_");
    delay_bind_init_one("sack_option_size_");
    delay_bind_init_one("max_sack_blocks_");
    delay_bind_init_one("sack_rtx_threshmode_");
	FullTcpAgent::delay_bind_init_all();
}

int
SackFullTcpAgent::delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer)
{
        if (delay_bind_bool(varName, localName, "clear_on_timeout_", &clear_on_timeout_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "sack_rtx_cthresh_", &sack_rtx_cthresh_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "sack_rtx_bthresh_", &sack_rtx_bthresh_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "sack_rtx_threshmode_", &sack_rtx_threshmode_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "sack_block_size_", &sack_block_size_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "sack_option_size_", &sack_option_size_, tracer)) return TCL_OK;
        if (delay_bind(varName, localName, "max_sack_blocks_", &max_sack_blocks_, tracer)) return TCL_OK;
        return FullTcpAgent::delay_bind_dispatch(varName, localName, tracer);
}

int
FullTcpAgent::command(int argc, const char*const* argv)
{
	// would like to have some "connect" primitive
	// here, but the problem is that we get called before
	// the simulation is running and we want to send a SYN.
	// Because no routing exists yet, this fails.
	// Instead, see code in advance().
	//
	// listen can happen any time because it just changes state_
	//
	// close is designed to happen at some point after the
	// simulation is running (using an ns 'at' command)

	if (argc == 2) {
		if (strcmp(argv[1], "listen") == 0) {
			// just a state transition
			listen();
			return (TCL_OK);
		}
		if (strcmp(argv[1], "soft-reset") == 0) {
			soft_reset();
			return (TCL_OK);
		}
		if (strcmp(argv[1], "close") == 0) {
			usrclosed();
			return (TCL_OK);
		}
	}
	if (argc == 3) {
		if (strcmp(argv[1], "advance") == 0) {
			advanceby(atoi(argv[2]));
			return (TCL_OK);
		}
		if (strcmp(argv[1], "advanceby") == 0) {
			advanceby(atoi(argv[2]));
			return (TCL_OK);
		}
		if (strcmp(argv[1], "advance-bytes") == 0) {
			advance_bytes(atoi(argv[2]));
			return (TCL_OK);
		}
		//Mohammad
		if (strcmp(argv[1], "get-flow") == 0) {
		        flow_remaining_ = atoi(argv[2]);
		        return(TCL_OK);
		}
	}
	if (argc == 4) {
		if (strcmp(argv[1], "sendmsg") == 0) {
			sendmsg(atoi(argv[2]), argv[3]);
			return (TCL_OK);
		}
	}
    return (TcpAgent::command(argc, argv));
}

/*
 * "User Interface" Functions for Full TCP
 *	advanceby(number of packets)
 *	advance_bytes(number of bytes)
 *	sendmsg(int bytes, char* buf)
 *	listen
 *	close
 */

/*
 * the 'advance' interface to the regular tcp is in packet
 * units.  Here we scale this to bytes for full tcp.
 *
 * 'advance' is normally called by an "application" (i.e. data source)
 * to signal that there is something to send
 *
 * 'curseq_' is the sequence number of the last byte provided
 * by the application.  In the case where no data has been supplied
 * by the application, curseq_ is the iss_.
 */
void
FullTcpAgent::advanceby(int np)
{
    advance_bytes(np * maxseg_);
}

/*
 * the byte-oriented interface: advance_bytes(int nbytes)
 */

void 
FullTcpAgent::soft_reset() {
    cwnd_ = initial_window();
    start_time = now();
    early_terminated_ = 0;
    seq_bound_ = -1;

    switch(state_) {
    case TcpState::CLOSED:
    case TcpState::LISTEN:
            deadline = nominal_deadline;
            startseq_ = iss_;
        break;
    case TcpState::ESTABLISHED:
    case TcpState::SYN_SENT:
    case TcpState::SYN_RECEIVED:
            startseq_ = (curseq_ < iss_) ? iss_ : curseq_;
        break;
    default:
        if (debug_)
            fprintf(stderr, "%f: FullTcpAgent::soft_reset(%s): cannot do soft reset while in state %s\n",
                 now(), name(), statestr(state_));

    }

    ndatabytes_=0; //Reset number of data bytes sent back to 0
}

void
FullTcpAgent::advance_bytes(int nb) {
	//
	// state-specific operations:
	//	if CLOSED or LISTEN, reset and try a new active open/connect
	//	if ESTABLISHED, queue and try to send more
	//	if SYN_SENT or SYN_RCVD, just queue
	//	if above ESTABLISHED, we are closing, so don't allow
	//
	switch (state_) {

    case TcpState::CLOSED:
    case TcpState::LISTEN:
            reset();
            curseq_ = iss_ + nb;
            connect();              // initiate new connection
		break;

    case TcpState::ESTABLISHED:
	case TcpState::SYN_SENT:
	case TcpState::SYN_RECEIVED:
            if (curseq_ < iss_)
                    curseq_ = iss_;
            curseq_ += nb;
		break;

	default:
            if (debug_)
	            fprintf(stderr, "%f: FullTcpAgent::advance(%s): cannot advance while in state %s\n",
		         now(), name(), statestr(state_));

	}

    if (state_ == TcpState::ESTABLISHED) {
        auto force = 0;

        if (rtx_on_eof_ && signal_on_empty_) {
            force = 1;
        }

        send_much(force, TcpXmissionReason::NORMAL, maxburst_);
    }
}

/*
 * If MSG_EOF is set, by setting close_on_empty_ to TRUE, we ensure that
 * a FIN will be sent when the send buffer emptys.
 * If DAT_EOF is set, the callback function done_data is called
 * when the send buffer empty
 *
 * When (in the future?) FullTcpAgent implements T/TCP, avoidance of 3-way
 * handshake can be handled in this function.
 */
void
FullTcpAgent::sendmsg(int nbytes, const char *flags)
{
	if (flags && strcmp(flags, "MSG_EOF") == 0){
		close_on_empty_ = TRUE;
printf("setting 2 closeonempty to true for fid= %d\n",fid_);
        }

	if (flags && strcmp(flags, "DAT_EOF") == 0){
		signal_on_empty_ = TRUE;
        if (rtx_on_eof_) {
            if (state_ == TcpState::ESTABLISHED) {
                last_eof_time_ = Scheduler::instance().clock();
            }
            t_seqno_ = (highest_ack_ < 0) ? iss_ : int(highest_ack_);
        }
        if (eof_minrto_ >= 0) {
            minrto_ = eof_minrto_;
        }
        if (reset_window_on_eof_) {
            cwnd_ = std::max(initial_window(), cwnd_);
            prob_mode_ = false;
	        prob_count_ = 0;
            set_rtx_timer();
        }
	}
    if (flags && strcmp(flags, "DAT_NEW") == 0) {
        if (no_eof_minrto_ >= 0) {
            minrto_ = no_eof_minrto_;
        }
        soft_reset();
    }
	if (nbytes == -1) {
		infinite_send_ = TRUE;
		advance_bytes(0);
	} else
		advance_bytes(nbytes);
}

/*
 * do an active open
 * (in real TCP, see tcp_usrreq, case PRU_CONNECT)
 */
void
FullTcpAgent::connect()
{
	newstate(TcpState::SYN_SENT);	// sending a SYN now
	sent(iss_, foutput(iss_, TcpXmissionReason::NORMAL));
}

/*
 * be a passive opener
 * (in real TCP, see tcp_usrreq, case PRU_LISTEN)
 * (for simulation, make this peer's ptype ACKs)
 */
void
FullTcpAgent::listen()
{
	newstate(TcpState::LISTEN);
	type_ = PT_ACK;	// instead of PT_TCP
}


/*
* This function is invoked when the sender buffer is empty. It in turn
* invokes the Tcl done_data procedure that was registered with TCP.
*/

void
FullTcpAgent::bufferempty()
{
    signal_on_empty_ = FALSE;
    Tcl::instance().evalf("%s done_data", this->name());
}


/*
 * called when user/application performs 'close'
 */

void
FullTcpAgent::usrclosed()
{
	curseq_ = maxseq_ - 1;	// now, no more data
	infinite_send_ = FALSE;	// stop infinite send
	switch (state_) {
    case TcpState::CLOSED:
    case TcpState::LISTEN:
		cancel_timers();
		newstate(TcpState::CLOSED);
		finish();
		break;
    case TcpState::SYN_SENT:
		newstate(TcpState::CLOSED);
		/* fall through */
    case TcpState::LAST_ACK:
		flags_ |= TF_NEEDFIN;
		send_much(1, TcpXmissionReason::NORMAL, maxburst_);
		break;
    case TcpState::SYN_RECEIVED:
    case TcpState::ESTABLISHED:
		newstate(TcpState::FIN_WAIT_1);
		flags_ |= TF_NEEDFIN;
		send_much(1, TcpXmissionReason::NORMAL, maxburst_);
		break;
    case TcpState::CLOSE_WAIT:
		newstate(TcpState::LAST_ACK);
		flags_ |= TF_NEEDFIN;
		send_much(1, TcpXmissionReason::NORMAL, maxburst_);
		break;
    case TcpState::FIN_WAIT_1:
    case TcpState::FIN_WAIT_2:
    case TcpState::CLOSING:
		/* usr asked for a close more than once [?] */
                if (debug_)
		         fprintf(stderr,
		           "%f FullTcpAgent(%s): app close in bad state %s\n",
		           now(), name(), statestr(state_));
		break;
	default:
                if (debug_)
		        fprintf(stderr,
		        "%f FullTcpAgent(%s): app close in unknown state %s\n",
		        now(), name(), statestr(state_));
	}

	return;
}

/*
 * Utility type functions
 */

void
FullTcpAgent::cancel_timers()
{

	// cancel: rtx, burstsend, delsnd
	TcpAgent::cancel_timers();
	// cancel: delack
	delack_timer_.force_cancel();
}

void
FullTcpAgent::newstate(TcpState state)
{
//printf("%f(%s): state changed from %s to %s\n",
//now(), name(), statestr(state_), statestr(state));

	state_ = state;
}

void
FullTcpAgent::prpkt(Packet *pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);	// TCP header
	hdr_cmn *th = hdr_cmn::access(pkt);	// common header (size, etc)
	//hdr_flags *fh = hdr_flags::access(pkt);	// flags (CWR, CE, bits)
	hdr_ip* iph = hdr_ip::access(pkt);
	int datalen = th->size() - tcph->hlen(); // # payload bytes

	fprintf(stdout, " [%d:%d.%d>%d.%d] (hlen:%d, dlen:%d, seq:%d, ack:%d, flags:0x%x (%s), salen:%d, reason:0x%x)\n",
		th->uid(),
		iph->saddr(), iph->sport(),
		iph->daddr(), iph->dport(),
		tcph->hlen(),
		datalen,
		tcph->seqno(),
		tcph->ackno(),
		tcph->flags(), flagstr(tcph->flags()),
		tcph->sa_length(),
		static_cast<unsigned int>(tcph->reason()));
}

const char *
FullTcpAgent::flagstr(int hflags)
{
	// update this if tcp header flags change
	static char const *flagstrs[28] = {
		"<null>", "<FIN>", "<SYN>", "<SYN,FIN>",	// 0-3
		"<?>", "<?,FIN>", "<?,SYN>", "<?,SYN,FIN>",	// 4-7
		"<PSH>", "<PSH,FIN>", "<PSH,SYN>", "<PSH,SYN,FIN>", // 0x08-0x0b
		/* do not use <??, in next line because that's an ANSI trigraph */
		"<?>", "<?,FIN>", "<?,SYN>", "<?,SYN,FIN>",	    // 0x0c-0x0f
		"<ACK>", "<ACK,FIN>", "<ACK,SYN>", "<ACK,SYN,FIN>", // 0x10-0x13
		"<ACK>", "<ACK,FIN>", "<ACK,SYN>", "<ACK,SYN,FIN>", // 0x14-0x17
		"<PSH,ACK>", "<PSH,ACK,FIN>", "<PSH,ACK,SYN>", "<PSH,ACK,SYN,FIN>", // 0x18-0x1b
	};
	if (hflags < 0 || (hflags > 28)) {
		/* Added strings for CWR and ECE  -M. Weigle 6/27/02 */
		if (hflags == 72) {
            return "<ECE,PSH>";
        } else if (hflags == 80) {
            return "<ECE,ACK>";
        } else if (hflags == 88) {
            return "<ECE,PSH,ACK>";
        } else if (hflags == 152) {
            return "<CWR,PSH,ACK>";
        } else if (hflags == 153) {
            return "<CWR,PSH,ACK,FIN>";
        } else {
            return "<invalid>";
        }
	}
	return flagstrs[hflags];
}

char const *
FullTcpAgent::statestr(TcpState state)
{
	static const char *statestrs[static_cast<int>(TcpState::NSTATES)] = {
		"CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD",
		"ESTABLISHED", "CLOSE_WAIT", "FIN_WAIT_1", "CLOSING",
		"LAST_ACK", "FIN_WAIT_2"
	};
	if (!is_state_valid(state))
		return "INVALID";
	return statestrs[static_cast<int>(state)];
}

void
DelAckTimer::expire(Event *) {
        a_->timeout(TCP_TIMER_DELACK);
}

/*
 * reset to starting point, don't set state_ here,
 * because our starting point might be LISTEN rather
 * than CLOSED if we're a passive opener
 */
void
FullTcpAgent::reset()
{
	cancel_timers();	// cancel timers first
      	TcpAgent::reset();	// resets most variables
	rq_.clear();		// clear reassembly queue
	rtt_init();		// zero rtt, srtt, backoff
	last_ack_sent_ = -1;
	flow_remaining_ = -1; // Mohammad
	rcv_nxt_ = -1;
	pipe_ = 0;
	rtxbytes_ = 0;
	flags_ = 0;
	t_seqno_ = iss_;
	maxseq_ = -1;
	irs_ = -1;
	last_send_time_ = -1.0;
	if (ts_option_)
		recent_ = recent_age_ = 0.0;
	else
		recent_ = recent_age_ = -1.0;

	fastrecov_ = FALSE;

	closed_ = 0;
	close_on_empty_ = FALSE;

	ecn_processor().reset();
	//Shuang
	prob_mode_ = false;
	prob_count_ = 0;
	last_sqtotal_ = 0;
	deadline = 0;
	early_terminated_ = 0;
}

/*
 * This function is invoked when the connection is done. It in turn
 * invokes the Tcl finish procedure that was registered with TCP.
 * This function mimics tcp_close()
 */

void
FullTcpAgent::finish()
{
	Tcl::instance().evalf("%s done", this->name());
}
/*
 * headersize:
 *	how big is an IP+TCP header in bytes; include options such as ts
 * this function should be virtual so others (e.g. SACK) can override
 */
int
FullTcpAgent::headersize()
{
	int total = tcpip_base_hdr_size_;
	if (total < 1) {
		fprintf(stderr,
		    "%f: FullTcpAgent(%s): warning: tcpip hdr size is only %d bytes\n",
			now(), name(), tcpip_base_hdr_size_);
	}

	if (ts_option_)
		total += ts_option_size_;

	return (total);
}

/*
 * flags that are completely dependent on the tcp state
 * these are used for the next outgoing packet in foutput()
 * (in real TCP, see tcp_fsm.h, the "tcp_outflags" array)
 */
TcpFlags
FullTcpAgent::outflags()
{
	// in real TCP an RST is added in the CLOSED state
	static TcpFlags tcp_outflags[static_cast<int>(TcpState::NSTATES)] = {
		TcpFlags::ACK,					/* 0, CLOSED */
		TcpFlags::NONE,					/* 1, LISTEN */
		TcpFlags::SYN,					/* 2, SYN_SENT */
		TcpFlags::SYN|TcpFlags::ACK,	/* 3, SYN_RECEIVED */
		TcpFlags::ACK,					/* 4, ESTABLISHED */
		TcpFlags::ACK,					/* 5, CLOSE_WAIT */
		TcpFlags::FIN|TcpFlags::ACK,	/* 6, FIN_WAIT_1 */
		TcpFlags::FIN|TcpFlags::ACK,	/* 7, CLOSING */
		TcpFlags::FIN|TcpFlags::ACK,	/* 8, LAST_ACK */
		TcpFlags::ACK,					/* 9, FIN_WAIT_2 */
		/* 10, TIME_WAIT --- not used in simulator */
	};

	if (!is_state_valid(state_)) {
		fprintf(stderr, "%f FullTcpAgent(%s): invalid state %d\n",
			now(), name(), state_);
		return TcpFlags::NONE;
	}

	return (tcp_outflags[static_cast<int>(state_)]);
}

/*
 * reaass() -- extract the appropriate fields from the packet
 *	and pass this info the ReassemblyQueue add routine
 *
 * returns the TCP header flags representing the "or" of
 *	the flags contained in the adjacent sequence # blocks
 */

TcpFlags
FullTcpAgent::reass(Packet* pkt)
{
        hdr_tcp *tcph =  hdr_tcp::access(pkt);
        hdr_cmn *th = hdr_cmn::access(pkt);

        int start = tcph->seqno();
        int end = start + th->size() - tcph->hlen();
        TcpFlags tiflags = TcpFlags{tcph->flags()};
	int fillshole = (start == rcv_nxt_);
	TcpFlags flags;

	// end contains the seq of the last byte of
	// in the packet plus one

	if (start == end && !contains_all(tiflags, TcpFlags::FIN)) {
		fprintf(stderr, "%f: FullTcpAgent(%s)::reass() -- bad condition - adding non-FIN zero-len seg\n",
			now(), name());
		abort();
	}

	flags = TcpFlags{rq_.add(start, end, static_cast<int>(tiflags), 0)};

	//present:
	//
	// If we've never received a SYN (unlikely)
	// or this is an out of order addition, no reason to coalesce
	//

	if (have_received_SYN(state_) == 0 || !fillshole) {
	 	return TcpFlags::NONE;
	}
	//
	// If we get some data in SYN_RECVD, no need to present to user yet
	//
	if (state_ == TcpState::SYN_RECEIVED && (end > start))
		return TcpFlags::NONE;

	// clear out data that has been passed, up to rcv_nxt_,
	// collects flags

	flags |= TcpFlags{rq_.cleartonxt()};

    return flags;
}

/*
 * utility function to set rcv_next_ during inital exchange of seq #s
 */

int
FullTcpAgent::rcvseqinit(int seq, int dlen)
{
	return (seq + dlen + 1);
//printf("newww3 fid= %d, rcv_nxt_= %d diff= %d, highest_ack= %d, last_ack_sent= %d diff= %d\n",fid_,(int)rcv_nxt_,((int)rcv_nxt_)-oldrcvnxt,(int)highest_ack_,last_ack_sent_,((int)last_ack_sent_)-oldlastacksent);
}

/*
 * build a header with the timestamp option if asked
 */
int
FullTcpAgent::build_options(hdr_tcp* tcph)
{
	int total = 0;
	if (ts_option_) {
		tcph->ts() = now();
		tcph->ts_echo() = recent_;
		total += ts_option_size_;
	} else {
		tcph->ts() = tcph->ts_echo() = -1.0;
	}
	return (total);
}

/*
 * pack() -- is the ACK a partial ACK? (not past recover_)
 */

int
FullTcpAgent::pack(Packet *pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);
	/* Added check for fast recovery.  -M. Weigle 5/2/02 */
	return (fastrecov_ && tcph->ackno() >= highest_ack_ &&
		tcph->ackno() < recover_);
}

/*
 * baseline reno TCP exists fast recovery on a partial ACK
 */

void
FullTcpAgent::pack_action(Packet*)
{
	if (reno_fastrecov_ && fastrecov_ && cwnd_ > double(ssthresh_)) {
		cwnd_ = double(ssthresh_); // retract window if inflated
	}
	fastrecov_ = FALSE;
//printf("%f: EXITED FAST RECOVERY\n", now());
	dupacks_ = 0;
}

/*
 * ack_action -- same as partial ACK action for base Reno TCP
 */

void
FullTcpAgent::ack_action(Packet* p)
{
	FullTcpAgent::pack_action(p);
}

int
FullTcpAgent::calc_no_deadline_priority(int seq, int maxseq) {
    auto const priority = calc_unbounded_no_deadline_priority(seq, maxseq);
    if (prio_num_ == 0) {
        return priority;
    } else {
        return into_bounded_priority(priority);
    }
}

int FullTcpAgent::calc_unbounded_no_deadline_priority(int seq, int maxseq) {
    auto const max = 100 * 1460;
    switch (prio_scheme_) {
        case PrioScheme ::CAPPED_BYTES_SENT:
            if (seq - startseq_ > max)
                return max;
            else
                return seq - startseq_;
        case PrioScheme::REMAINING_SIZE:
            return maxseq - seq;
        case PrioScheme::LAZY_REMAINING_SIZE:
            return signal_on_empty_ ? maxseq - seq : get_lowest_priority();
        case PrioScheme::LAZY_REMAINING_SIZE_BYTES_SENT:
            return signal_on_empty_ ? maxseq - seq 
                : get_lowest_priority() + seq - startseq_;
        case PrioScheme::FLOW_SIZE:
            return maxseq - startseq_;
        case PrioScheme::BYTES_SENT:
            return seq - startseq_;
        case PrioScheme::BATCHED_REMAINING_SIZE:
            if (seq >= seq_bound_) {
                seq_bound_ = maxseq_;
                if (maxseq - seq + 10 < 0)
                    last_prio_ = 0;
                else
                    last_prio_ = maxseq - seq + 10;
            }
            return last_prio_;
        case PrioScheme::UNKNOWN:
            abort();
    }
}

int
FullTcpAgent::into_bounded_priority(int prio) {
	if (prio_num_ != 2 && prio_num_ != 4 && prio_num_ != 8) {
		fprintf(stderr, "wrong number or priority class %d\n", prio_num_);
		return 0;
	}
	for (int i = 1; i < prio_num_; i++)
		if (prio <= prio_cap_[i * 8 / prio_num_ - 1])
		{
			return i - 1;
		}

	return prio_num_ - 1;
}

int FullTcpAgent::piasPrio(int bytes_sent)
{
    if(enable_pias_&&pias_prio_num_>=1)
    {
        pias_prio_num_=min(pias_prio_num_,8); //no more than 8 priorities now
        for(int i=0; i<pias_prio_num_-1; i++) //there are pias_prio_num-1 demotion thresholds in total
        {
            if(bytes_sent<=pias_thresh_[i])
                return i;
        }
        return pias_prio_num_-1;    //lowest priority by default
    }
    else
    {
        return 0;
    }
}

/*
 * sendpacket:
 *	allocate a packet, fill in header fields, and send
 *	also keeps stats on # of data pkts, acks, re-xmits, etc
 *
 * fill in packet fields.  Agent::allocpkt() fills
 * in most of the network layer fields for us.
 * So fill in tcp hdr and adjust the packet size.
 *
 * Also, set the size of the tcp header.
 */
void
FullTcpAgent::sendpacket(int seqno, int ackno, TcpFlags pflags, int datalen, TcpXmissionReason reason, Packet *p) {
    if (!p) p = allocpkt();

    hdr_tcp *tcph = hdr_tcp::access(p);
    hdr_ip *iph = hdr_ip::access(p);

    /* build basic header w/options */

    tcph->seqno() = seqno;
    tcph->ackno() = ackno;
    tcph->flags() = static_cast<int>(pflags);
    tcph->reason() = reason; // make tcph->reason look like ns1 pkt->flags?
    tcph->sa_length() = 0;    // may be increased by build_options()
    tcph->hlen() = tcpip_base_hdr_size_;
    tcph->hlen() += build_options(tcph);
    //Shuang: reduce header length
    //tcph->hlen() = 1;

    //iph->prio() = curseq_ - seqno + 10;

    ecn_processor().on_sendpacket(pflags, p, datalen > 0);
    /* actual size is data length plus header length */

    hdr_cmn *ch = hdr_cmn::access(p);
    ch->size() = datalen + tcph->hlen();

    if (datalen <= 0) {
        ++nackpack_;
        //Shuang: artifically reduce ack size
        //ch->size() = 1;
    } else {
        ++ndatapack_;
        ndatabytes_ += datalen;
        last_send_time_ = now();    // time of last data
    }
    if (reason == TcpXmissionReason::TIMEOUT || reason == TcpXmissionReason::DUPACK || reason == TcpXmissionReason::SACK) {
        ++nrexmitpack_;
        nrexmitbytes_ += datalen;
    }
    last_ack_sent_ = ackno;


    if (enable_pias_) {
        iph->prio() = calc_pias_priority(seqno, datalen, iph);
    } else {
        if (datalen > 0) {
            if (deadline != 0 && use_deadline) {
                iph->prio() = calc_deadline_priority();
            } else {
                auto max_seq = curseq_;
                if (use_true_remaining_size_ && true_flow_size_ >= 0) {
                    max_seq = startseq_ + true_flow_size_;
                }
                iph->prio() = calc_no_deadline_priority(seqno, max_seq);
            }
        }
    }

    send(p, nullptr);
}

int FullTcpAgent::calc_deadline_priority() {
    int tleft_us = deadline - int((now() - start_time) * 1e6);
    auto priority = get_expiration_time_us();
    if (tleft_us < 0 || get_num_bytes_remaining() * 8.0 / (link_rate_ * (1.e9 / 1.e6)) > tleft_us) {
        priority = get_lowest_priority();
    } else {
//       priority = iph->prio() / 40 * 1000 + calc_no_deadline_priority(seqno, curseq_) / 1460;
    }
    return priority;
}

int FullTcpAgent::get_expiration_time_us() const { 
    return deadline + int(start_time * 1e6); 
}

int FullTcpAgent::calc_pias_priority(int seqno, int datalen, hdr_ip *iph) {
    auto priority = 0;
    if (datalen > 0) {
        priority = piasPrio(seqno - startseq_);
        if (pias_debug_)
            printf("Packet prio is %d when bytes sent is %d\n", iph->prio(),
                   seqno - startseq_);
    }
    return priority;
}

//
// reset_rtx_timer: called during a retransmission timeout
// to perform exponential backoff.  Also, note that because
// we have performed a retransmission, our rtt timer is now
// invalidated (indicate this by setting rtt_active_ false)
//
void
FullTcpAgent::reset_rtx_timer(int /* mild */)
{
	// cancel old timer, set a new one
	/* if there is no outstanding data, don't back off rtx timer *
	 * (Fix from T. Kelly.) */
	if (!(highest_ack_ == maxseq_ && restart_bugfix_)) {
        	rtt_backoff();		// double current timeout
	}
        set_rtx_timer();	// set new timer
        rtt_active_ = FALSE;	// no timing during this window
}

/*
 * see if we should send a segment, and if so, send it
 * 	(may be ACK or data)
 * return the number of data bytes sent (count a SYN or FIN as 1 each)
 *
 * simulator var, desc (name in real TCP)
 * --------------------------------------
 * maxseq_, largest seq# we've sent plus one (snd_max)
 * flags_, flags regarding our internal state (t_state)
 * pflags, a local used to build up the tcp header flags (flags)
 * curseq_, is the highest sequence number given to us by "application"
 * highest_ack_, the highest ACK we've seen for our data (snd_una-1)
 * seqno, the next seq# we're going to send (snd_nxt)
 */
int
FullTcpAgent::foutput(int seqno_arg, TcpXmissionReason reason_arg)
{
    auto const seqno = seqno_arg;
    auto const reason = reason_arg;
	// if maxseg_ not set, set it appropriately
	// Q: how can this happen?

	if (maxseg_ == 0)
	       maxseg_ = size_;// Mohammad: changed from size_  - headersize();
	// Mohamad: commented the else condition
	// which is unnecessary and conflates with
	// tcp.cc
	//else
	//	size_ =  maxseg_ + headersize();

	int is_retransmit = (seqno < maxseq_);
	int quiet = (highest_ack_ == maxseq_);
	TcpFlags pflags = outflags();
	auto syn = (seqno == iss_);
	int emptying_buffer = FALSE;
	int buffered_bytes = (infinite_send_) ? TCP_MAXSEQ :
				curseq_ - highest_ack_ + 1;
//printf("buffered bytes= %d now= %lf fid= %d cwnd= %d\n", buffered_bytes,now(),fid_,(int)cwnd_);
	int win = window() * maxseg_;	// window (in bytes)
	if (prob_mode_ && win > 1)
	  win = 1;

	int off = seqno - highest_ack_;	// offset of seg in window
	int datalen;
	//int amtsent = 0;

	// be careful if we have not received any ACK yet
	if (highest_ack_ < 0) {
		if (!infinite_send_)
			buffered_bytes = curseq_ - iss_;;
		off = seqno - iss_;
	}

	if (syn && !data_on_syn_)
		datalen = 0;
	else if (pipectrl_)
		datalen = buffered_bytes - off;
	else
		datalen = min(buffered_bytes, win) - off;

	if ((signal_on_empty_) && (!buffered_bytes) && (!syn)) {
        bufferempty();
	}
	//
	// in real TCP datalen (len) could be < 0 if there was window
	// shrinkage, or if a FIN has been sent and neither ACKd nor
	// retransmitted.  Only this 2nd case concerns us here...
	//
	if (datalen < 0) {
		datalen = 0;
	} else if (datalen > maxseg_) {
		datalen = maxseg_;
	}


	//
	// this is an option that causes us to slow-start if we've
	// been idle for a "long" time, where long means a rto or longer
	// the slow-start is a sort that does not set ssthresh
	//

	if (slow_start_restart_ && quiet && datalen > 0) {
		if (idle_restart()) {
			slowdown(TcpSlowdownFlags::CWND_INIT);
        }
	}

	//
	// see if sending this packet will empty the send buffer
	// a dataless SYN packet counts also
	//

	if (!infinite_send_ && ((seqno + datalen) > curseq_ || (syn && datalen == 0))) {
		emptying_buffer = TRUE;
		//
		// if not a retransmission, notify application that
		// everything has been sent out at least once.
		//
		if (!syn) {
			idle();
			if (close_on_empty_ && quiet) {
				flags_ |= TF_NEEDCLOSE;
			}
		}
		pflags |= TcpFlags::PUSH;
		//
		// if close_on_empty set, we are finished
		// with this connection; close it
		//
	} else  {
		/* not emptying buffer, so can't be FIN */
		pflags &= ~TcpFlags::FIN;
	}
	if (infinite_send_ && (syn && datalen == 0))
		pflags |= TcpFlags::PUSH;  // set PUSH for dataless SYN

	/* sender SWS avoidance (Nagle) */

	if (datalen > 0) {
		// if full-sized segment, ok
		if (datalen == maxseg_)
			goto send;
		// if Nagle disabled and buffer clearing, ok
		if ((quiet || nodelay_)  && emptying_buffer)
			goto send;
		// if a retransmission
		if (is_retransmit)
			goto send;
		// if big "enough", ok...
		//	(this is not a likely case, and would
		//	only happen for tiny windows)
		if (datalen >= ((wnd_ * maxseg_) / 2.0))
			goto send;
		//Shuang
		if (datalen == 1 && prob_mode_)
			goto send;
	}

	if (need_send()){
		goto send;
	}

	/*
	 * send now if a control packet or we owe peer an ACK
	 * TF_ACKNOW can be set during connection establishment and
	 * to generate acks for out-of-order data
	 */

	if ((flags_ & (TF_ACKNOW|TF_NEEDCLOSE)) ||
			contains_any(pflags, TcpFlags::SYN | TcpFlags::FIN)) {
		goto send;
	}

    if (last_eof_time_) {
        std::cout << "Forced retransmission failed! " 
            << " src: " << here_.addr_  << " dst: " << dst_.addr_ 
            << " fid: " << fid_ << " maxseq: " << maxseq_ 
            << " seqno: " << seqno << " curseq: " << curseq_
            << " t_seqno: " << t_seqno_ << " highest_ack: " << highest_ack_
            << " win: " << win << " datalen: " << datalen
            << std::endl;
    }
        /*
         * No reason to send a segment, just return.
         */
	return 0;

send:

	// is a syn or fin?
	syn = contains_all(pflags, TcpFlags::SYN);

	auto fin = contains_all(pflags, TcpFlags::FIN);

	ecn_processor().modify_pflags_on_foutput(pflags, is_retransmit);

    /* moved from sendpacket()  -M. Weigle 6/19/02 */
    //
    // although CWR bit is ordinarily associated with ECN,
    // it has utility within the simulator for traces. Thus, set
    // it even if we aren't doing ECN
    //
    if (datalen > 0 && ecn_processor().has_sender_respondedn_to_congestion() && !is_retransmit) {
        pflags |= TcpFlags::CWR;
    }

        /*
         * Tack on the FIN flag to the data segment if close_on_empty_
         * was previously set-- avoids sending a separate FIN
         */
        if (flags_ & TF_NEEDCLOSE) {
                flags_ &= ~TF_NEEDCLOSE;
                if (state_ <= TcpState::ESTABLISHED && state_ != TcpState::CLOSED)
                {
                    pflags |= TcpFlags::FIN;
                    fin = 1;  /* FIN consumes sequence number */
                    newstate(TcpState::FIN_WAIT_1);
                }
        }

    if (last_eof_time_ && *last_eof_time_ != Scheduler::instance().clock()) {
        std::cout << "Forced retransmission failed! [post] " 
            << " src: " << here_.addr_  << " dst: " << dst_.addr_ 
            << " fid: " << fid_ << " maxseq: " << maxseq_ 
            << " seqno: " << seqno << " curseq: " << curseq_
            << " t_seqno: " << t_seqno_ << " highest_ack: " << highest_ack_
            << " win: " << win << " datalen: " << datalen
            << std::endl;
    }
    last_eof_time_ = std::nullopt;

	sendpacket(seqno, rcv_nxt_, pflags, datalen, reason);

        /*
         * Data sent (as far as we can tell).
         * Any pending ACK has now been sent.
         */
	flags_ &= ~(TF_ACKNOW|TF_DELACK);

	// Mohammad
	delack_timer_.force_cancel();
	/*
	if (datalen == 0)
	        printf("%f -- %s sent ACK for %d, canceled delack\n", this->name(), Scheduler::instance().clock(), rcv_nxt_);
	*/

	/*
	 * if we have reacted to congestion recently, the
	 * slowdown() procedure will have set cong_action_ and
	 * sendpacket will have copied that to the outgoing pkt
	 * CWR field. If that packet contains data, then
	 * it will be reliably delivered, so we are free to turn off the
	 * cong_action_ state now  If only a pure ACK, we keep the state
	 * around until we actually send a segment
	 */

	auto const reliable = datalen + syn + fin; // seq #'s reliably sent
	/*
	 * Don't reset cong_action_ until we send new data.
	 * -M. Weigle 6/19/02
	 */
	if (ecn_processor().cong_action_ && reliable > 0 && !is_retransmit)
		ecn_processor().cong_action_ = FALSE;

	// highest: greatest sequence number sent + 1
	//	and adjusted for SYNs and FINs which use up one number

	auto const highest = seqno + reliable;
	ecn_processor().on_foutput(highest);
	if (highest > maxseq_) {
		maxseq_ = highest;
		//
		// if we are using conventional RTT estimation,
		// establish timing on this segment
		//
		if (!ts_option_ && rtt_active_ == FALSE) {
			rtt_active_ = TRUE;	// set timer
			rtt_seq_ = seqno; 	// timed seq #
			rtt_ts_ = now();	// when set
		}
	}

	/*
	 * Set retransmit timer if not currently set,
	 * and not doing an ack or a keep-alive probe.
	 * Initial value for retransmit timer is smoothed
	 * round-trip time + 2 * round-trip time variance.
	 * Future values are rtt + 4 * rttvar.
	 */
	if (rtx_timer_.status() != TimerStatus::PENDING && reliable) {
		set_rtx_timer();  // no timer pending, schedule one
	}

	return (reliable);
}

/*
 *
 * send_much: send as much data as we are allowed to.  This is
 * controlled by the "pipectrl_" variable.  If pipectrl_ is set
 * to FALSE, then we are working as a normal window-based TCP and
 * we are allowed to send whatever the window allows.
 * If pipectrl_ is set to TRUE, then we are allowed to send whatever
 * pipe_ allows us to send.  One tricky part is to make sure we
 * do not overshoot the receiver's advertised window if we are
 * in (pipectrl_ == TRUE) mode.
 */

void
FullTcpAgent::send_much(int force, TcpXmissionReason reason, int maxburst)
{
	int npackets = 0;	// sent so far

	if (!force && (delsnd_timer_.status() == TimerStatus::PENDING))
		return;

	while (true) {

		/*
		 * note that if output decides to not actually send
		 * (e.g. because of Nagle), then if we don't break out
		 * of this loop, we can loop forever at the same
		 * simulated time instant
		 */
		int amt;
		int seq = nxt_tseq();


		if (!force && !send_allowed(seq))
			break;
		// Q: does this need to be here too?
		if (!force && overhead_ != 0 &&
		    (delsnd_timer_.status() != TimerStatus::PENDING)) {
			delsnd_timer_.resched(Random::uniform(overhead_));
			return;
		}
		if ((amt = foutput(seq, reason)) <= 0) {
		        break;
		}
		if (contains_all(outflags(), TcpFlags::FIN))
			--amt;	// don't count FINs
        sent(seq, amt);
		force = 0;

		if (contains_any(outflags(), TcpFlags::SYN | TcpFlags::FIN) ||
		    (maxburst && ++npackets >= maxburst))
			break;
	}
}

/*
 * base TCP: we are allowed to send a sequence number if it
 * is in the window
 */
int
FullTcpAgent::send_allowed(int seq)
{
        int win = window() * maxseg_;
		//Shuang: probe_mode
		if (prob_mode_ && win > 1)
			win = 1;
        int topwin = curseq_; // 1 seq number past the last byte we can send

        if ((topwin > highest_ack_ + win) || infinite_send_)
                topwin = highest_ack_ + win;

	return (seq < topwin);
}
/*
 * Process an ACK
 *	this version of the routine doesn't necessarily
 *	require the ack to be one which advances the ack number
 *
 * if this ACKs a rtt estimate
 *	indicate we are not timing
 *	reset the exponential timer backoff (gamma)
 * update rtt estimate
 * cancel retrans timer if everything is sent and ACK'd, else set it
 * advance the ack number if appropriate
 * update segment to send next if appropriate
 */
void
FullTcpAgent::newack(Packet* pkt)
{

   	//Shuang: cancel prob_mode_ when receiving an ack
    prob_mode_ = false;
    prob_count_ = 0;

	hdr_tcp *tcph = hdr_tcp::access(pkt);

	auto const ackno = tcph->ackno();
	int progress = (ackno > highest_ack_);

	if (ackno == maxseq_) {
		cancel_rtx_timer();	// all data ACKd
	} else if (progress) {
		set_rtx_timer();
	}

	// advance the ack number if this is for new data
	if (progress) {
	    prev_highest_ack_ = highest_ack_;
		highest_ack_ = ackno;
	}

	// if we have suffered a retransmit timeout, t_seqno_
	// will have been reset to highest_ ack.  If the
	// receiver has cached some data above t_seqno_, the
	// new-ack value could (should) jump forward.  We must
	// update t_seqno_ here, otherwise we would be doing
	// go-back-n.

	if (t_seqno_ < highest_ack_)
		t_seqno_ = highest_ack_; // seq# to send next

        /*
         * Update RTT only if it's OK to do so from info in the flags header.
         * This is needed for protocols in which intermediate agents

         * in the network intersperse acks (e.g., ack-reconstructors) for
         * various reasons (without violating e2e semantics).
         */
        hdr_flags *fh = hdr_flags::access(pkt);

	if (!fh->no_ts_) {
		if (ts_option_) {
			recent_age_ = now();
			recent_ = tcph->ts();
			rtt_update(now() - tcph->ts_echo());
			if (ts_resetRTO_ && ecn_processor().can_reset_rto_backoff_tcp(pkt)) {
				// From Andrei Gurtov
				//
				// Don't end backoff if still in ECN-Echo with
				// a congestion window of 1 packet.
				t_backoff_ = 1;
			}
		} else if (rtt_active_ && ackno > rtt_seq_) {
			// got an RTT sample, record it
			// "t_backoff_ = 1;" deleted by T. Kelly.
			rtt_active_ = FALSE;
			rtt_update(now() - rtt_ts_);
		}
		if (ecn_processor().can_reset_rto_backoff_tcp(pkt)) {
			/*
			 * Don't end backoff if still in ECN-Echo with
			 * a congestion window of 1 packet.
			 * Fix from T. Kelly.
			 */
			t_backoff_ = 1;
			ecn_processor().reset_rto_backoff_tcp();
		}

	}
	return;
}



/*
 * this is the simulated form of the header prediction
 * predicate.  While not really necessary for a simulation, it
 * follows the code base more closely and can sometimes help to reveal
 * odd behavior caused by the implementation structure..
 *
 * Here's the comment from the real TCP:
 *
 * Header prediction: check for the two common cases
 * of a uni-directional data xfer.  If the packet has
 * no control flags, is in-sequence, the window didn't
 * change and we're not retransmitting, it's a
 * candidate.  If the length is zero and the ack moved
 * forward, we're the sender side of the xfer.  Just
 * free the data acked & wake any higher level process
 * that was blocked waiting for space.  If the length
 * is non-zero and the ack didn't move, we're the
 * receiver side.  If we're getting packets in-order
 * (the reassembly queue is empty), add the data to
 * the socket buffer and note that we need a delayed ack.
 * Make sure that the hidden state-flags are also off.
 * Since we check for TcpState::ESTABLISHED above, it can only
 * be TF_NEEDSYN.
 */

int
FullTcpAgent::predict_ok(Packet* pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);
        hdr_flags *fh = hdr_flags::access(pkt);

	/* not the fastest way to do this, but perhaps clearest */

	int p1 = (state_ == TcpState::ESTABLISHED);		// ready
	int p2 = contains_all(TcpFlags{tcph->flags()}, TcpFlags::ACK) 
			&& !contains_any(TcpFlags{tcph->flags()}, TcpFlags::SYN | TcpFlags::FIN); // ACK
	int p3 = ((flags_ & TF_NEEDFIN) == 0);		// don't need fin
	int p4 = (!ts_option_ || fh->no_ts_ || (tcph->ts() >= recent_)); // tsok
	int p5 = (tcph->seqno() == rcv_nxt_);		// in-order data
	int p6 = (t_seqno_ == maxseq_);			// not re-xmit
        int p7 = (ecn_processor().predict_ok(pkt)); // no ECN
        int p8 = (tcph->sa_length() == 0);		// no SACK info

	return (p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8);
}

/*
 * fast_retransmit using the given seqno
 *	perform fast RTX, set recover_, set last_cwnd_action
 */

int
FullTcpAgent::fast_retransmit(int seq)
{
	// we are now going to fast-retransmit and willtrace that event
	recover_ = maxseq_;	// recovery target
	last_cwnd_action_ = CongestionWindowAdjustmentReason::DUPACK;
	return foutput(seq, TcpXmissionReason::DUPACK);	// send one pkt
}

/*
 * real tcp determines if the remote
 * side should receive a window update/ACK from us, and often
 * results in sending an update every 2 segments, thereby
 * giving the familiar 2-packets-per-ack behavior of TCP.
 * Here, we don't advertise any windows, so we just see if
 * there's at least 'segs_per_ack_' pkts not yet acked
 *
 * also, provide for a segs-per-ack "threshold" where
 * we generate 1-ack-per-seg until enough stuff
 * (spa_thresh_ bytes) has been received from the other side
 * This idea came from vj/kmn in BayTcp.  Added 8/21/01.
 */

int
FullTcpAgent::need_send()
{
	if (flags_ & TF_ACKNOW)
		return TRUE;

	return ((rcv_nxt_ - last_ack_sent_) > 0);
}

/*
 * determine whether enough time has elapsed in order to
 * conclude a "restart" is necessary (e.g. a slow-start)
 *
 * for now, keep track of this similarly to how rtt_update() does
 */

int
FullTcpAgent::idle_restart()
{
	if (last_send_time_ < 0.0) {
		// last_send_time_ isn't set up yet, we shouldn't
		// do the idle_restart
		return (0);
	}

	double tao = now() - last_send_time_;
	if (!ts_option_) {
                double tickoff = fmod(last_send_time_ + boot_time_,
			tcp_tick_);
                tao = int((tao + tickoff) / tcp_tick_) * tcp_tick_;
	}

	return (tao > t_rtxcur_);  // verify this CHECKME
	//return (tao > (int(t_srtt_) >> T_SRTT_BITS)*tcp_tick_); //Mohammad

}

/*
 * tcp-full's version of set_initial_window()... over-rides
 * the one in tcp.cc
 */
void
FullTcpAgent::set_initial_window()
{
	syn_ = TRUE;	// full-tcp always models SYN exchange
	TcpAgent::set_initial_window();
}

/*
 * main reception path -
 * called from the agent that handles the data path below in its muxing mode
 * advance() is called when connection is established with size sent from
 * user/application agent
 *
 * This is a fairly complex function.  It operates generally as follows:
 *	do header prediction for simple cases (pure ACKS or data)
 *	if in LISTEN and we get a SYN, begin initializing connection
 *	if in SYN_SENT and we get an ACK, complete connection init
 *	trim any redundant data from received dataful segment
 *	deal with ACKS:
 *		if in SYN_RCVD, complete connection init then go on
 *		see if ACK is old or at the current highest_ack
 *		if at current high, is the threshold reached or not
 *		if so, maybe do fast rtx... otherwise drop or inflate win
 *	deal with incoming data
 *	deal with FIN bit on in arriving packet
 */
void
FullTcpAgent::recv(Packet *pkt, Handler*)
{
	//Shuang: cancel probe mode
		prob_mode_ = false;
		prob_count_ = 0;

	hdr_tcp *tcph = hdr_tcp::access(pkt);	// TCP header
	hdr_cmn *th = hdr_cmn::access(pkt);	// common header (size, etc)
	hdr_flags *fh = hdr_flags::access(pkt);	// flags (CWR, CE, bits)

	int needoutput = FALSE;
	int ourfinisacked = FALSE;
	int dupseg = FALSE;			// recv'd dup data segment
	int todrop = 0;				// duplicate DATA cnt in seg

	last_state_ = state_;

	int datalen = th->size() - tcph->hlen(); // # payload bytes
	int ackno = tcph->ackno();		 // ack # from packet
	TcpFlags tiflags = TcpFlags{tcph->flags()}; 		 // tcp flags from packet

	/*
	 * Acknowledge FIN from passive closer even in TcpState::CLOSED state
	 * (since we lack TIME_WAIT state and RST packets,
	 * the loss of the FIN packet from the passive closer will make that
	 * endpoint retransmit the FIN forever)
	 * -F. Hernandez-Campos 8/6/00
	 */
	if ( (state_ == TcpState::CLOSED) && contains_all(tiflags,TcpFlags::FIN)) {
		goto dropafterack;
	}

	/*
	 * Don't expect to see anything while closed
	 */

	if (state_ == TcpState::CLOSED) {
                if (debug_) {
		        fprintf(stderr, "%f: FullTcp(%s): recv'd pkt in CLOSED state: ",
			    now(), name());
		        prpkt(pkt);
                }
		goto drop;
	}

	/*
	 *  Shuang: if fid does not match, drop packets
	 */
	if (fid_ != hdr_ip::access(pkt)->fid_) {
		goto drop;
	}

        /*
         * Process options if not in LISTEN state,
         * else do it below
         */
	if (state_ != TcpState::LISTEN)
		dooptions(pkt);

	/*
	 * if we are using delayed-ACK timers and
	 * no delayed-ACK timer is set, set one.
	 * They are set to fire every 'interval_' secs, starting
	 * at time t0 = (0.0 + k * interval_) for some k such
	 * that t0 > now
	 */
	/*
	 *Mohammad: commented this out for more efficient
	 * delayed ack generation
	 */
	/*if (delack_interval_ > 0.0 &&
	    (delack_timer_.status() != TimerStatus::PENDING)) {
		int last = int(now() / delack_interval_);
		delack_timer_.resched(delack_interval_ * (last + 1.0) - now());
		}*/


    if (tiflags != TcpFlags{tcph->flags()}) {
        throw std::logic_error("ecn processor assumes tcp flags are takeng from packet");
    }
	ecn_processor().on_receive(pkt);
	// TODO: might need to be moved to ecn_processor

	/*
	 * Try header prediction: in seq data or in seq pure ACK
	 *	with no funny business
	 */
	if (!nopredict_ && predict_ok(pkt)) {
                /*
                 * If last ACK falls within this segment's sequence numbers,
                 * record the timestamp.
		 * See RFC1323 (now RFC1323 bis)
                 */
                if (ts_option_ && !fh->no_ts_ &&
		    tcph->seqno() <= last_ack_sent_) {
			/*
			 * this is the case where the ts value is newer than
			 * the last one we've seen, and the seq # is the one
			 * we expect [seqno == last_ack_sent_] or older
			 */
			recent_age_ = now();
			recent_ = tcph->ts();
                }

		//
		// generate a stream of ecnecho bits until we see a true
		// cong_action bit
		//

		ecn_processor().on_receive_predict_ok(pkt, datalen > 0);
		// Header predication basically looks to see
		// if the incoming packet is an expected pure ACK
		// or an expected data segment

		if (datalen == 0) {
			// check for a received pure ACK in the correct range..
			// also checks to see if we are wnd_ limited
			// (we don't change cwnd at all below), plus
			// not being in fast recovery and not a partial ack.
			// If we are in fast
			// recovery, go below so we can remember to deflate
			// the window if we need to
			if (ackno > highest_ack_ && ackno < maxseq_ &&
			    cwnd_ >= wnd_ && !fastrecov_) {
				newack(pkt);	// update timers,  highest_ack_
				send_much(0, TcpXmissionReason::NORMAL, maxburst_);
				Packet::free(pkt);
				return;
			}
		} else if (ackno == highest_ack_ && rq_.empty()) {
			// check for pure incoming segment
			// the next data segment we're awaiting, and
			// that there's nothing sitting in the reassem-
			// bly queue
			// 	give to "application" here
			//	note: DELACK is inspected only by
			//	tcp_fasttimo() in real tcp.  Every 200 ms
			//	this routine scans all tcpcb's looking for
			//	DELACK segments and when it finds them
			//	changes DELACK to ACKNOW and calls tcp_output()

			ecn_processor().on_receive_pure_incoming();
			rcv_nxt_ += datalen;

			flags_ |= TF_DELACK;
			// Mohammad
			delack_timer_.resched(delack_interval_);

			recvBytes(datalen); // notify application of "delivery"

			if (flow_remaining_ > 0)
			        flow_remaining_ -= datalen; // Mohammad

			if (flow_remaining_ == 0) {
			        flags_ |= TF_ACKNOW;
				flow_remaining_ = -1;
			}

			//
			// special code here to simulate the operation
			// of a receiver who always consumes data,
			// resulting in a call to tcp_output
			Packet::free(pkt);
			if (need_send()){
				send_much(1, TcpXmissionReason::NORMAL, maxburst_);
			}
			return;
		}
	} /* header prediction */


	//
	// header prediction failed
	// (e.g. pure ACK out of valid range, SACK present, etc)...
	// do slow path processing

	//
	// the following switch does special things for these states:
	//	TcpState::LISTEN, TcpState::SYN_SENT
	//

	switch (state_) {

    /*
     * If the segment contains an ACK then it is bad and do reset.
     * If it does not contain a SYN then it is not interesting; drop it.
     * Otherwise initialize tp->rcv_nxt, and tp->irs, iss is already
     * selected, and send a segment:
     *     <SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
     * Initialize tp->snd_nxt to tp->iss.
     * Enter SYN_RECEIVED state, and process any other fields of this
     * segment in this state.
     */

    case TcpState::LISTEN:	/* awaiting peer's SYN */

		if (contains_all(tiflags, TcpFlags::ACK)) {
                        if (debug_) {
		    	        fprintf(stderr,
		    		"%f: FullTcpAgent(%s): warning: recv'd ACK while in LISTEN: ",
				    now(), name());
			        prpkt(pkt);
                        }
			// don't want ACKs in LISTEN
			goto dropwithreset;
		}
		if (!contains_all(tiflags, TcpFlags::SYN)) {
                        if (debug_) {
		    	        fprintf(stderr, "%f: FullTcpAgent(%s): warning: recv'd NON-SYN while in LISTEN\n",
				now(), name());
			        prpkt(pkt);
                        }
			// any non-SYN is discarded
			goto drop;
		}

		/*
		 * must by a SYN (no ACK) at this point...
		 * in real tcp we would bump the iss counter here also
		 */
		dooptions(pkt);
		irs_ = tcph->seqno();
		t_seqno_ = iss_; /* tcp_sendseqinit() macro in real tcp */
		rcv_nxt_ = rcvseqinit(irs_, datalen);
		flags_ |= TF_ACKNOW;

		ecn_processor().on_receive_syn(fh);

		if (fid_ == 0) {
			// XXX: sort of hack... If we do not
			// have a special flow ID, pick up that
			// of the sender (active opener)
			hdr_ip* iph = hdr_ip::access(pkt);
			fid_ = iph->flowid();
		}

		newstate(TcpState::SYN_RECEIVED);
		goto trimthenstep6;

        /*
         * If the state is SYN_SENT:
         *      if seg contains an ACK, but not for our SYN, drop the input.
         *      if seg does not contain SYN, then drop it.
         * Otherwise this is an acceptable SYN segment
         *      initialize tp->rcv_nxt and tp->irs
         *      if seg contains ack then advance tp->snd_una
         *      if SYN has been acked change to ESTABLISHED else SYN_RCVD state
         *      arrange for segment to be acked (eventually)
         *      continue processing rest of data/controls, beginning with URG
         */

    case TcpState::SYN_SENT:	/* we sent SYN, expecting SYN+ACK (or SYN) */

		/* drop if it's a SYN+ACK and the ack field is bad */
		if (contains_all(tiflags, TcpFlags::ACK) &&
			((ackno <= iss_) || (ackno > maxseq_))) {
			// not an ACK for our SYN, discard
                        if (debug_) {
			       fprintf(stderr, "%f: FullTcpAgent::recv(%s): bad ACK for our SYN: ",
			        now(), name());
			        prpkt(pkt);
                        }
			goto dropwithreset;
		}

		if (!contains_all(tiflags, TcpFlags::SYN)) {
                        if (debug_) {
			        fprintf(stderr, "%f: FullTcpAgent::recv(%s): no SYN for our SYN: ",
			        now(), name());
			        prpkt(pkt);
                        }
			goto drop;
		}

		if (contains_all(tiflags, TcpFlags::ACK))
		{
			if (ecn_processor().on_receive_syn_ack_pre(fh) 
					== EcnProcessor::EcnDecision::CONGESTION_DROP) {
				goto drop;
			}
		}


#ifdef notdef
cancel_rtx_timer();	// cancel timer on our 1st SYN [does this belong!?]
#endif
		irs_ = tcph->seqno();	// get initial recv'd seq #
		rcv_nxt_ = rcvseqinit(irs_, datalen);

		if (contains_all(tiflags, TcpFlags::ACK)) {
			// SYN+ACK (our SYN was acked)
			ecn_processor().on_receive_syn_ack(fh);
			highest_ack_ = ackno;
			cwnd_ = initial_window();

#ifdef notdef
            /*
             * if we didn't have to retransmit the SYN,
             * use its rtt as our initial srtt & rtt var.
             */
            if (t_rtt_) {
                double tao = now() - tcph->ts();
                rtt_update(tao);
            }
#endif
			/*
			 * if there's data, delay ACK; if there's also a FIN
			 * ACKNOW will be turned on later.
			 */
			if (datalen > 0) {
				flags_ |= TF_DELACK;	// data there: wait
				// Mohammad
				delack_timer_.resched(delack_interval_);
			} else {
				flags_ |= TF_ACKNOW;	// ACK peer's SYN
			}

			/*
			 * Received <SYN,ACK> in SYN_SENT[*] state.
			 * Transitions:
			 *      SYN_SENT  --> ESTABLISHED
			 *      SYN_SENT* --> FIN_WAIT_1
			 */

			if (flags_ & TF_NEEDFIN) {
				newstate(TcpState::FIN_WAIT_1);
				flags_ &= ~TF_NEEDFIN;
				tiflags &= ~TcpFlags::SYN;
			} else {
				newstate(TcpState::ESTABLISHED);
			}

			// special to ns:
			//  generate pure ACK here.
			//  this simulates the ordinary connection establishment
			//  where the ACK of the peer's SYN+ACK contains
			//  no data.  This is typically caused by the way
			//  the connect() socket call works in which the
			//  entire 3-way handshake occurs prior to the app
			//  being able to issue a write() [which actually
			//  causes the segment to be sent].
			sendpacket(t_seqno_, rcv_nxt_, TcpFlags::ACK, 0, TcpXmissionReason::NORMAL);
		} else {
			ecn_processor().on_receive_syn(fh);

			// SYN (no ACK) (simultaneous active opens)
			flags_ |= TF_ACKNOW;
			cancel_rtx_timer();
			newstate(TcpState::SYN_RECEIVED);
			/*
			 * decrement t_seqno_: we are sending a
			 * 2nd SYN (this time in the form of a
			 * SYN+ACK, so t_seqno_ will have been
			 * advanced to 2... reduce this
			 */
			t_seqno_--;	// CHECKME
		}

trimthenstep6:
		/*
		 * advance the seq# to correspond to first data byte
		 */
		tcph->seqno()++;

		if (contains_all(tiflags, TcpFlags::ACK))
			goto process_ACK;

		goto step6;

    case TcpState::LAST_ACK:
		/*
		 * The only way we're in LAST_ACK is if we've already
		 * received a FIN, so ignore all retranmitted FINS.
		 * -M. Weigle 7/23/02
		 */
		if (contains_all(tiflags, TcpFlags::FIN)) {
			goto drop;
		}
		break;
    case TcpState::CLOSING:
		break;
    default:
		break;
	} /* end switch(state_) */

        /*
         * States other than LISTEN or SYN_SENT.
         * First check timestamp, if present.
         * Then check that at least some bytes of segment are within
         * receive window.  If segment begins before rcv_nxt,
         * drop leading data (and SYN); if nothing left, just ack.
         *
         * RFC 1323 PAWS: If we have a timestamp reply on this segment
         * and it's less than ts_recent, drop it.
         */

	if (ts_option_ && !fh->no_ts_ && recent_ && tcph->ts() < recent_) {
		if ((now() - recent_age_) > TCP_PAWS_IDLE) {
			/*
			 * this is basically impossible in the simulator,
			 * but here it is...
			 */
                        /*
                         * Invalidate ts_recent.  If this segment updates
                         * ts_recent, the age will be reset later and ts_recent
                         * will get a valid value.  If it does not, setting
                         * ts_recent to zero will at least satisfy the
                         * requirement that zero be placed in the timestamp
                         * echo reply when ts_recent isn't valid.  The
                         * age isn't reset until we get a valid ts_recent
                         * because we don't want out-of-order segments to be
                         * dropped when ts_recent is old.
                         */
			recent_ = 0.0;
		} else {
			fprintf(stderr, "%f: FullTcpAgent(%s): dropped pkt due to bad ts\n",
				now(), name());
			goto dropafterack;
		}
	}

	// check for redundant data at head/tail of segment
	//	note that the 4.4bsd [Net/3] code has
	//	a bug here which can cause us to ignore the
	//	perfectly good ACKs on duplicate segments.  The
	//	fix is described in (Stevens, Vol2, p. 959-960).
	//	This code is based on that correction.
	//
	// In addition, it has a modification so that duplicate segments
	// with dup acks don't trigger a fast retransmit when dupseg_fix_
	// is enabled.
	//
	// Yet one more modification: make sure that if the received
	//	segment had datalen=0 and wasn't a SYN or FIN that
	//	we don't turn on the ACKNOW status bit.  If we were to
	//	allow ACKNOW to be turned on, normal pure ACKs that happen
	//	to have seq #s below rcv_nxt can trigger an ACK war by
	//	forcing us to ACK the pure ACKs
	//
	// Update: if we have a dataless FIN, don't really want to
	// do anything with it.  In particular, would like to
	// avoid ACKing an incoming FIN+ACK while in CLOSING
	//
	todrop = rcv_nxt_ - tcph->seqno();  // how much overlap?

	if (todrop > 0 && (contains_all(tiflags, TcpFlags::SYN) || datalen > 0)) {
		if (contains_all(tiflags, TcpFlags::SYN)) {
			tiflags &= ~TcpFlags::SYN;
			tcph->seqno()++;
			th->size()--;	// XXX Must decrease packet size too!!
					// Q: Why?.. this is only a SYN
			todrop--;
		}
		//
		// see Stevens, vol 2, p. 960 for this check;
		// this check is to see if we are dropping
		// more than this segment (i.e. the whole pkt + a FIN),
		// or just the whole packet (no FIN)
		//
		if ((todrop > datalen) ||
		    (todrop == datalen && !contains_all(tiflags, TcpFlags::FIN))) {
                        /*
                         * Any valid FIN must be to the left of the window.
                         * At this point the FIN must be a duplicate or out
                         * of sequence; drop it.
                         */

			tiflags &= ~TcpFlags::FIN;

			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */

			flags_ |= TF_ACKNOW;
			todrop = datalen;
			dupseg = TRUE;	// *completely* duplicate

		}

		/*
		 * Trim duplicate data from the front of the packet
		 */

		tcph->seqno() += todrop;
		th->size() -= todrop;	// XXX Must decrease size too!!
					// why? [kf]..prob when put in RQ
		datalen -= todrop;

	} /* data trim */

	/*
	 * If we are doing timstamps and this packet has one, and
	 * If last ACK falls within this segment's sequence numbers,
	 * record the timestamp.
	 * See RFC1323 (now RFC1323 bis)
	 */
	if (ts_option_ && !fh->no_ts_ && tcph->seqno() <= last_ack_sent_) {
		/*
		 * this is the case where the ts value is newer than
		 * the last one we've seen, and the seq # is the one we expect
		 * [seqno == last_ack_sent_] or older
		 */
		recent_age_ = now();
		recent_ = tcph->ts();
	}

	if (contains_all(tiflags, TcpFlags::SYN)) {
		if (debug_) {
			fprintf(stderr, "%f: FullTcpAgent::recv(%s) received unexpected SYN (state:%d): ",
				now(), name(), state_);
				prpkt(pkt);
		}
		goto dropwithreset;
	}

	if (!contains_all(tiflags, TcpFlags::ACK)) {
		/*
		 * Added check for state != SYN_RECEIVED.  We will receive a
		 * duplicate SYN in SYN_RECEIVED when our SYN/ACK was dropped.
		 * We should just ignore the duplicate SYN (our timeout for
		 * resending the SYN/ACK is about the same as the client's
		 * timeout for resending the SYN), but give no error message.
		 * -M. Weigle 07/24/01
		 */
		if (state_ != TcpState::SYN_RECEIVED) {
                        if (debug_) {
			        fprintf(stderr, "%f: FullTcpAgent::recv(%s) got packet lacking ACK (state:%d): ",
				now(), name(), state_);
			        prpkt(pkt);
                        }
		}
		goto drop;
	}

	/*
	 * Ack processing.
	 */

	switch (state_) {
    case TcpState::SYN_RECEIVED:
		/* want ACK for our SYN+ACK */
		if (ackno < highest_ack_ || ackno > maxseq_) {
			// not in useful range
                        if (debug_) {
		    	        fprintf(stderr, "%f: FullTcpAgent(%s): ack(%d) not in range while in SYN_RECEIVED: ",
			 	now(), name(), ackno);
			        prpkt(pkt);
                        }
			goto dropwithreset;
		}

		switch (ecn_processor().on_receive_syn_ack_ack(fh)) {
        case EcnProcessor::EcnDecision::CONGESTION_DROP:
			goto drop;
        case EcnProcessor::EcnDecision::CONGESTION:
            break;
        case EcnProcessor::EcnDecision::NO_CONGESTION:
            cwnd_ = initial_window();
            break;
		}
		/*
		 * Make transitions:
		 *      SYN-RECEIVED  -> ESTABLISHED
		 *      SYN-RECEIVED* -> FIN-WAIT-1
		 */
		if (flags_ & TF_NEEDFIN) {
			newstate(TcpState::FIN_WAIT_1);
			flags_ &= ~TF_NEEDFIN;
		} else {
			newstate(TcpState::ESTABLISHED);
		}

		/* fall into ... */


	/*
	 * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
	 * ACKs.  If the ack is in the range
	 *      tp->snd_una < ti->ti_ack <= tp->snd_max
	 * then advance tp->snd_una to ti->ti_ack and drop
	 * data from the retransmission queue.
	 *
	 * note that state TIME_WAIT isn't used
	 * in the simulator
	*/

    case TcpState::ESTABLISHED:
    case TcpState::FIN_WAIT_1:
    case TcpState::FIN_WAIT_2:
    case TcpState::CLOSE_WAIT:
    case TcpState::CLOSING:
    case TcpState::LAST_ACK: {

		//
		// look for ECNs in ACKs, react as necessary
		//

		ecn_processor().on_receive_ack_pre(pkt, datalen > 0);

		//
		// If ESTABLISHED or starting to close, process SACKS
		//

		if (state_ >= TcpState::ESTABLISHED && tcph->sa_length() > 0) {
			process_sack(tcph);
		}

		//
		// ACK indicates packet left the network
		//	try not to be fooled by data
		//

		if (fastrecov_ && (datalen == 0 || ackno > highest_ack_))
			pipe_ -= maxseg_;

		// look for dup ACKs (dup ack numbers, no data)
		//
		// do fast retransmit/recovery if at/past thresh
//if (ackno <= highest_ack_) printf("dupi= %d\n",(int)dupacks_);
//else printf("in fully\n");
		//Shuang:
//		if (ackno <= highest_ack_ && cur_sqtotal_ <= last_sqtotal_) {
		if (ackno <= highest_ack_) {
			// a pure ACK which doesn't advance highest_ack_
//printf("dupi= %d\n",dupacks_);
			if (datalen == 0 && (!dupseg_fix_ || !dupseg)) {

			        //Mohammad: check for dynamic dupack mode.
			         if (dynamic_dupack_ > 0.0) {
				        tcprexmtthresh_ = int(dynamic_dupack_ * window());
					if (tcprexmtthresh_ < 3)
					       tcprexmtthresh_ = 3;
				 }
				  /*
                                 * If we have outstanding data
                                 * this is a completely
                                 * duplicate ack,
                                 * the ack is the biggest we've
                                 * seen and we've seen exactly our rexmt
                                 * threshhold of them, assume a packet
                                 * has been dropped and retransmit it.
                                 *
                                 * We know we're losing at the current
                                 * window size so do congestion avoidance.
                                 *
                                 * Dup acks mean that packets have left the
                                 * network (they're now cached at the receiver)
                                 * so bump cwnd by the amount in the receiver
                                 * to keep a constant cwnd packets in the
                                 * network.
                                 */

				if ((rtx_timer_.status() != TimerStatus::PENDING) ||
				    ackno < highest_ack_) {
					// Q: significance of timer not pending?
					// ACK below highest_ack_
					oldack();
				} else if (++dupacks_ == tcprexmtthresh_) {
					// ACK at highest_ack_ AND meets threshold
					//trace_event("FAST_RECOVERY");
					//Shuang: dupack_action
					dupack_action(); // maybe fast rexmt
					goto drop;

				} else if (dupacks_ > tcprexmtthresh_) {
					// ACK at highest_ack_ AND above threshole
					//trace_event("FAST_RECOVERY");
					extra_ack();

					// send whatever window allows
					send_much(0, TcpXmissionReason::DUPACK, maxburst_);
					goto drop;
				}
			} else {
				// non zero-length [dataful] segment
				// with a dup ack (normal for dataful segs)
				// (or window changed in real TCP).
				if (dupack_reset_) {
					dupacks_ = 0;
					fastrecov_ = FALSE;
				}
			}
			break;	/* take us to "step6" */
		} /* end of dup/old acks */

		/*
		 * we've finished the fast retransmit/recovery period
		 * (i.e. received an ACK which advances highest_ack_)
		 * The ACK may be "good" or "partial"
		 */

process_ACK:

		if (ackno > maxseq_) {
			// ack more than we sent(!?)
                        if (debug_) {
			        fprintf(stderr, "%f: FullTcpAgent::recv(%s) too-big ACK (maxseq:%d): ",
				now(), name(), int(maxseq_));
			        prpkt(pkt);
                        }
			goto dropafterack;
		}

        /*
         * If we have a timestamp reply, update smoothed
         * round trip time.  If no timestamp is present but
         * transmit timer is running and timed sequence
         * number was acked, update smoothed round trip time.
         * Since we now have an rtt measurement, cancel the
         * timer backoff (cf., Phil Karn's retransmit alg.).
         * Recompute the initial retransmit timer.
         *
         * If all outstanding data is acked, stop retransmit
         * If there is more data to be acked, restart retransmit
         * timer, using current (possibly backed-off) value.
         */
		newack(pkt);	// handle timers, update highest_ack_

		/*
		 * if this is a partial ACK, invoke whatever we should
		 * note that newack() must be called before the action
		 * functions, as some of them depend on side-effects
		 * of newack()
		 */

		int partial = pack(pkt);

		if (partial)
			pack_action(pkt);
		else
			ack_action(pkt);

		ecn_processor().on_receive_ack(pkt, tiflags);

		// Mohammad
		/*if (Random::uniform(1) < ecnhat_alpha_ && !contains_all(tiflags, TcpFlags::SYN) ) {
			ecn(highest_ack_);
			if (cwnd_ < 1)
			 	set_rtx_timer();
				}*/

		// CHECKME: handling of rtx timer
		if (ackno == maxseq_) {
			needoutput = TRUE;
		}

		/*
		 * If no data (only SYN) was ACK'd,
		 *    skip rest of ACK processing.
		 */
		if (ackno == (highest_ack_ + 1))
			goto step6;

		// if we are delaying initial cwnd growth (probably due to
		// large initial windows), then only open cwnd if data has
		// been received
		// Q: check when this happens
		/*
		 * When new data is acked, open the congestion window.
		 * If the window gives us less than ssthresh packets
		 * in flight, open exponentially (maxseg per packet).
		 * Otherwise open about linearly: maxseg per window
		 * (maxseg^2 / cwnd per packet).
		 */
		if ((!delay_growth_ || (rcv_nxt_ > 0)) &&
				last_state_ == TcpState::ESTABLISHED) {
			if (!partial || open_cwnd_on_pack_) {
				if (ecn_processor().can_open_cwnd(pkt))
					opencwnd();
			}
		}

		ecn_processor().on_receive_newack(pkt);

		if ((state_ >= TcpState::FIN_WAIT_1) && (ackno == maxseq_)) {
			ourfinisacked = TRUE;
		}

		//
		// special additional processing when our state
		// is one of the closing states:
		//	FIN_WAIT_1, CLOSING, LAST_ACK

		switch (state_) {
                /*
                 * In FIN_WAIT_1 STATE in addition to the processing
                 * for the ESTABLISHED state if our FIN is now acknowledged
                 * then enter FIN_WAIT_2.
                 */
        case TcpState::FIN_WAIT_1:	/* doing active close */
			if (ourfinisacked) {
				// got the ACK, now await incoming FIN
				newstate(TcpState::FIN_WAIT_2);
				cancel_timers();
				needoutput = FALSE;
			}
			break;

                /*
                 * In CLOSING STATE in addition to the processing for
                 * the ESTABLISHED state if the ACK acknowledges our FIN
                 * then enter the TIME-WAIT state, otherwise ignore
                 * the segment.
                 */
        case TcpState::CLOSING:	/* simultaneous active close */;
			if (ourfinisacked) {
				newstate(TcpState::CLOSED);
				cancel_timers();
			}
			break;
                /*
                 * In LAST_ACK, we may still be waiting for data to drain
                 * and/or to be acked, as well as for the ack of our FIN.
                 * If our FIN is now acknowledged,
                 * enter the closed state and return.
                 */
        case TcpState::LAST_ACK:	/* passive close */
			// K: added state change here
			if (ourfinisacked) {
				newstate(TcpState::CLOSED);
				finish(); // cancels timers, erc
				reset(); // for connection re-use (bug fix from ns-users list)
				goto drop;
			} else {
				// should be a FIN we've seen
				if (debug_) {
					fprintf(stderr, "%f: FullTcpAgent(%s)::received non-ACK (state:%d): ",
						now(), name(), state_);
					prpkt(pkt);
				}
			}
			break;

		/* no case for TIME_WAIT in simulator */
		default:
			break;
		}  // inner state_ switch (closing states)
		break;
	}
	default:
		throw std::logic_error(
				fmt::format("Unexpected TCP state during ACK processing: {}", statestr(state_)));
	} // outer state_ switch (ack processing)

step6:

	/*
	 * Processing of incoming DATAful segments.
	 * 	Code above has already trimmed redundant data.
	 *
	 * real TCP handles window updates and URG data here also
	 */

/* dodata: this label is in the "real" code.. here only for reference */

	if ((datalen > 0 || contains_all(tiflags, TcpFlags::FIN)) &&
	    have_received_FIN(state_) == 0) {

		//
		// the following 'if' implements the "real" TCP
		// TCP_REASS macro
		//

		if (tcph->seqno() == rcv_nxt_ && rq_.empty()) {
			// got the in-order packet we were looking
			// for, nobody is in the reassembly queue,
			// so this is the common case...
			// note: in "real" TCP we must also be in
			// ESTABLISHED state to come here, because
			// data arriving before ESTABLISHED is
			// queued in the reassembly queue.  Since we
			// don't really have a process anyhow, just
			// accept the data here as-is (i.e. don't
			// require being in ESTABLISHED state)

			ecn_processor().on_receive_pure_incoming();
			flags_ |= TF_DELACK;
			// Mohammad
			delack_timer_.resched(delack_interval_);
			rcv_nxt_ += datalen;

			// printf("%f: receving data %d, rescheduling delayed ack\n", Scheduler::instance().clock(), rcv_nxt_);

			tiflags = TcpFlags{tcph->flags()} & TcpFlags::FIN;

			// give to "application" here
			// in "real" TCP, this is sbappend() + sorwakeup()
			if (datalen) {
				recvBytes(datalen); // notify app. of "delivery"

				//printf("flow_remaining before dec = %d\n" , flow_remaining_);
				if (flow_remaining_ > 0)
				      flow_remaining_ -= datalen; // Mohammad
				if (flow_remaining_ == 0) {
				      flags_ |= TF_ACKNOW;
				      flow_remaining_ = -1;
				}
				//printf("flow_remaining after dec = %d\n" , flow_remaining_);
       			}

			needoutput = need_send();
		} else {
			// see the "tcp_reass" function:
			// not the one we want next (or it
			// is but there's stuff on the reass queue);
			// do whatever we need to do for out-of-order
			// segments or hole-fills.  Also,
			// send an ACK (or SACK) to the other side right now.
			// Note that we may have just a FIN here (datalen = 0)

			/* Mohammad: the DCTCP receiver conveys the ECN-CE
			   received on each out-of-order data packet */

		        int rcv_nxt_old_ = rcv_nxt_; // notify app. if changes
			tiflags = reass(pkt);
			if (rcv_nxt_ > rcv_nxt_old_) {
				// if rcv_nxt_ has advanced, must have
				// been a hole fill.  In this case, there
				// is something to give to application
			        recvBytes(rcv_nxt_ - rcv_nxt_old_);

				//printf("flow_remaining before dec = %d\n" , flow_remaining_);
				if (flow_remaining_ > 0)
				       flow_remaining_ -= datalen; // Mohammad

				if (flow_remaining_ == 0) {
				       flags_ |= TF_ACKNOW;
				       flow_remaining_ = -1;
				}

				//printf("flow_remaining after dec = %d\n" , flow_remaining_);

			}
			flags_ |= TF_ACKNOW;

			if (contains_all(tiflags, TcpFlags::PUSH)) {
				//
				// ???: does this belong here
				// K: APPLICATION recv
				needoutput = need_send();
			}
		}
	} else {
		/*
		 * we're closing down or this is a pure ACK that
		 * wasn't handled by the header prediction part above
		 * (e.g. because cwnd < wnd)
		 */
		// K: this is deleted
		tiflags &= ~TcpFlags::FIN;
	}

	/*
	 * if FIN is received, ACK the FIN
	 * (let user know if we could do so)
	 */

	if (contains_all(tiflags, TcpFlags::FIN)) {
		if (have_received_FIN(state_) == 0) {
			flags_ |= TF_ACKNOW;
 			rcv_nxt_++;
		}
		switch (state_) {
        /*
         * In SYN_RECEIVED and ESTABLISHED STATES
         * enter the CLOSE_WAIT state.
         * (passive close)
         */
        case TcpState::SYN_RECEIVED:
        case TcpState::ESTABLISHED:
            newstate(TcpState::CLOSE_WAIT);
            break;

            /*
             * If still in FIN_WAIT_1 STATE FIN has not been acked so
             * enter the CLOSING state.
             * (simultaneous close)
             */
        case TcpState::FIN_WAIT_1:
            newstate(TcpState::CLOSING);
            break;
        /*
         * In FIN_WAIT_2 state enter the TIME_WAIT state,
         * starting the time-wait timer, turning off the other
         * standard timers.
         * (in the simulator, just go to CLOSED)
         * (completion of active close)
         */
        case TcpState::FIN_WAIT_2:
            newstate(TcpState::CLOSED);
            cancel_timers();
            break;
        default:
            throw std::logic_error(
					fmt::format("Unexpected TCP state with FIN bit set: {}", 
						statestr(state_)));
		}
	} /* end of if FIN bit on */

	if (needoutput || (flags_ & TF_ACKNOW))
		send_much(1, TcpXmissionReason::NORMAL, maxburst_);
	else if (curseq_ >= highest_ack_ || infinite_send_)
		send_much(0, TcpXmissionReason::NORMAL, maxburst_);
	// K: which state to return to when nothing left?

	if (!halfclose_ && state_ == TcpState::CLOSE_WAIT && highest_ack_ == maxseq_)
		usrclosed();

	Packet::free(pkt);

	// haoboy: Is here the place for done{} of active close?
	// It cannot be put in the switch above because we might need to do
	// send_much() (an ACK)
	if (state_ == TcpState::CLOSED)
		Tcl::instance().evalf("%s done", this->name());

	return;

	//
	// various ways of dropping (some also ACK, some also RST)
	//

dropafterack:
	flags_ |= TF_ACKNOW;
	send_much(1, TcpXmissionReason::NORMAL, maxburst_);
	goto drop;

dropwithreset:
	/* we should be sending an RST here, but can't in simulator */
	if (contains_all(tiflags, TcpFlags::ACK)) {
		sendpacket(ackno, 0, TcpFlags::NONE, 0, TcpXmissionReason::NORMAL);
	} else {
		int ack = tcph->seqno() + datalen;
		if (contains_all(tiflags, TcpFlags::SYN))
			ack--;
		sendpacket(0, ack, TcpFlags::ACK, 0, TcpXmissionReason::NORMAL);
	}
drop:
   	Packet::free(pkt);
	return;
}

/*
 * Dupack-action: what to do on a DUP ACK.  After the initial check
 * of 'recover' below, this function implements the following truth
 * table:
 *
 *      bugfix  ecn     last-cwnd == ecn        action
 *
 *      0       0       0                       full_reno_action
 *      0       0       1                       full_reno_action [impossible]
 *      0       1       0                       full_reno_action
 *      0       1       1                       1/2 window, return
 *      1       0       0                       nothing
 *      1       0       1                       nothing         [impossible]
 *      1       1       0                       nothing
 *      1       1       1                       1/2 window, return
 */

void
FullTcpAgent::dupack_action()
{

	int recovered = (highest_ack_ > recover_);

	fastrecov_ = TRUE;
	rtxbytes_ = 0;

	if (recovered || (!bug_fix_ && !ecn_processor().is_enabled())
			|| (last_cwnd_action_ == CongestionWindowAdjustmentReason::DUPACK)
			|| ( highest_ack_ == 0)) {
		goto full_reno_action;
	}

	if (ecn_processor().is_enabled() && last_cwnd_action_ == CongestionWindowAdjustmentReason::ECN) {
		slowdown(TcpSlowdownFlags::CWND_HALF);
		cancel_rtx_timer();
		rtt_active_ = FALSE;
		(void)fast_retransmit(highest_ack_);
		return;
	}

	if (bug_fix_) {
		// The line below, for "bug_fix_" true, avoids
		// problems with multiple fast retransmits in one
		// window of data.
		return;
}

full_reno_action:
        slowdown(TcpSlowdownFlags::SSTHRESH_HALF | TcpSlowdownFlags::CWND_HALF);
	cancel_rtx_timer();
	rtt_active_ = FALSE;
	recover_ = maxseq_;
	(void)fast_retransmit(highest_ack_);
	// we measure cwnd in packets,
	// so don't scale by maxseg_
	// as real TCP does
	cwnd_ = double(ssthresh_) + double(dupacks_);
       return;

}

void
FullTcpAgent::timeout_action()
{
	recover_ = maxseq_;

//	cwnd_ = 0.5 * cwnd_;
//Shuang: comment all below
	if (cwnd_ < 1.0) {
                if (debug_) {
	            fprintf(stderr, "%f: FullTcpAgent(%s):: resetting cwnd from %f to 1\n",
		    now(), name(), double(cwnd_));
                }
		cwnd_ = 1.0;
	}

	if (last_cwnd_action_ == CongestionWindowAdjustmentReason::ECN) {
		slowdown(TcpSlowdownFlags::CWND_ONE);
	} else {
		slowdown(TcpSlowdownFlags::SSTHRESH_HALF | TcpSlowdownFlags::CWND_RESTART);
		last_cwnd_action_ = CongestionWindowAdjustmentReason::TIMEOUT;
	}

	//cwnd_ = initial_window();
//	ssthresh_ = cwnd_;

	reset_rtx_timer(1);
	t_seqno_ = (highest_ack_ < 0) ? iss_ : int(highest_ack_);
	ecn_processor().on_timeout();

	//printf("%f, fid %d took timeout, cwnd_ = %f\n", now(), fid_, (double)cwnd_);
	fastrecov_ = FALSE;
	dupacks_ = 0;
}
/*
 * deal with timers going off.
 * 2 types for now:
 *	retransmission timer (rtx_timer_)
 *  delayed ack timer (delack_timer_)
 *	delayed send (randomization) timer (delsnd_timer_)
 *
 * real TCP initializes the RTO as 6 sec
 *	(A + 2D, where A=0, D=3), [Stevens p. 305]
 * and thereafter uses
 *	(A + 4D, where A and D are dynamic estimates)
 *
 * note that in the simulator t_srtt_, t_rttvar_ and t_rtt_
 * are all measured in 'tcp_tick_'-second units
 */

void
FullTcpAgent::timeout(int tno)
{

	/*
	 * Due to F. Hernandez-Campos' fix in recv(), we may send an ACK
	 * while in the CLOSED state.  -M. Weigle 7/24/01
	 */
	if (state_ == TcpState::LISTEN) {
	 	// shouldn't be getting timeouts here
                if (debug_) {
		        fprintf(stderr, "%f: FullTcpAgent(%s): unexpected timeout %d in state %s\n",
			now(), name(), tno, statestr(state_));
                }
		return;
	}

	switch (tno) {

	case TCP_TIMER_RTX:
                /* retransmit timer */
                ++nrexmit_;
                timeout_action();
		/* fall thru */
	case TCP_TIMER_DELSND:
		/* for phase effects */
                send_much(1, TcpXmissionReason::TIMEOUT, maxburst_);
		break;

	case TCP_TIMER_DELACK:
                if (flags_ & TF_DELACK) {
                        flags_ &= ~TF_DELACK;
                        flags_ |= TF_ACKNOW;
                        send_much(1, TcpXmissionReason::NORMAL, 0);
                }
		// Mohammad
                //delack_timer_.resched(delack_interval_);
		break;
	default:
		fprintf(stderr, "%f: FullTcpAgent(%s) Unknown Timeout type %d\n",
			now(), name(), tno);
	}
	return;
}

void
FullTcpAgent::dooptions(Packet* pkt)
{
	// interesting options: timestamps (here),
	//	CC, CCNEW, CCECHO (future work perhaps?)

        hdr_flags *fh = hdr_flags::access(pkt);
	hdr_tcp *tcph = hdr_tcp::access(pkt);

	if (ts_option_ && !fh->no_ts_) {
		if (tcph->ts() < 0.0) {
			fprintf(stderr,
			    "%f: FullTcpAgent(%s) warning: ts_option enabled in this TCP, but appears to be disabled in peer\n",
				now(), name());
		} else if (contains_all(TcpFlags{tcph->flags()}, TcpFlags::SYN)) {
			flags_ |= TF_RCVD_TSTMP;
			recent_ = tcph->ts();
			recent_age_ = now();
		}
	}

	return;
}

//
// this shouldn't ever happen
//
void
FullTcpAgent::process_sack(hdr_tcp*)
{
	fprintf(stderr, "%f: FullTcpAgent(%s) Non-SACK capable FullTcpAgent received a SACK\n",
		now(), name());
	return;
}

int
FullTcpAgent::get_num_bytes_remaining() {
	return curseq_ - int(highest_ack_) - window() * maxseg_;
}

int FullTcpAgent::get_lowest_priority() {
    return 1 << 30;
}

FullTcpAgent::~FullTcpAgent() {
    cancel_timers();
    rq_.clear();
}


/*
 * ****** Tahoe ******
 *
 * for TCP Tahoe, we force a slow-start as the dup ack
 * action.  Also, no window inflation due to multiple dup
 * acks.  The latter is arranged by setting reno_fastrecov_
 * false [which is performed by the Tcl init function for Tahoe in
 * ns-default.tcl].
 */

/*
 * Tahoe
 * Dupack-action: what to do on a DUP ACK.  After the initial check
 * of 'recover' below, this function implements the following truth
 * table:
 *
 *      bugfix  ecn     last-cwnd == ecn        action
 *
 *      0       0       0                       full_tahoe_action
 *      0       0       1                       full_tahoe_action [impossible]
 *      0       1       0                       full_tahoe_action
 *      0       1       1                       1/2 window, return
 *      1       0       0                       nothing
 *      1       0       1                       nothing         [impossible]
 *      1       1       0                       nothing
 *      1       1       1                       1/2 window, return
 */

void
TahoeFullTcpAgent::dupack_action()
{
	int recovered = (highest_ack_ > recover_);

	fastrecov_ = TRUE;
	rtxbytes_ = 0;

	if (recovered || (!bug_fix_ && !ecn_processor().is_enabled()) || highest_ack_ == 0) {
		goto full_tahoe_action;
	}

	if (ecn_processor().is_enabled() && last_cwnd_action_ == CongestionWindowAdjustmentReason::ECN) {
		// slow start on ECN
		last_cwnd_action_ = CongestionWindowAdjustmentReason::DUPACK;
		slowdown(TcpSlowdownFlags::CWND_ONE);
		set_rtx_timer();
		rtt_active_ = FALSE;
		t_seqno_ = highest_ack_;
		return;
	}

        if (bug_fix_) {
                /*
                 * The line below, for "bug_fix_" true, avoids
                 * problems with multiple fast retransmits in one
                 * window of data.
                 */
                return;
        }

full_tahoe_action:
	// slow-start and reset ssthresh
	recover_ = maxseq_;
	last_cwnd_action_ = CongestionWindowAdjustmentReason::DUPACK;
    slowdown(TcpSlowdownFlags::SSTHRESH_HALF | TcpSlowdownFlags::CWND_ONE);
	set_rtx_timer();
        rtt_active_ = FALSE;
	t_seqno_ = highest_ack_;
	send_much(0, TcpXmissionReason::NORMAL, 0);
}

/*
 * ****** Newreno ******
 *
 * for NewReno, a partial ACK does not exit fast recovery,
 * and does not reset the dup ACK counter (which might trigger fast
 * retransmits we don't want).  In addition, the number of packets
 * sent in response to an ACK is limited to recov_maxburst_ during
 * recovery periods.
 */

NewRenoFullTcpAgent::NewRenoFullTcpAgent() : save_maxburst_(-1)
{
	bind("recov_maxburst_", &recov_maxburst_);
}

void
NewRenoFullTcpAgent::pack_action(Packet*)
{
	(void)fast_retransmit(highest_ack_);
	cwnd_ = double(ssthresh_);
	if (save_maxburst_ < 0) {
		save_maxburst_ = maxburst_;
		maxburst_ = recov_maxburst_;
	}
}

void
NewRenoFullTcpAgent::ack_action(Packet* p)
{
	if (save_maxburst_ >= 0) {
		maxburst_ = save_maxburst_;
		save_maxburst_ = -1;
	}
	FullTcpAgent::ack_action(p);
}

int SackFullTcpAgent::calc_unbounded_no_deadline_priority(int seq, int maxseq) {
    auto max = 100 * 1460;
    switch (prio_scheme_) {
        case PrioScheme::CAPPED_BYTES_SENT:
            if (seq - startseq_ > max)
                return  max;
            else
                return seq - startseq_;
        case PrioScheme::REMAINING_SIZE:
            if (maxseq - int(highest_ack_) - sq_.total() + 10 < 0)
                return 0;
            else
                return maxseq - int(highest_ack_) - sq_.total() + 10;
        case PrioScheme::LAZY_REMAINING_SIZE_BYTES_SENT:
            if (!signal_on_empty_) {
                return get_lowest_priority() + seq - startseq_;
            } else if (maxseq - int(highest_ack_) - sq_.total() + 10 < 0) {
                return 0;
            } else {
                return maxseq - int(highest_ack_) - sq_.total() + 10;
            }
        case PrioScheme::LAZY_REMAINING_SIZE:
            if (!signal_on_empty_) {
                return get_lowest_priority();
            } else if (maxseq - int(highest_ack_) - sq_.total() + 10 < 0) {
                return 0;
            } else {
                return maxseq - int(highest_ack_) - sq_.total() + 10;
            }
        case PrioScheme::FLOW_SIZE:
            return maxseq - startseq_;
        case PrioScheme::BYTES_SENT:
            return seq - startseq_;
        case PrioScheme ::BATCHED_REMAINING_SIZE:
            if (int(highest_ack_) >= seq_bound_) {
                seq_bound_ = maxseq_;
                if (maxseq - int(highest_ack_) - sq_.total() + 10 < 0)
                    last_prio_ = 0;
                else
                    last_prio_ = maxseq - int(highest_ack_) - sq_.total() + 10;
            }
            return last_prio_;
        case PrioScheme::UNKNOWN:
            abort();
    }
}


 void
SackFullTcpAgent::reset()
{
	sq_.clear();			// no SACK blocks
	/* Fixed typo.  -M. Weigle 6/17/02 */
	sack_min_ = h_seqno_ = -1;	// no left edge of SACK blocks
	FullTcpAgent::reset();
}


int
SackFullTcpAgent::hdrsize(int nsackblocks)
{
    int total = FullTcpAgent::headersize();
    // use base header size plus SACK option size
        if (nsackblocks > 0) {
                total += ((nsackblocks * sack_block_size_)
                        + sack_option_size_);
    }
    return (total);
}

void
SackFullTcpAgent::dupack_action()
{
    int recovered = (highest_ack_ > recover_);

	fastrecov_ = TRUE;
	rtxbytes_ = 0;
	pipe_ = maxseq_ - highest_ack_ - sq_.total();

	if (recovered || (!bug_fix_ && !ecn_processor().is_enabled())) {
		goto full_sack_action;
	}

	if (ecn_processor().is_enabled() && last_cwnd_action_ == CongestionWindowAdjustmentReason::ECN) {
		last_cwnd_action_ = CongestionWindowAdjustmentReason::DUPACK;
		if (ecn_processor().should_slowdown_on_dup_ack())
			slowdown(TcpSlowdownFlags::SSTHRESH_HALF | TcpSlowdownFlags::CWND_HALF);
		cancel_rtx_timer();
		rtt_active_ = FALSE;
		int amt = fast_retransmit(highest_ack_);
		pipectrl_ = TRUE;
		h_seqno_ = highest_ack_ + amt;
		send_much(0, TcpXmissionReason::DUPACK, maxburst_);
		return;
	}

	if (bug_fix_) {
		return;
	}

full_sack_action:
    slowdown(TcpSlowdownFlags::SSTHRESH_HALF | TcpSlowdownFlags::CWND_HALF);
    cancel_rtx_timer();
    rtt_active_ = FALSE;

	// these initiate SACK-style "pipe" recovery
	pipectrl_ = TRUE;
	recover_ = maxseq_;	// where I am when recovery starts

	int amt = fast_retransmit(highest_ack_);
	h_seqno_ = highest_ack_ + amt;

	send_much(0, TcpXmissionReason::DUPACK, maxburst_);
}

void
SackFullTcpAgent::pack_action(Packet*)
{
	if (!sq_.empty() && sack_min_ < highest_ack_) {
		sack_min_ = highest_ack_;
		sq_.cleartonxt();
	}
	pipe_ -= maxseg_;	// see comment in tcp-sack1.cc
	if (h_seqno_ < highest_ack_)
		h_seqno_ = highest_ack_;
}

void
SackFullTcpAgent::ack_action(Packet*)
{
//printf("%f: EXITING fast recovery, recover:%d\n",
//now(), recover_);

	//Shuang: not set pipectrol_ = false
	fastrecov_ = pipectrl_ = FALSE;
	fastrecov_ = FALSE;
        if (!sq_.empty() && sack_min_ < highest_ack_) {
                sack_min_ = highest_ack_;
                sq_.cleartonxt();
        }
	dupacks_ = 0;

	/*
	 * Update h_seqno_ on new ACK (same as for partial ACKS)
	 * -M. Weigle 6/3/05
	 */
	if (h_seqno_ < highest_ack_)
		h_seqno_ = highest_ack_;
}

//
// receiver side: if there are things in the reassembly queue,
// build the appropriate SACK blocks to carry in the SACK
//
int
SackFullTcpAgent::build_options(hdr_tcp* tcph)
{
	int total = FullTcpAgent::build_options(tcph);

        if (!rq_.empty()) {
                int nblk = rq_.gensack(&tcph->sa_left(0), max_sack_blocks_);
                tcph->sa_length() = nblk;
		total += (nblk * sack_block_size_) + sack_option_size_;
        } else {
                tcph->sa_length() = 0;
        }
	//Shuang: reduce ack size
	//return 0;
	return (total);
}

void
SackFullTcpAgent::timeout_action()
{
	FullTcpAgent::timeout_action();

	/*recover_ = maxseq_;

	int progress = curseq_ - int(highest_ack_) - sq_.total();
	cwnd_ = min((last_timeout_progress_ - progress) / 1460 + 1, maxcwnd_);
	ssthresh_ = cwnd_;
	printf("%d %d", progress/1460, last_timeout_progress_ / 1460);
	last_timeout_progress_ = progress;

	reset_rtx_timer(1);
	t_seqno_ = (highest_ack_ < 0) ? iss_ : int(highest_ack_);
	ecnhat_recalc_seq = t_seqno_;
	ecnhat_maxseq = ecnhat_recalc_seq;

	printf("%f, fid %d took timeout, cwnd_ = %f\n", now(), fid_, (double)cwnd_);
	fastrecov_ = FALSE;
	dupacks_ = 0;*/

	//
	// original SACK spec says the sender is
	// supposed to clear out its knowledge of what
	// the receiver has in the case of a timeout
	// (on the chance the receiver has renig'd).
	// Here, this happens when clear_on_timeout_ is
	// enabled.
	//

	if (clear_on_timeout_ ) {
		sq_.clear();
		sack_min_ = highest_ack_;
	}

	return;
}

void
SackFullTcpAgent::process_sack(hdr_tcp* tcph)
{
	//
	// Figure out how many sack blocks are
	// in the pkt.  Insert each block range
	// into the scoreboard
	//
	last_sqtotal_ = sq_.total();

	if (max_sack_blocks_ <= 0) {
		fprintf(stderr,
		    "%f: FullTcpAgent(%s) warning: received SACK block but I am not SACK enabled\n",
			now(), name());
		return;
	}

	int slen = tcph->sa_length(), i;
	for (i = 0; i < slen; ++i) {
		/* Added check for FIN   -M. Weigle 5/21/02 */
		if (!contains_all(TcpFlags{tcph->flags()}, TcpFlags::FIN) &&
		    tcph->sa_left(i) >= tcph->sa_right(i)) {
			fprintf(stderr,
			    "%f: FullTcpAgent(%s) warning: received illegal SACK block [%d,%d]\n",
				now(), name(), tcph->sa_left(i), tcph->sa_right(i));
			continue;
		}
		sq_.add(tcph->sa_left(i), tcph->sa_right(i), 0);
	}

	cur_sqtotal_ = sq_.total();
	return;
}

int
SackFullTcpAgent::send_allowed(int seq)
{
	//Shuang: always pipe control and simple pipe function
	//pipectrl_ = true;
	//pipe_ = maxseq_ - highest_ack_ - sq_.total();

	// not in pipe control, so use regular control
	if (!pipectrl_)
		return (FullTcpAgent::send_allowed(seq));

	// don't overshoot receiver's advertised window
	int topawin = highest_ack_ + int(wnd_) * maxseg_;
//	printf("%f: PIPECTRL: SEND(%d) AWIN:%d, pipe:%d, cwnd:%d highest_ack:%d sqtotal:%d\n",
	//now(), seq, topawin, pipe_, int(cwnd_), int(highest_ack_), sq_.total());

	if (seq >= topawin) {
		return FALSE;
	}

	/*
	 * If not in ESTABLISHED, don't send anything we don't have
	 *   -M. Weigle 7/18/02
	 */
	if (state_ != TcpState::ESTABLISHED && seq > curseq_)
		return FALSE;

	// don't overshoot cwnd_
	int cwin = int(cwnd_) * maxseg_;
	return (pipe_ < cwin);
}


//
// Calculate the next seq# to send by send_much.  If we are recovering and
// we have learned about data cached at the receiver via a SACK,
// we may want something other than new data (t_seqno)
//

int
SackFullTcpAgent::nxt_tseq()
{

	int in_recovery = (highest_ack_ < recover_);
	int seq = h_seqno_;

	if (!in_recovery) {
		return (t_seqno_);
	}

	int fcnt;	// following count-- the
			// count field in the block
			// after the seq# we are about
			// to send
	int fbytes;	// fcnt in bytes

	while ((seq = sq_.nexthole(seq, fcnt, fbytes)) > 0) {
		// if we have a following block
		// with a large enough count
		// we should use the seq# we get
		// from nexthole()
		if (sack_rtx_threshmode_ == 0 ||
		    (sack_rtx_threshmode_ == 1 && fcnt >= sack_rtx_cthresh_) ||
		    (sack_rtx_threshmode_ == 2 && fbytes >= sack_rtx_bthresh_) ||
		    (sack_rtx_threshmode_ == 3 && (fcnt >= sack_rtx_cthresh_ || fbytes >= sack_rtx_bthresh_)) ||
		    (sack_rtx_threshmode_ == 4 && (fcnt >= sack_rtx_cthresh_ && fbytes >= sack_rtx_bthresh_))) {

			// adjust h_seqno, as we may have
			// been "jumped ahead" by learning
			// about a filled hole
			if (seq > h_seqno_)
				h_seqno_ = seq;
			return (seq);
		} else if (fcnt <= 0)
			break;
		else {
		//Shuang; probe
			if (prob_cap_ != 0) {
				seq ++;
			} else
			seq += maxseg_;
		}
	}

	return (t_seqno_);
}

int
SackFullTcpAgent::get_num_bytes_remaining() {
	return curseq_ - int(highest_ack_) - sq_.total() - window() * maxseg_;
}
void
MinTcpAgent::timeout_action() {
//Shuang: prob count when cwnd=1
	if (prob_cap_ != 0) {
		prob_count_ ++;
		if (prob_count_ == prob_cap_) {
			prob_mode_ = true;
		}
		//Shuang: h_seqno_?
		h_seqno_ = highest_ack_;
	}


	SackFullTcpAgent::timeout_action();
}

double
MinTcpAgent::rtt_timeout() {
	return minrto_;
}

void
DDTcpAgent::slowdown(TcpSlowdownFlags how) {

    double decrease;  /* added for highspeed - sylvia */
    double win, halfwin, decreasewin;
    int slowstart = 0;
    ++ncwndcuts_;
    if (!(how & TcpSlowdownFlags::TCP_IDLE) && !(how & TcpSlowdownFlags::NO_OUTSTANDING_DATA)) {
        ++ncwndcuts1_;
    }

    //Shuang: deadline-aware
    double penalty = ecn_processor().get_dctcp_alpha();
    if (deadline != 0) {
        double tleft = deadline / 1e6 - (now() - start_time);

        //if (tleft < 0 && now() < 3) {
        //	cwnd_ = 1;
        //	printf("early termination now %.8lf start %.8lf deadline %d\n", now(), start_time, deadline);
        //	fflush(stdout);
        //	if (signal_on_empty_);
        //		bufferempty();
        //		return;
        //} else
        if (tleft < 0) {
            tleft = 1e10;
        }
        double rtt = int(t_srtt_ >> T_SRTT_BITS) * tcp_tick_;
        double Tc = get_num_bytes_remaining() / (0.75 * cwnd_ * maxseg_) * rtt;
        double d = Tc / tleft;
        if (d > 2) d = 2;
        if (d < 0.5) d = 0.5;
        if (d >= 0)
            penalty = pow(penalty, d);
    } else if (penalty > 0) {
        //non-deadline->TCP
        penalty = 1;
    }

    // we are in slowstart for sure if cwnd < ssthresh
    if (cwnd_ < ssthresh_)
        slowstart = 1;

    if (precision_reduce_) {
        halfwin = windowd() / 2;
        if (wnd_option_ == 6) {
            /* binomial controls */
            decreasewin = windowd() - (1.0 - decrease_num_) * pow(windowd(), l_parameter_);
        } else if (wnd_option_ == 8 && (cwnd_ > low_window_)) {
            /* experimental highspeed TCP */
            decrease = decrease_param();
            //if (decrease < 0.1)
            //	decrease = 0.1;
            decrease_num_ = decrease;
            decreasewin = windowd() - (decrease * windowd());
        } else {
            decreasewin = decrease_num_ * windowd();
        }
        win = windowd();
    } else {
        int temp;
        temp = (int) (window() / 2);
        halfwin = (double) temp;
        if (wnd_option_ == 6) {
            /* binomial controls */
            temp = (int) (window() - (1.0 - decrease_num_) * pow(window(), l_parameter_));
        } else if ((wnd_option_ == 8) && (cwnd_ > low_window_)) {
            /* experimental highspeed TCP */
            decrease = decrease_param();
            //if (decrease < 0.1)
            //       decrease = 0.1;
            decrease_num_ = decrease;
            temp = (int) (windowd() - (decrease * windowd()));
        } else {
            temp = (int) (decrease_num_ * window());
        }
        decreasewin = (double) temp;
        win = (double) window();
    }

    if (how & TcpSlowdownFlags::SSTHRESH_HALF) {
        // For the first decrease, decrease by half
        // even for non-standard values of decrease_num_.
        if (first_decrease_ == 1 || slowstart ||
            last_cwnd_action_ == CongestionWindowAdjustmentReason::TIMEOUT) {
            // Do we really want halfwin instead of decreasewin
            // after a timeout?
            ssthresh_ = (int) halfwin;
        } else {
            ssthresh_ = (int) decreasewin;
        }
    } else if (how & TcpSlowdownFlags::SSTHRESH_ECNHAT) {
        ssthresh_ = (int) ((1 - penalty / 2.0) * windowd());
    } else if (how & TcpSlowdownFlags::SSTHRESH_THREE_QUATER) {
        if (ssthresh_ < 3 * cwnd_ / 4)
            ssthresh_ = (int) (3 * cwnd_ / 4);
    }

	if (how & TcpSlowdownFlags::CWND_HALF) {
        if (first_decrease_ == 1 || slowstart || decrease_num_ == 0.5) {
            cwnd_ = halfwin;
        } else {
            cwnd_ = decreasewin;
        }
    } else if (how & TcpSlowdownFlags::CWND_ECNHAT) {
		cwnd_ = (1 - penalty/2.0) * windowd();
		if (cwnd_ < 1)
			cwnd_ = 1;
    } else if (how & TcpSlowdownFlags::CWND_HALF_WITH_MIN) {
        // We have not thought about how non-standard TCPs, with
        // non-standard values of decrease_num_, should respond
        // after quiescent periods.
        cwnd_ = decreasewin;
        if (cwnd_ < 1)
            cwnd_ = 1;
    } else if (how & TcpSlowdownFlags::CWND_RESTART) {
        cwnd_ = int(wnd_restart_);
    } else if (how & TcpSlowdownFlags::CWND_INIT) {
        cwnd_ = int(wnd_init_);
    } else if (how & TcpSlowdownFlags::CWND_ONE) {
        cwnd_ = 1;
    } else if (how & TcpSlowdownFlags::CWND_HALF_WAY) {
		cwnd_ = W_used + decrease_num_ * (win - W_used);
        if (cwnd_ < 1)
                cwnd_ = 1;
	}

	if (ssthresh_ < 2)
		ssthresh_ = 2;
	if (cwnd_ < 1)
		cwnd_ = 1; // Added by Mohammad
	if (how & (
            TcpSlowdownFlags::CWND_HALF | TcpSlowdownFlags::CWND_RESTART | TcpSlowdownFlags::CWND_INIT |
            TcpSlowdownFlags::CWND_ONE | TcpSlowdownFlags::CWND_ECNHAT))
		ecn_processor().notify_sender_responded_to_congestion();

	fcnt_ = count_ = 0;
	if (first_decrease_ == 1)
		first_decrease_ = 0;
	// for event tracing slow start
	if (cwnd_ == 1 || slowstart)
		// Not sure if this is best way to capture slow_start
		// This is probably tracing a superset of slowdowns of
		// which all may not be slow_start's --Padma, 07/'01.
		trace_event("SLOW_START");
}

int
DDTcpAgent::get_num_bytes_remaining() {
    // TODO: use for priority
	return curseq_ - int(highest_ack_) - sq_.total();
}

int
DDTcpAgent::foutput(int seqno, TcpXmissionReason reason) {
    if (deadline != 0) {
        double tleft_s = deadline/1.e6 - (now() - start_time) 
            - (curseq_ - int(maxseq_)) * 8.0 / (link_rate_ * 1.e9);
        if (tleft_s < 0) {
            deadline = 0;
            // TODO
            //early_terminated_ = 1;
            //bufferempty();
            //printf("early termination V2 now %.8lf start %.8lf deadline %d get_num_bytes_remaining %d tleft %.8f\n", now(), start_time, deadline, curseq_ - int(maxseq_), tleft);
            //fflush(stdout);
            //return 0;
        }
    }
    return SackFullTcpAgent::foutput(seqno, reason);
}

int
DDTcpAgent::need_send() {
	if (deadline != 0) { 
 		auto tleft = deadline / 1e6 - (now() - start_time);
		if (tleft < 0)
			return 0;// TODO: this doesn't hang only due to foutput setting deadline to zero
	}
	return SackFullTcpAgent::need_send();
}

DDTcpAgent::DDTcpAgent() : SackFullTcpAgent{} {

}

int DDTcpAgent::calc_unbounded_no_deadline_priority(int seq, int maxseq) {
   if (prio_scheme_ != PrioScheme::UNKNOWN) {
       return get_lowest_priority();
   } else {
       return SackFullTcpAgent::calc_unbounded_no_deadline_priority(seq, maxseq);
   }
}

FullTcpAgent::EcnProcessor::EcnProcessor(FullTcpAgent * agent) 
    : TcpAgent::EcnProcessor(agent)
    , informpacer_{0}
    , normal_recv_{std::make_unique<NormalReceiverCETracker>()}
    , ecnhat_recv_{std::make_unique<EcnhatReceiverCETracker>()}
    , afabric_recv_{std::make_unique<AFabricReceiverCETracker>()}
    , afabric_send_{std::make_unique<AfabricEcnhatSenderCETracker>(this)}
{
    if constexpr(std::is_base_of_v<TracedVar, FullTcpAgent::TracedInt>) {
        this->agent().bind("afabric_ecn_enabled_", &afabric_ecn_enabled_);
    }
}

void FullTcpAgent::EcnProcessor::set_ce_if_needed(TcpFlags& pflags) const {
    if (is_enabled() && is_active() && get_recv_tracker().need_ce_echo()) {
        // This is needed here for the ACK in a SYN, SYN/ACK, ACK
        // sequence.
        pflags |= TcpFlags::ECE;
    }
}

void FullTcpAgent::EcnProcessor::modify_pflags_on_foutput(TcpFlags& pflags, bool is_retransmit) {
    if (!is_enabled()) {
        return;
    }

    if (is_some_syn(pflags)) {
        if (is_syn(pflags)) {
            pflags |= TcpFlags::ECE;
            pflags |= TcpFlags::CWR;
        } else if (is_syn_ack(pflags)) {
            pflags |= TcpFlags::ECE;
            pflags &= ~TcpFlags::CWR;
        }
    } else {
        if (is_active() && need_to_attach_cong_action(is_retransmit)) {
            /* set CWR if necessary */
            pflags |= TcpFlags::CWR;
            /* Turn cong_action_ off: Added 6/5/08, Sally Floyd. */
            cong_action_ = false;
        }
    }

    set_ce_if_needed(pflags);
}

auto FullTcpAgent::EcnProcessor::is_syn(TcpFlags pflags) -> bool {
    return is_some_syn(pflags) && !is_syn_ack(pflags);
}

auto FullTcpAgent::EcnProcessor::is_some_syn(TcpFlags pflags) -> bool {
    return contains_all(pflags, TcpFlags::SYN);
}

auto FullTcpAgent::EcnProcessor::is_syn_ack(TcpFlags pflags) -> bool {
    return contains_all(pflags, TcpFlags::SYN | TcpFlags::ACK);
}

void FullTcpAgent::EcnProcessor::on_sendpacket(TcpFlags& pflags, Packet * pkt, bool has_data) {
    /*
     * Explicit Congestion Notification (ECN) related:
     * Bits in header:
     * 	ECT (EC Capable Transport),
     * 	ECNECHO (ECHO of ECN Notification generated at router),
     * 	CWR (Congestion Window Reduced from RFC 2481)
     * States in TCP:
     *	ecn_: I am supposed to do ECN if my peer does
     *	ect_: I am doing ECN (ecn_ should be T and peer does ECN)
     */

    hdr_flags::access(pkt)->ect() = 
        get_next_packet_ect(has_data, is_syn_ack(pflags));

    set_ce_if_needed(pflags);

    copy_to_hdr_flags(pflags, hdr_flags::access(pkt));

    inform_pacer_if_needed(hdr_ip::access(pkt), has_data);
}

auto FullTcpAgent::EcnProcessor::get_next_packet_ect(
        bool has_data, bool is_syn_ack) const -> bool {
    if (get_send_tracker().always_report_ect()) {
        return ect_;
    }

    if (!is_enabled()) {
        /* Set ect() to 0.  -M. Weigle 1/19/05 */
        return false;
    }

    if (has_data || (ecn_syn_ && ecn_syn_next_ && is_syn_ack)) {
        // set ect on data packets
        // set ect on syn/ack packet, if syn packet was negotiating ECT
        return ect_;
    }
    return false;
}

void FullTcpAgent::EcnProcessor::copy_to_hdr_flags(TcpFlags pflags, hdr_flags * fh) const {
    // TODO: there seem to be two reasons for setting echo: handshake-specific
    // protocol and really the congetion; split the two

    // fill in CWR and ECE bits which don't actually sit in
    // the tcp_flags but in hdr_flags
    if (contains_all(pflags, TcpFlags::ECE)) {
        get_recv_tracker().ce_set_packet_echo(fh);
    } else {
        fh->ecnecho() = 0;
    }

    if (contains_all(pflags, TcpFlags::CWR)) {
        fh->cong_action() = 1;
    } else {
        /* Set cong_action() to 0  -M. Weigle 1/19/05 */
        fh->cong_action() = 0;
    }
}


void FullTcpAgent::EcnProcessor::on_receive(Packet * pkt) {
    get_send_tracker().on_receive(pkt);

    /* Mohammad: check if we need to inform
     * pacer of ecnecho.
     */
    auto const tiflags = TcpFlags{hdr_tcp::access(pkt)->flags()};
    if (!is_some_syn(tiflags) && is_ecn_active(pkt))
        informpacer_ = 1;
}


void FullTcpAgent::EcnProcessor::update_state_new_packet(Packet * packet, bool has_data) {
    if (is_enabled()) {
        get_recv_tracker().on_new_packet(packet, has_data);
    }
}

void FullTcpAgent::EcnProcessor::on_receive_pure_incoming() {
    /* Mohammad: For DCTCP state machine */
    if (get_recv_tracker().has_pending() && agent_has_pending_acks()) {
        // Must send an immediate ACK with with previous ECN state
        // before transitioning to new state
        send_immediate_ack_with_previous_ce();
    }
}

auto FullTcpAgent::EcnProcessor::agent_has_pending_acks() const -> bool {
    return (agent().rcv_nxt_ - agent().last_ack_sent_) > 0;
}

void FullTcpAgent::EcnProcessor::send_immediate_ack_with_previous_ce() {
    agent().flags_ |= TF_ACKNOW;
    get_recv_tracker().flip_ce();
    agent().send_much(1, TcpXmissionReason::NORMAL, agent().maxburst_);
    get_recv_tracker().flip_ce();
}

auto FullTcpAgent::EcnProcessor::on_receive_syn_ack_pre(hdr_flags * fh) -> EcnDecision {
    if (!is_enabled()) {
        return EcnDecision::NO_CONGESTION;
    }
    /* looks like an ok SYN or SYN+ACK */
    // If ecn_syn_wait is set to 2:
    //  Check if CE-marked SYN/ACK packet, then just send an ACK
    //  packet with ECE set, and drop the SYN/ACK packet.
    //  Don't update TCP state.
    if (fh->ecnecho() && !fh->cong_action() && ecn_syn_wait_ == EcnSynAckAction::RETRY) {
        // if SYN/ACK packet and ecn_syn_wait_ == 2
        if (fh->ce()) {
            // If SYN/ACK packet is CE-marked
            agent().set_rtx_timer();
            agent().sendpacket(agent().t_seqno_, agent().rcv_nxt_, TcpFlags::ACK | TcpFlags::ECE, 0, TcpXmissionReason::NORMAL);
            return EcnDecision::CONGESTION_DROP;
        }
    }
    return EcnDecision::NO_CONGESTION;
}

void FullTcpAgent::EcnProcessor::on_receive_ack_pre(Packet * packet, bool has_data) {
    if (hdr_flags::access(packet)->ecnecho() && (!is_enabled() || !is_active())) {
        report_unexpected_ecnecho();
    }

    update_state_new_packet(packet, has_data);
}

void FullTcpAgent::EcnProcessor::report_unexpected_ecnecho() const {
    fprintf(stderr,
        "%f: FullTcp(%s): warning, recvd ecnecho but I am not ECN capable! %d %d\n",
        Scheduler::instance().clock(), agent_->name(), is_enabled(), ect_);
}

auto FullTcpAgent::EcnProcessor::on_receive_syn_ack_ack(hdr_flags * fh) -> EcnDecision {
    if (!is_enabled()) {
        return EcnDecision::NO_CONGESTION;
    }
    // TODO: check that priority is set by the sender in this case
    // and change to is_ecn_active()
    if (ect_ && ecn_syn_ && fh->ecnecho()) {
        return handle_syn_ack_ack_ecn();
    } else {
        return EcnDecision::NO_CONGESTION;
    }
}

auto FullTcpAgent::EcnProcessor::handle_syn_ack_ack_ecn() -> EcnDecision {
    switch (ecn_syn_wait_) {
    case EcnSynAckAction::RETRY:
        // The SYN/ACK packet was ECN-marked.
        // Reset the rtx timer, send another SYN/ACK packet
        //  immediately, and drop the ACK packet.
        // Do not move to TCPS_ESTB state or update TCP variables.
        retry_syn_ack();
        return EcnDecision::CONGESTION_DROP;
    case EcnSynAckAction::WAIT:
        // A timer will be called in ecn().
        agent().cwnd_ = 1;
        use_rtt_ = true; //KK, wait for timeout() period
        return EcnDecision::CONGESTION;
    case EcnSynAckAction::WINDOW_OF_ONE:
        // Congestion window will be halved in ecn().
        agent().cwnd_ = 2;
        return EcnDecision::CONGESTION;
    }
}

void FullTcpAgent::EcnProcessor::retry_syn_ack() {
    agent().cancel_rtx_timer();
    ecn_syn_next_ = false;
    agent().foutput(agent().iss_, TcpXmissionReason::NORMAL);
    agent().wnd_init_option_ = 1;
    agent().wnd_init_ = 1;
}

void FullTcpAgent::EcnProcessor::on_receive_newack(Packet * pkt) {
	// Mohammad
	if (ect_) {
		update_ecn_burst(pkt);
	}
}

void FullTcpAgent::EcnProcessor::reset() {
	if (ecn_syn_)
		ecn_syn_next_ = true;
	else
		ecn_syn_next_ = false;
}

auto FullTcpAgent::EcnProcessor::can_open_cwnd(Packet * pkt) const -> bool {
	return super::can_open_cwnd(pkt) || (ecn_burst() && !old_ecn_);
}

void FullTcpAgent::EcnProcessor::on_receive_syn(hdr_flags * fh) {
    if (!is_enabled()) {
        return;
    }
    if (fh->ecnecho() && fh->cong_action()) {
        ect_ = true;
    }
}

void FullTcpAgent::EcnProcessor::on_receive_syn_ack(hdr_flags * fh) {
    if (!is_enabled()) {
        return;
    }

    get_recv_tracker().on_syn_ack(fh);

    if (fh->ecnecho() && !fh->cong_action()) {
        ect_ = true;
    }
}

void FullTcpAgent::EcnProcessor::on_receive_ack(Packet * pkt, TcpFlags tiflags) {
    /*
     * if this is an ACK with an ECN indication, handle this
     * but not if it is a syn packet
     */
    if (is_ecn_active(pkt) && !is_some_syn(tiflags)) {
        // Note from Sally: In one-way TCP,
        // ecn() is called before newack()...
        ecn(agent().highest_ack_);  // updated by newack(), before on_receive_ack
        // "set_rtx_timer();" from T. Kelly.
        if (agent().cwnd_ < 1) {
            agent().set_rtx_timer();
        }
    }
}

void FullTcpAgent::EcnProcessor::inform_pacer_if_needed(hdr_ip *iph, bool has_data) {
    if (!has_data) {
        return;
    }

    if (informpacer_)
        iph->gotecnecho = 1;
    else
        iph->gotecnecho = 0;

    informpacer_ = 0;
}

auto FullTcpAgent::EcnProcessor::get_recv_tracker() -> ReceiverCETracker& {
    return const_cast<ReceiverCETracker&>(
            const_cast<EcnProcessor const *>(this)->get_recv_tracker());
}

auto FullTcpAgent::EcnProcessor::get_recv_tracker() const -> ReceiverCETracker const& {
    if (ecnhat_) {
        if (afabric_ecn_enabled_) {
            return *afabric_recv_;
        } else {
            return *ecnhat_recv_;
        }
    } else {
        return *normal_recv_;
    }
}

auto FullTcpAgent::EcnProcessor::predict_ok(Packet *pkt) const -> bool {
  return !is_enabled() || !is_ecn_active(pkt);
}

void FullTcpAgent::EcnProcessor::on_receive_predict_ok(Packet * packet, bool has_data) {
    update_state_new_packet(packet, has_data);
}

auto FullTcpAgent::ecn_processor() -> EcnProcessor& {
    return static_cast<EcnProcessor&>(super::ecn_processor());
}

auto FullTcpAgent::EcnProcessor::agent() -> FullTcpAgent& {
    return const_cast<FullTcpAgent &>(const_cast<EcnProcessor const *>(this)->agent());
}

auto FullTcpAgent::EcnProcessor::agent() const -> FullTcpAgent const& {
    return static_cast<FullTcpAgent const&>(super::agent());
}

auto FullTcpAgent::EcnProcessor::is_active() const -> bool {
    return ect_;
}

void FullTcpAgent::EcnProcessor::on_foutput(int highest) {
    get_send_tracker().on_foutput(highest);
}

void FullTcpAgent::EcnProcessor::delay_bind_init_all() {
    afabric_ecn_enabled_ = false;
    if constexpr (!std::is_base_of_v<TracedVar, FullTcpAgent::TracedInt>) {
        agent().delay_bind_init_one("afabric_ecn_enabled_");
    }
    super::delay_bind_init_all();
}

auto FullTcpAgent::EcnProcessor::delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer) -> bool {
    if (agent().delay_bind_bool(
            varName, localName, "afabric_ecn_enabled_", &afabric_ecn_enabled_, tracer))
        return true;
    return super::delay_bind_dispatch(varName, localName, tracer);

}

auto FullTcpAgent::EcnProcessor::get_send_tracker() -> SenderCETracker& {
    return const_cast<SenderCETracker&>(
            const_cast<EcnProcessor const *>(this)->get_send_tracker());
}

auto FullTcpAgent::EcnProcessor::get_send_tracker() const -> SenderCETracker const& {
    if (afabric_ecn_enabled_) {
        return *afabric_send_;
    } else {
        return super::get_send_tracker();
    }
}
