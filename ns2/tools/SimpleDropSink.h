#ifndef ns_simple_drop_sink_h
#define ns_simple_drop_sink_h

#include <tools/CommandDispatchHelper.h>
#include <common/object.h>
#include <unordered_map>

class SimpleDropSink : public NsObject {
    using super = NsObject;
public:
    SimpleDropSink();

    void recv(Packet * packet, Handler *) override;
    auto command(int argc, char const * const * argv) -> int override;

private:
    auto print_stats(std::string_view channel_name) -> nfp::command::TclResult;

private:
    std::unordered_map<int, int> per_fid_counts_;
};

#endif
