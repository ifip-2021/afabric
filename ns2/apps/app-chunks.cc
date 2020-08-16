#include "app-chunks.h"
#include "timer-handler.h"

#include <tools/BindCachingMixin.hpp>

#include <limits>
#include <algorithm>
#include <string>

static class ChunksApplicationClass : public TclClass {
public:
    ChunksApplicationClass() : TclClass("Application/Chunks") {}
    TclObject* create(int, const char * const *) {
        return new ChunksApplication{};
    }
} class_chunks_application;
 
class ChunksApplication::NextChunkTimer : public TimerHandler {
public:
    explicit NextChunkTimer(ChunksApplication * app) : app_{app} {}

private:
    void expire(Event *) override {
        app_->send_next_chunk();
    }

private:
    ChunksApplication * app_;
};

struct ChunksApplication::Chunk { 
    double time;
    int size_in_bytes; 

    auto operator<(Chunk const& other) {
        return make_tuple(time, size_in_bytes) 
            < make_tuple(other.time, other.size_in_bytes);
    };
};


ChunksApplication::ChunksApplication() : 
    next_chunk_timer_{make_unique<NextChunkTimer>(this)} {

}

void ChunksApplication::send(int nbytes) {
    agent_->sendmsg(0, "DAT_NEW");
    if (check_if_using_alpha()) {
        init_chunks(nbytes);
        send_next_chunk();
    } else {
        agent_->sendmsg(nbytes, "DAT_EOF");
    }
}

auto ChunksApplication::check_if_using_alpha() -> bool {
    if (use_alpha_probability_ < 0) {
        return true;
    }
    auto const x = std::generate_canonical<
        double, std::numeric_limits<double>::digits>(get_random_device());
    return x < use_alpha_probability_;
}

void ChunksApplication::delay_bind_init_all() {
    delay_bind_init_one("alpha_");
    delay_bind_init_one("link_rate_");
    delay_bind_init_one("packet_size_bytes_");
    delay_bind_init_one("header_size_bytes_");
    delay_bind_init_one("max_num_chunks_");
    delay_bind_init_one("use_alpha_probability_");
}

int ChunksApplication::command(int argc, const char * const * argv) {
    if (argc == 3) {
        if (strcmp(argv[1], "set-seed") == 0) {
            rng_ = std::make_unique<RNG>(std::stoul(argv[2]));
            return TCL_OK;
        }
    }

    return super::command(argc, argv);
}

auto ChunksApplication::get_random_device() const -> RNG& {
    return *rng_;
}

auto ChunksApplication::generate_chunks(double start_time, int num_bytes) const
        -> std::vector<Chunk> {
    auto const num_packets = num_bytes / packet_size_bytes_;
    auto const packet_time =  (packet_size_bytes_ + header_size_bytes_)
        * BITS_IN_BYTE / (link_rate_gbps_ * 1e9);

    auto const num_chunks = std::min(num_packets, max_num_chunks_);
    auto const chunk_size_pkts = num_packets / num_chunks;
    auto const num_bigger_chunks = num_packets % num_chunks;

    auto chunks = std::vector<Chunk>(num_chunks);
    auto total_size = 0;
    for (auto i = 0; i < num_chunks; i++) {
        chunks[i] = Chunk { 
            .time = get_next_random_double(
                        start_time, 
                        start_time + alpha_ * packet_time * num_packets
                    ),
            .size_in_bytes = packet_size_bytes_ * 
                (i < num_bigger_chunks ? chunk_size_pkts + 1 : chunk_size_pkts)
        };
        if (chunks[i].size_in_bytes <= 0) {
            throw std::logic_error("Empty chunk!");
        }
        total_size += chunks[i].size_in_bytes;
    }
    if (total_size != num_bytes) {
        throw std::logic_error("Generated chunks have a different number of bytes");
    }

    std::sort(begin(chunks), end(chunks));

    return chunks;
}

void ChunksApplication::init_chunks(int num_bytes) {
    cur_chunks_ = generate_chunks(Scheduler::instance().clock(), num_bytes);
    next_chunk_idx_ = 0;
}

void ChunksApplication::send_next_chunk() {
    agent_->sendmsg(
            get_next_chunk().size_in_bytes,
            next_chunk_idx_ + 1 == cur_chunks_.size() ? 
                "DAT_EOF" : nullptr
            );

    next_chunk_idx_++;

    if (next_chunk_idx_ < cur_chunks_.size()) {
        next_chunk_timer_->resched(
                get_next_chunk().time- Scheduler::instance().clock());

    }
}

auto ChunksApplication::get_next_chunk() const -> Chunk const& {
    return cur_chunks_[next_chunk_idx_];
}

auto ChunksApplication::get_next_random_double(double min, double max) const 
        -> double {
    auto const x = std::generate_canonical<
            double, std::numeric_limits<double>::digits
        >(get_random_device());
    return min + x * (max - min);
}

int ChunksApplication::delay_bind_dispatch(
        const char *varName, const char *localName, TclObject *tracer
        ) {
    if (delay_bind(varName, localName, "alpha_", &alpha_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "link_rate_", &link_rate_gbps_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "packet_size_bytes_", &packet_size_bytes_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "header_size_bytes_", &header_size_bytes_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "max_num_chunks_", &max_num_chunks_, tracer)) return TCL_OK;
    if (delay_bind(varName, localName, "use_alpha_probability_", &use_alpha_probability_, tracer)) return TCL_OK;

    return super::delay_bind_dispatch(varName, localName, tracer);
}
