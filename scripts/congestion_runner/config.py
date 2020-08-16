from __future__ import annotations

import functools
import os
from enum import Enum, auto
from typing import Any, Dict, List, Mapping, Optional, Set

import toml

import congestion_runner.random_variables as rvs

BITS_PER_BYTE = 8
PACKET_SIZE = 1460
LINK_LAYER_PACKET_SIZE = 1500  # copied from .tcl long time ago, not sure why
_FNAME_SEPARATOR = '.'


class PriorityScheme(Enum):
    UNKNOWN = 5
    REMAINING_SIZE = 2
    BYTES_SENT = 3
    LAZY_REMAINING_SIZE = 6
    LAZY_REMAINING_SIZE_BYTES_SENT = 7


class DelayAssignmentConfig:
    def __init__(self, config: Dict[str, str], link_rate: float, load: float):
        self._config = config
        self._link_rate = link_rate
        self._load = load

    @property
    def json(self) -> dict:
        if 'delay' in self._config:
            return self.get_json_fixed_delay(
                float(self._config['delay']))
        elif 'delay_min' in self._config:
            return self.get_json_range()
        elif 'exp' in self._config:
            return self.get_exp_json(
                float(self._config['exp']),
                self._config['use_capping'] == 'true')
        elif 'gp' in self._config:
            return self.get_json_gp(int(self._config['gp']))
        elif 'nfct' in self._config:
            return self.get_json_gp(
                int(8.0 / float(self._config['nfct'])))
        elif 'file' in self._config:
            return self.get_json_file(self._config['file'])
        elif 'uniform_min_delay' in self._config:
            return self._get_json_uniform(
                float(self._config['uniform_min_delay']),
                float(self._config['uniform_max_delay']))
        else:
            raise AssertionError("unknown delay asignment")

    def get_json_fixed_delay(self, delay: float):
        result = {
            'type': 'fixed',
            'parameters': {
                'delay': delay,
            }
        }

        self.attach_size_bounds(result)

        return result

    @staticmethod
    def get_json_min(subjsons):
        return {
            'type': 'min',
            'parameters': subjsons
        }

    def get_json_range(self):
        min_delay, max_delay = (
            float(self._config['delay_min']),
            float(self._config['delay_max']))
        num_steps = int(self._config['count'])
        step = (max_delay - min_delay) / (num_steps - 1)
        return [self.get_json_fixed_delay(min_delay + step * i)
                for i in range(num_steps)]

    def get_exp_json(self, delay, use_capping):
        result = {
            'type': 'exponential',
            'parameters': {
                'average': delay,
                'link_speed': self._link_rate * 10 ** 9,
                'use_capping': use_capping,
            }
        }

        self.attach_size_bounds(result)

        return result

    def get_json_file(self, file_pattern):
        return {
            'type': 'file',
            'parameters': os.path.abspath(file_pattern.format(
                int(self._load * 100)))
        }

    def attach_size_bounds(self, result: dict):
        if 'size_upper_bound' in self._config:
            result['parameters']['size_upper_bound'] = \
                int(self._config['size_upper_bound'])
        if 'size_lower_bound' in self._config:
            result['parameters']['size_lower_bound'] = \
                int(self._config['size_lower_bound'])

    def get_json_gp(self, goodput):
        result = {
            'type': 'goodput',
            'parameters': {'goodput': goodput}
        }

        self.attach_size_bounds(result)

        return result

    @staticmethod
    def _get_json_uniform(min_delay, max_delay):
        return {
            'type': 'uniform',
            'parameters': {
                'min_delay': min_delay,
                'max_delay': max_delay,
            }
        }


class PacketPropertiesAssignerConfig:
    def __init__(self, config: Dict[str, str]):
        self._config = config

    @property
    def json(self) -> Optional[dict]:
        if len(self._config) == 0:
            return None
        if 'uniform' in self._config:
            return {'type': 'uniform'}
        elif 'inv_size' in self._config:
            return {'type': 'inv_size'}
        elif 'inv_rem_size' in self._config:
            return {'type': 'inv_rem_size'}
        elif 'las' in self._config:
            return {'type': 'las'}
        else:
            raise AssertionError


class ArrivalsConfig:
    def __init__(self, config: Dict[str, Any], per_server_rate_bps: float):
        self._config = config
        self._per_server_rate_bps = per_server_rate_bps

    @property
    def flow_intval_rv(self) -> rvs.RandomVariable:
        mean_flow_load_bits = (
            self.mean_flow_size_in_bytes * BITS_PER_BYTE
            / PACKET_SIZE * LINK_LAYER_PACKET_SIZE)
        arrival_rate = self._per_server_rate_bps / mean_flow_load_bits
        return rvs.ExponentialRandomVariable(arrival_rate)

    @property
    def use_alpha_probability(self) -> Optional[float]:
        if 'use_alpha_probability' not in self._config:
            return None
        return float(self._config['use_alpha_probability'])

    @property
    def flow_size_rv(self) -> rvs.RandomVariable:
        return self._rv_from_dict(self._config['flow_size'])

    @staticmethod
    def _rv_from_dict(json: dict) -> rvs.RandomVariable:
        if 'cdf' in json:
            return rvs.EmpiricalRandomvVariable(
                os.path.abspath(json['cdf']),
                rvs.EmpiricalInterpolation.INTEGRAL)
        if 'fixed' in json:
            return rvs.FixedRandomVariable(json['fixed'])
        if 'uniform' in json:
            return rvs.UniformRandomVariable(
                json['uniform']['min'],
                json['uniform']['max'])
        raise AssertionError(f'Unknown random variable: {json}')

    @property
    def mean_flow_size(self) -> int:
        if 'mean_flow_size' in self._config:
            return int(self._config['mean_flow_size'])
        return int(self.flow_size_rv.mean)

    @property
    def mean_flow_size_in_bytes(self) -> int:
        return self.mean_flow_size * PACKET_SIZE


