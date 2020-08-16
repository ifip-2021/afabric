#ifndef ns_app_chunks_h
#define ns_app_chunks_h

#include "app.h"
#include "timer-handler.h"
#include <tools/BindCachingMixin.h>
#include <random>
#include <optional>

class ChunksApplication : public BindCachingMixin<Application> {
    using super = BindCachingMixin;
    using RNG = std::minstd_rand0;
public:
    ChunksApplication();

    void send(int nbytes) override;

    int command(int argc, const char * const * argv) override;

    void delay_bind_init_all() override;

    int delay_bind_dispatch(
            const char *varName, const char *localName, TclObject *tracer
        ) override;

private:
    static auto const BITS_IN_BYTE = 8;

private:
    struct Chunk;
    class NextChunkTimer;

private:
    auto get_next_chunk() const -> Chunk const&;
    auto check_if_using_alpha() -> bool;
    auto generate_chunks(double start_time, int num_bytes) const
        -> std::vector<Chunk>;

    void init_chunks(int num_bytes);
    void send_next_chunk();

    auto get_random_device() const -> RNG&;
    auto get_next_random_double(double min, double max) const -> double;

private:
    std::unique_ptr<RNG> rng_;
    std::unique_ptr<NextChunkTimer> next_chunk_timer_;
    std::vector<Chunk> cur_chunks_;
    size_t next_chunk_idx_;

    double use_alpha_probability_;
    double alpha_;
    double link_rate_gbps_;
    int max_num_chunks_;
    int packet_size_bytes_;
    int header_size_bytes_;
};

#endif
