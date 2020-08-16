import subprocess
import json
import os
import sys
from itertools import product
from os import path
from shutil import which
from typing import Any, List, Optional, Tuple

import click

import congestion_runner.random_variables as rvs
from congestion_runner.config import Aspect, Config, Run

SCRIPT_LOCATION = path.dirname(path.abspath(__file__))
DELAY_ASSIGNMENT_JSON_FNAME = 'delay_assignment.json'
PACKET_PROPERTIES_ASSIGNMENT_JSON_FNAME = 'packet_properties_assignment.json'


class Options:
    def __init__(
        self,
        dry_run: bool,
        valgrind: bool,
        perf: bool,
        debug: bool,
    ):
        self.dry_run = dry_run
        self.valgrind = valgrind
        self.perf = perf
        self.debug = debug


RunResult = Tuple[int, Optional[str], Optional[str]]


def run_it(
    exe: str,
    args: List[str],
    directory_name: str,
    delay_assignment_json: Optional[dict],
    packet_properties_assignment_json: Optional[dict],
    opt: Options,
) -> RunResult:

    # Make directory to save results
    if not os.path.exists(directory_name):
        os.makedirs(directory_name)

    if delay_assignment_json is not None:
        with open(path.join(directory_name, DELAY_ASSIGNMENT_JSON_FNAME),
                  'w') as delay_file:
            json.dump(delay_assignment_json, delay_file)

    with open(path.join(
            directory_name,
            PACKET_PROPERTIES_ASSIGNMENT_JSON_FNAME
    ), 'w') as packet_properties_file:
        json.dump(packet_properties_assignment_json, packet_properties_file)

    exe_args = [exe]

    print(' '.join(f'"{arg}"' for arg in args))

    if opt.dry_run:
        return (0, None, None)

    if opt.valgrind:
        exe_args = ['valgrind'] + exe_args

    if opt.perf:
        debug_args = ['--call-graph', 'dwarf'] if opt.debug else []
        exe_args = ['perf', 'record'] + debug_args + exe_args

    with open(path.join(directory_name, 'stdout.log'), 'w') as stdout:
        with open(path.join(directory_name, 'stderr.log'), 'w') as stderr:
            process = subprocess.run(
                    exe_args + args, stdout=stdout, stderr=stderr)

    if process.returncode != 0:
        with open(path.join(directory_name, 'stdout.log')) as stdout:
            with open(path.join(directory_name, 'stderr.log')) as stderr:
                return (process.returncode, stdout.read(), stderr.read())

    return (0, None, None)


def run_single_config(
        run: Run,
        ns_path: str,
        results_dir: str,
        opt: Options) -> RunResult:
    config = Config.from_file('config.toml', run)

    directory_name = path.join(results_dir, config.run_name)

    # TODO: magic constant, fix!
    sim_script = os.path.abspath(
        config.custom_script or 'spine_empirical.tcl')

    delay_assignment_json_filename = None

    if config.delay_assignment_json is not None:
        delay_assignment_json_filename = path.abspath(path.join(
            directory_name, DELAY_ASSIGNMENT_JSON_FNAME))

    packet_properties_assignment_json_filename = None

    if config.packet_properties_assignment_json is not None:
        packet_properties_assignment_json_filename = path.abspath(
            path.join(directory_name,
                      PACKET_PROPERTIES_ASSIGNMENT_JSON_FNAME))

    args = [
        sim_script,
        config['sim_end'],
        config['link_rate'],
        config['mean_link_delay'],
        config['host_delay'],
        config['queue_size'],
        config['good_queue_size'],
        config['connections_per_pair'],
        config.arrivals_config.flow_size_rv,
        config.arrivals_config.flow_intval_rv,
        config.alpha,
        config.arrivals_config.use_alpha_probability,
        config['max_num_chunks'],
        config['enable_multi_path'],
        config['per_flow_mp'],
        config['source_alg'],
        config['init_window'],
        config['ack_ratio'],
        config['slow_start_restart'],
        config['dctcp_g'],
        config['min_rto'],
        config['eof_min_rto'],
        config['no_eof_min_rto'],
        config['prob_cap'],
        config['switch_alg'],
        config['dctcp_k'],
        config['drop_prio'],
        config.priority_scheme.value,
        config['deque_prio'],
        config['keep_order'],
        config['drop_low_prio'],
        config['prio_num'],
        config['ecn_scheme'],
        config.get_pias_threshold(0),
        config.get_pias_threshold(1),
        config.get_pias_threshold(2),
        config.get_pias_threshold(3),
        config.get_pias_threshold(4),
        config.get_pias_threshold(5),
        config.get_pias_threshold(6),
        config['topology_spt'],
        config['topology_tors'],
        config['topology_spines'],
        config['topology_x'],
        path.abspath(path.join(directory_name, 'flow.tr')),
        path.abspath(path.join(directory_name, 'drop.tr')),
        delay_assignment_json_filename,
        config['use_fifo_processing_order'],
        config['use_deadline'],
        packet_properties_assignment_json_filename,
        config['expiration_time_controller'],
        config['enable_delay'],
        config['enable_dupack'],
        config['enable_early_expiration'],
        config['use_true_remaining_size'],
        config['rtx_on_eof'],
        config['reset_window_on_eof'],
        config['afabric_ecn_enable'],
    ]

    args = [_to_tcl_arg(arg) for arg in args]

    return run_it(
        ns_path,
        args,
        path.abspath(directory_name),
        config.delay_assignment_json,
        config.packet_properties_assignment_json,
        opt)