class Aspect(Enum):
    INPUT = auto()
    BUFFER = auto()
    CONTROL = auto()
    SLACKS = auto()
    SCALE = auto()
    LOAD = auto()
    ALPHA = auto()


Run = Mapping[Aspect, str]


def get_run_name(run: Run) -> str:
    return _FNAME_SEPARATOR.join(
        v if a is not Aspect.LOAD and a is not Aspect.ALPHA
        else f'{int(float(v)*100)}'
        for a, v in run.items())


class Config:
    DELAYS = [0.9, 0.8, 0.7, 0.6, 0.5]

    def __init__(
            self,
            run: Run,
            default: Mapping[Any, Any],
            inherited: List[Dict[Any, Any]],
            ):
        self._run = run
        self._default = default
        self._inherited = inherited

    @property
    def load(self) -> float:
        return float(self._run[Aspect.LOAD])

    @property
    def alpha(self) -> Optional[float]:
        if Aspect.ALPHA not in self._run:
            return None
        return float(self._run[Aspect.ALPHA])

    @property
    def run_name(self) -> str:
        return get_run_name(self._run)

    @classmethod
    def from_file(cls, filename: str, run: Run) -> Config:
        config = toml.load(filename)

        inherited = []

        for k, v in run.items():
            if k is not Aspect.LOAD and k is not Aspect.ALPHA:
                inherited.append(dict(config[k.name.lower()][v]))
                del config[k.name.lower()]

        return cls(run, config, inherited)

    @property
    def num_servers(self) -> int:
        return int(self['topology_spt']) * int(self['topology_tors'])

    @property
    def plot_generation_bounds(self):
        if 'plot_generation_bounds' not in self:
            return []
        return self['plot_generation_bounds'].split()

    @property
    def priority_scheme(self):
        return PriorityScheme[self['prio_scheme'].upper()]

    @property
    def packet_properties_assignment_json(self) -> Optional[dict]:
        if 'packet_properties_assignment' not in self:
            return None
        packet_properties_assignment_config = PacketPropertiesAssignerConfig(
            self['packet_properties_assignment'])
        return packet_properties_assignment_config.json

    @property
    def link_rate_gbps(self) -> float:
        return float(self['link_rate'])

    @property
    def link_rate_bps(self) -> float:
        return self.link_rate_gbps * 10**9

    def get_load_specific_var(self, var: str, load: float) -> Any:
        if isinstance(self[var], list):
            return {d: v for d, v in zip(self.DELAYS, self[var])}[load]
        else:
            return self[var]

    def keys(self) -> Set[str]:
        return functools.reduce(
            lambda x, y: x | y,
            [self._default.keys()] + [inh.keys() for inh in self._inherited],
            set())

    def __getitem__(self, item: str) -> Any:
        inherited = [inh[item] for inh in self._inherited if item in inh]
        if len(inherited) == 1:
            result = inherited[0]
        elif len(inherited) > 1:
            raise KeyError(f'key {item} is inherited from multiple bases')
        else:
            result = self._default[item]
        if isinstance(result, list):
            return {d: v for d, v in zip(self.DELAYS, result)}[self.load]
        else:
            return result

    def get(self, item: str, default=None):
        try:
            return self[item]
        except KeyError:
            return default

    def __contains__(self, item):
        return any(item in inh for inh in self._inherited + [self._default])

    @property
    def custom_script(self) -> Optional[str]:
        return self.get('custom_script', None)

    def get_pias_threshold(self, idx: int) -> int:
        return PACKET_SIZE * self[f'pias_thresh_{idx}']

    @property
    def delay_assignment_json(self) -> Optional[dict]:
        config = self.get('delay_assignment')
        if config is None:
            return None

        delay_assignment_config = DelayAssignmentConfig(
            config,
            self.link_rate_gbps,
            int(self.load * 100))
        return delay_assignment_config.json

    @property
    def arrivals_config(self) -> ArrivalsConfig:
        return ArrivalsConfig(self['arrivals'], self.per_server_rate_bps)

    @property
    def per_server_rate_bps(self) -> float:
        return self.link_rate_bps * self.load / (self.num_servers - 1)


def _strip_prefix(text: str, prefix: str) -> str:
    if text.startswith(prefix):
        return text[len(prefix):]
    else:
        raise ValueError(f'"{text}" does not have prefix "{prefix}"')
