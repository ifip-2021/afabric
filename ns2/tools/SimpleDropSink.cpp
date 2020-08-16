#include "SimpleDropSink.h"

#include <common/ip.h>

#include <sstream>

namespace {

class SimpleDropSinkClass : public TclClass {
public:
    SimpleDropSinkClass() : TclClass("SimpleDropSink") {}
    auto create(int, const char *const* ) -> TclObject * override {
        return new SimpleDropSink{};
    }
} class_drop_tail;

}

SimpleDropSink::SimpleDropSink()
    : per_fid_counts_{}
{}

auto SimpleDropSink::command(int argc, char const * const * argv) -> int {
    return nfp::command::dispatch(this, argc, argv)
        .add("print-stats", &SimpleDropSink::print_stats)
        .result([this] (auto argc, auto argv) 
                { return super::command(argc, argv); });
}

auto SimpleDropSink::print_stats(std::string_view channel_name) 
        -> nfp::command::TclResult {
    auto& tcl = Tcl::instance();
    int mode;
    auto channel = Tcl_GetChannel(tcl.interp(), channel_name.data(), &mode);

    if (channel == nullptr) {
        tcl.resultf("print-stats: can't write to %s", channel_name.data());
        return nfp::command::TclResult::ERROR;
    }

    std::ostringstream buf{};
    for (auto [fid, cnt] : per_fid_counts_) {
        buf << fid << " " << cnt << "\n";
    }
    auto const str = buf.str();
    Tcl_Write(channel, str.c_str(), str.size());
    return nfp::command::TclResult::OK;
}

void SimpleDropSink::recv(Packet * packet, Handler *) {
    auto const iph = hdr_ip::access(packet);
    per_fid_counts_[iph->flowid()]++;
    Packet::free(packet);
}
