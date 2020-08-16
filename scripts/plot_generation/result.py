import argparse
from json import dumps
from typing import List, Optional

_PACKET_SIZE = 1460
_HDR_SIZE = 40
_BITS_IN_BYTE = 8
_CUT_THROUGH = False


class Bound:
    def __init__(self, lower, upper):
        self.lower = lower
        self.upper = upper

    @staticmethod
    def from_string(s: str):
        a, b = s.split(',')
        return Bound(int(a) if a else None, int(b) if b else None)

    def satisfies(self, value):
        if self.lower is not None and value < self.lower:
            return False
        if self.upper is not None and value > self.upper:
            return False
        return True


class FlowSetStats:
    def __init__(self,
                 name: str,
                 bound: Bound,
                 rate: float,
                 link_delay: float,
                 host_delay: float):
        self.rate = rate
        self.link_delay = link_delay
        self.host_delay = host_delay
        self.bound = bound
        self.name = name
        self.deadlines = 0
        self.deadlines_met = 0
        self.nfcts: List[float] = []
        self.real_nfcts: List[float] = []
        self.fcts: List[float] = []
        self.fcts_notimeout: List[float] = []
        self.num_timeouts = 0
        self.gps: List[float] = []
        self.non_deadline_gps: List[float] = []

    def add(self, src_id: int, dst_id: int, ofct: float,
            fct: float, size: int, deadline_is_met: bool, has_deadline: bool,
            num_timeouts: int):
        if not self.bound.satisfies(size):
            return

        self.nfcts.append(fct / size)
        self.real_nfcts.append(fct / ofct)
        self.fcts.append(fct)
        self.gps.append(size * 8 / fct)
        if not has_deadline:
            self.non_deadline_gps.append(size * 8 / fct)
        if num_timeouts == 0:
            self.fcts_notimeout.append(fct)
        if deadline_is_met:
            self.deadlines_met += 1
        if has_deadline:
            self.deadlines += 1
        self.num_timeouts += num_timeouts

    @property
    def avg_gp(self) -> float:
        return sum(self.gps) / len(self.gps) if len(self.gps) > 0 else 0

    @property
    def avg_nondeadline_gp(self) -> float:
        if len(self.non_deadline_gps) == 0:
            return 0
        return sum(self.non_deadline_gps) / len(self.non_deadline_gps)

    @property
    def avg_fct(self) -> float:
        if len(self.fcts) == 0:
            return float('inf')
        return sum(self.fcts) / len(self.fcts)

    @property
    def avg_nfct(self) -> float:
        if len(self.nfcts) == 0:
            return float('inf')
        return sum(self.nfcts) / len(self.nfcts)

    @property
    def avg_real_nfct(self) -> float:
        if len(self.real_nfcts) == 0:
            return float('inf')
        return sum(self.real_nfcts) / len(self.real_nfcts)

    @property
    def real_nfct_99th(self) -> float:
        self.real_nfcts.sort()
        if len(self.real_nfcts) > 0:
            return self.real_nfcts[99 * len(self.real_nfcts) // 100]
        else:
            return 0

    @property
    def fct_99th(self) -> float:
        self.fcts.sort()
        if len(self.fcts) > 0:
            return self.fcts[99 * len(self.fcts) // 100]
        else:
            return 0

    @property
    def median_fct(self) -> float:
        self.fcts.sort()
        if len(self.fcts) > 0:
            return self.fcts[50 * len(self.fcts) // 100]
        else:
            return 0

    @property
    def num_flows(self) -> int:
        return len(self.fcts)

    @property
    def nfct_99th(self) -> float:
        self.nfcts.sort()
        if len(self.nfcts) > 0:
            return self.nfcts[99 * len(self.fcts) // 100]
        else:
            return 0


def _parse_line(line: str, ofct_line: str):
    pkt_size, time, timeouts_num, src, dst, pid, deadline, *rest = line.split()
    oracle_fct, = ofct_line.split()

    early_termination_str = None
    try:
        early_termination_str, = rest
    except ValueError:
        pass

    early_termination = int(early_termination_str) == 1\
        if early_termination_str is not None else False

    return (float(pkt_size), float(time), int(timeouts_num),
            int(src), int(dst), int(pid),
            float(deadline) * 1.e-6, early_termination, float(oracle_fct))


def _has_deadline(deadline, early_termination):
    return deadline != 0.0 or early_termination


def _is_deadline_met(deadline, time, early_termination):
    return deadline != 0.0 and time <= deadline and not early_termination


def report(report_avg, report_tail, report_num_deadlines, report_gp,
           report_avgn, report_avgrealn, report_num_to, json,
           stats: FlowSetStats):
    if json is not None and stats.name not in json:
        json[stats.name] = {}
    json[stats.name]['num'] = stats.num_flows

    if report_avg:
        if json is None:
            print(f"Average FCT for {stats.num_flows} in {stats.name}:\n"
                  f"{stats.avg_fct}")
        else:
            json[stats.name]['avgfct'] = stats.avg_fct

    if report_gp:
        if json is None:
            print(f"Average Goodput for {stats.num_flows} in {stats.name}:\n"
                  f"{stats.avg_gp} (w/o slack: {stats.avg_nondeadline_gp}")
        else:
            json[stats.name]['gp'] = stats.avg_gp
            json[stats.name]['nd_gp'] = stats.avg_nondeadline_gp

    if report_tail:
        if json is None:
            print(f'99th percentile FCT for {stats.num_flows} '
                  f'in {stats.name}:\n{stats.fct_99th}')
        else:
            json[stats.name]['tailfct'] = stats.fct_99th

    if report_num_deadlines:
        if json is None:
            print(f'Num deadlines met for {stats.num_flows} in {stats.name}:\n'
                  f'{stats.deadlines_met}/{stats.deadlines}')
        else:
            json[stats.name]['thd'] = stats.deadlines_met
            json[stats.name]['max_thd'] = stats.deadlines
            json[stats.name]['thd_fraction'] = stats.deadlines_met / \
                stats.deadlines if stats.deadlines != 0 else None

    if report_avgrealn:
        if json is None:
            print(f'Average (real) normalized FCT for {stats.num_flows} in '
                  f'{stats.name}:\n{stats.avg_nfct}')
        else:
            json[stats.name]['avgrealnfct'] = stats.avg_real_nfct
            json[stats.name]['tailavgrealnfct'] = stats.real_nfct_99th

    if report_avgn:
        if json is None:
            print(f'Average normalized FCT for {stats.num_flows} in '
                  f'{stats.name}:\n{stats.avg_nfct}')
        else:
            json[stats.name]['avgnfct'] = stats.avg_nfct
            json[stats.name]['tailnfct'] = stats.nfct_99th

    if report_num_to:
        if json is None:
            print(f'Total number of timeouts for {stats.num_flows} in '
                  f'{stats.name}:\n{stats.num_timeouts}')
        else:
            json[stats.name]['numtos'] = stats.num_timeouts


parser = argparse.ArgumentParser()
parser.add_argument("--input", help="input file name")
parser.add_argument("--ofct-input", help="ofct input file name")
parser.add_argument("--rate", help="link rate")
parser.add_argument("--link-delay", help="link propagation delay")
parser.add_argument("--host-delay", help="host delay")
parser.add_argument("--chunks-alpha", help="alpha used for chunk generation")
parser.add_argument("--bound", action="append", type=Bound.from_string,
                    help="bound for a flow set")
parser.add_argument(
    "--all", action="store_true",
    help="print all FCT information")
parser.add_argument(
    "--overall", action="store_true",
    help="print overall FCT information")
parser.add_argument(
    "--small", action="store_true",
    help="print FCT information for short flows")
parser.add_argument(
    "--median", action="store_true",
    help="print FCT information for median flows")
parser.add_argument(
    "--large", action="store_true",
    help="print FCT information for large flows")
parser.add_argument(
    "--avg", action="store_true",
    help="print average FCT information for flows in specific ranges")
parser.add_argument(
    "--avgn", action="store_true",
    help="pring average normalized FCT information"
         " for flows in specific ranges")
parser.add_argument(
    "--avgrealn", action="store_true",
    help="pring average (real) normalized FCT information"
         " for flows in specific ranges")
parser.add_argument(
    "--tail", action="store_true",
    help="only print tail (99th percentile) FCT information for flows"
         "in specific ranges")
parser.add_argument(
    "--to", action="store_true",
    help="only print number of timeouts for flows in specific ranges")
parser.add_argument(
    "--thd", action="store_true",
    help="print number of deadlines met")
parser.add_argument("--gp", action='store_true', help="print goodput")
parser.add_argument("--json", action="store_true", help="output JSON")

args = parser.parse_args()

rate = float(args.rate)
link_delay = float(args.link_delay)
host_delay = float(args.host_delay)

overall = FlowSetStats(
    'overall', Bound(None, None),
    rate, link_delay, host_delay)
short = FlowSetStats(
    'short', Bound(None, 100 * 1024),
    rate, link_delay, host_delay)
median = FlowSetStats(
    'median', Bound(100 * 1024 + 1, 10 * 1024 * 1024),
    rate, link_delay, host_delay)
large = FlowSetStats(
    'large', Bound(10 * 1024 * 1024, None),
    rate, link_delay, host_delay)

custom_sets = [FlowSetStats(f'custom{i}', b,
                            rate, link_delay, host_delay)
               for i, b in enumerate(
               args.bound)] if args.bound else []
flowsets = [overall, short, median, large] + custom_sets


if args.input:
    with open(args.input) as fp, open(args.ofct_input) as ofct_fp:
        while True:
            line = fp.readline()
            ofct_line = ofct_fp.readline()

            if not line:
                break
            if len(line.split()) < 3:
                continue

            pkt_size, time, num_to, src, dst, _, deadline, early, ofct \
                = _parse_line(line, ofct_line)
            byte_size = pkt_size * 1460

            if time == 0:
                continue

            for flow_set in flowsets:
                flow_set.add(
                    src,
                    dst,
                    ofct,
                    time,
                    byte_size,
                    _is_deadline_met(deadline, time, early),
                    _has_deadline(deadline, early),
                    num_to)

    json: Optional[dict] = {} if args.json else None

    if args.all:
        print(f"There are {overall.num_flows} flows in total")
        print(f"There are {overall.num_timeouts} TCP timeouts in total")
        print(f"Overall average FCT is:\n{overall.avg_fct}")
        print(f"Average FCT for {short.num_flows} flows in (0,100KB)"
              f"\n{short.avg_fct}")
        print(
            f"99th percentile FCT for {short.num_flows} flows in (0,100KB)\n"
            f"{short.fct_99th}")
        print(
            f"Average FCT for {median.num_flows} flows in (100KB,10MB)\n"
            f"{median.median_fct}")
        print(
            f"Average FCT for {large.num_flows} flows in (10MB,)\n"
            f"{large.avg_fct}")

    # Overall FCT information
    if args.overall:
        report(args.avg, args.tail, args.thd, args.gp, args.avgn,
               args.avgrealn, args.to, json, overall)

    # FCT information for short flows
    if args.small:
        report(args.avg, args.tail, args.thd, args.gp, args.avgn,
               args.avgrealn, args.to, json, short)

    # FCT information for median flows
    if args.median:
        report(args.avg, args.tail, args.thd, args.gp, args.avgn,
               args.avgrealn, args.to, json, median)

    # FCT information for large flows
    if args.large:
        report(args.avg, args.tail, args.thd, args.gp, args.avgn,
               args.avgrealn, args.to, json, large)

    for custom_flow_set in custom_sets:
        report(args.avg, args.tail, args.thd, args.gp, args.avgn,
               args.avgrealn, args.to, json, custom_flow_set)

    if json is not None and not args.all:
        print(dumps(json))
