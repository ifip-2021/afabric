#include <ios>
#include <string>
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <limits>

namespace {

const constexpr auto PACKET_SIZE = 1460;
const constexpr auto HEADER_SIZE = 40;
const constexpr auto BITS_IN_BYTE = 8;

struct simulation_params {
    double alpha;
    double rate;
    double link_delay;
    double host_delay;
    int max_num_chunks;
};

struct Chunk { 
    double time;
    int size_in_bytes; 

    auto operator<(Chunk const& other) {
        return std::make_tuple(time, size_in_bytes) 
            < std::make_tuple(other.time, other.size_in_bytes);
    };
};

auto generate_next_random_double(double min, double max, std::minstd_rand0& rng) {
    auto const x = std::generate_canonical<
        double, std::numeric_limits<double>::digits>(rng);
    return min + x * (max - min);
}

auto get_packet_time(simulation_params const& params) {
    return (PACKET_SIZE + HEADER_SIZE) * BITS_IN_BYTE / params.rate;
}

auto generate_chunks(simulation_params const& params, 
                     int num_bytes, std::minstd_rand0& rng) {
    auto const num_packets = num_bytes / PACKET_SIZE;
    auto const num_chunks = std::min(num_packets, params.max_num_chunks);
    auto const chunk_size_pkts = num_packets / num_chunks;
    auto const num_bigger_chunks = num_packets % num_chunks;

    auto const start_time = 0.0;
    auto const end_time = start_time + params.alpha * get_packet_time(params) * num_packets;
    auto chunks = std::vector<Chunk>(num_chunks);
    for (auto i = 0; i < num_chunks; i++) {
        chunks[i] = Chunk { 
            .time = generate_next_random_double(start_time, end_time, rng),
            .size_in_bytes = PACKET_SIZE * 
                (i < num_bigger_chunks ? chunk_size_pkts + 1 : chunk_size_pkts)
        };
    }
    std::sort(begin(chunks), end(chunks));
    return chunks;
}

auto get_oracle_fct(simulation_params const& params, int src, int dst, 
                    std::vector<Chunk> const& chunks) {
    auto end_time = 0.0; 
    for (auto &[start_time, num_bytes] : chunks) {
        end_time = std::max(start_time, end_time) + 
            num_bytes / PACKET_SIZE * get_packet_time(params);
    }
    auto const num_hops = src / 16 == dst / 16 ? 2 : 4;
    auto const propagation_delay = 2 * (num_hops * params.link_delay + 2 * params.host_delay);
    return end_time + propagation_delay;
}

}

int main(int argc, char const * argv[]) {
    std::ios_base::sync_with_stdio(false);
    auto const params = simulation_params {
        .alpha = std::stod(argv[1]),
        .rate = std::stod(argv[2]),
        .link_delay = std::stod(argv[3]),
        .host_delay = std::stod(argv[4]),
        .max_num_chunks = std::stoi(argv[5]),
    };

    auto rngs = std::map<std::tuple<int, int, int>, std::minstd_rand0>{};

    auto cnt = 0;
    while (true) {
        int src, dst, pid, num_to, early;
        double num_pkts, fct, deadline;
        std::cin >> num_pkts >> fct >> num_to >> src >> dst >> pid >> deadline >> early;
        if (!std::cin) {
            break;
        }
        auto const size_in_bytes = PACKET_SIZE * int(num_pkts);
        auto const key = std::make_tuple(src, dst, pid);

        if (rngs.count(key) == 0) {
            auto const seed = 17 * src + 1244 * dst + pid;
            rngs[key] = std::minstd_rand0(seed);
        }

        auto const chunks = generate_chunks(params, size_in_bytes, rngs[key]);
        auto const oracle_fct = get_oracle_fct(params, src, dst, chunks);
        std::cout << oracle_fct << "\n";
    }
}