def _to_tcl_arg(arg: Any) -> str:
    if isinstance(arg, str):
        return arg
    if isinstance(arg, bool):
        return str(arg).lower()
    if isinstance(arg, list):
        return ' '.join(arg)
    if isinstance(arg, rvs.RandomVariable):
        return f'create_{arg.name}_rv {" ".join(arg.args)}'
    if arg is None:
        return ''
    else:
        return str(arg)


def config_params(multi=False):
    def decorator(func):
        def single_wrapper(**kwargs):
            run = {aspect: kwargs[aspect.name.lower()] for aspect in Aspect}

            for aspect in Aspect:
                del kwargs[aspect.name.lower()]

            func(run=run, **kwargs)

        def multi_wrapper(**kwargs):
            runs = [{
                aspect: choice for aspect, choice in zip(Aspect, run)
                if choice is not None
                } for run in product(*(
                    kwargs.get(aspect.name.lower()) or [None]
                    for aspect in Aspect
                ))
                ]
            for aspect in Aspect:
                del kwargs[aspect.name.lower()]
            func(runs=runs, **kwargs)

        wrapper = multi_wrapper if multi else single_wrapper
        for aspect in Aspect:
            wrapper = click.option(
                f'--{aspect.name.lower()}',
                type=str,
                multiple=multi,
                )(wrapper)
        return wrapper
    return decorator


def _get_ns_path(debug: bool, ns_executable: str):
    if ns_executable is not None:
        return ns_executable
    else:
        if which('ns') is not None:
            return which('ns')
        elif 'DEV_NS_DIR' in os.environ:
            ns_path_template = path.join(
                os.environ['DEV_NS_DIR'], 'cmake-build-{}', 'ns')

            if debug:
                return ns_path_template.format('debug')
            else:
                return ns_path_template.format('release')
        else:
            raise RuntimeError('ns executable not found')


@click.command()
@click.option('--dry-run', is_flag=True)
@click.option('--valgrind', is_flag=True)
@click.option('--perf', is_flag=True)
@click.option('--debug', is_flag=True)
@click.option('--ns-executable', type=click.Path())
@click.option('--results-dir', type=click.Path(), default='results')
@config_params()
def main(debug: bool,
         run: Run,
         dry_run: bool,
         valgrind: bool,
         perf: bool,
         ns_executable: str,
         results_dir: str):
    ns_path = _get_ns_path(debug, ns_executable)
    opt = Options(dry_run=dry_run, valgrind=valgrind, perf=perf, debug=debug)

    result: RunResult = run_single_config(run, ns_path, results_dir, opt)

    if result[0] != 0:
        sout, serr = result[1:]
        sys.stdout.write(str(sout))
        sys.stderr.write(str(serr))
        exit(1)


if __name__ == '__main__':
    main()
