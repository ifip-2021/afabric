#!/usr/bin/env python3

import asyncio
import json
import subprocess
from collections import namedtuple
from csv import DictWriter
from typing import List
from os import path, environ

import click

from congestion_runner.config import Aspect, Config, Run, get_run_name
from congestion_runner.run import config_params

PLOT_DIR = environ['PLOT_PATH']
APPEND_TRUE_FCT = path.join(
    path.dirname(__file__), 'cmake-build-release', 'append_true_fct')
RESULT_PY = path.join(
    path.dirname(__file__), 'result.py')

RunResult = namedtuple('RunResult', ['value', 'results'])


@click.command()
@click.option('--results-dir', type=str, default='results')
@click.option('--aspect',
              type=click.Choice([x.name.lower() for x in Aspect]),
              required=True)
@click.option('--value', type=str, multiple=True, required=True)
@config_params(multi=True)
def generate(results_dir: str,
             aspect: str,
             runs: List[Run],
             value: List[str]):
    for run in runs:
        asyncio.run(generate_for_name(
            results_dir, run, Aspect[aspect.upper()], value))


async def generate_for_name(
        results_dir: str,
        run: Run,
        aspect: Aspect,
        values: List[str]):

    value_configs = [(nv, Config.from_file(
        'config.toml',
        {a: run[a] if a is not aspect else nv for a in Aspect}))
        for nv in values]

    run_results = await asyncio.gather(*(
        generate_for(results_dir, v, c) for v, c in value_configs))

    save_result(aspect, run, run_results)


async def generate_for(
        results_dir: str, value: str, config: Config
        ) -> RunResult:
    return RunResult(value, await _get_all_results(
        path.join(results_dir, config.run_name), config))


def save_result(aspect: Aspect, run: Run, run_results: List[RunResult]):
    _, first_result = run_results[0]
    filename = f'{PLOT_DIR}/{get_run_name(run)}.csv'
    print(f'saving {filename}')

    fields = [aspect.name.lower()] + [
            f'{flow_kind}_{metric}'
            for flow_kind in first_result.keys()
            for metric in first_result[flow_kind].keys()]

    with open(filename, 'w') as csvfile:
        writer = DictWriter(csvfile, fields)
        writer.writeheader()
        for v, result in run_results:
            row_dict = {
                f'{flows}_{value}': result[flows][value]
                for flows in result.keys()
                for value in result[flows].keys()
            }
            row_dict[aspect.name.lower()] = v
            writer.writerow(row_dict)


async def _get_all_results(directory_name: str, config: Config) -> dict:
    flow_path = path.join(directory_name, 'flow.tr')
    ofct_path = path.join(directory_name, 'ofct.tr')

    with open(flow_path) as flow_file, open(ofct_path, 'w') as ofct_file:
        args = [str(config.alpha), str(config.link_rate_bps),
                str(config['mean_link_delay']), str(config['host_delay']),
                str(config['max_num_chunks'])]
        process = await asyncio.create_subprocess_exec(
                    APPEND_TRUE_FCT, *args, stdin=flow_file, stdout=ofct_file)
        await process.wait()

    args = [RESULT_PY,
            '--input', flow_path, '--ofct-input', ofct_path,
            '--overall', '--small', '--median', '--large',
            '--avg', '--avgn', '--avgrealn', '--tail',
            '--thd', '--json', '--gp', '--to',
            '--link-delay', str(config['mean_link_delay']),
            '--host-delay', str(config['host_delay']),
            '--rate', str(config.link_rate_bps)]

    if config.alpha is not None:
        args.extend(['--chunks-alpha', str(config.alpha)])

    for gb in config.plot_generation_bounds:
        args.extend(['--bound', gb])

    process = await asyncio.create_subprocess_exec(
            'python', *args, stdout=subprocess.PIPE)
    (sout, serr) = await process.communicate()

    result = json.loads(sout)
    result['overall']['drops'] = _get_all_drops(directory_name)

    return result


def _get_all_drops(directory_name: str) -> int:
    with open(path.join(directory_name, 'drop.tr')) as drop_file:
        return sum(int(line.split()[1]) for line in drop_file)


if __name__ == '__main__':
    generate()
