from abc import ABC, abstractmethod
from enum import Enum
from typing import List, Tuple


def _interpolate_none(start: float, end: float):
    return end


def _interpolate_integral(start: float, end: float):
    result = start + (end - start - 1) / 2
    return result


def _interpolate_continous(start: float, end: float):
    return (end + start) / 2


class EmpiricalInterpolation(Enum):
    NONE = (0, _interpolate_none)
    CONTINUOUS = (1, _interpolate_continous)
    INTEGRAL = (2, _interpolate_integral)

    def mean(self, cdf: List[Tuple[float, float]]) -> float:
        return sum((x2 - x1) * self.value[1](y1, y2)
                   for ((y1, x1), (y2, x2)) in zip(cdf[:-1], cdf[1:]))

    @property
    def code(self) -> int:
        return self.value[0]


class RandomVariable(ABC):
    @property
    @abstractmethod
    def name(self) -> str:
        pass

    @property
    @abstractmethod
    def mean(self) -> float:
        pass

    @property
    @abstractmethod
    def args(self) -> List[str]:
        pass


class FixedRandomVariable(RandomVariable):
    def __init__(self, value: float):
        self.value = value

    @property
    def mean(self) -> float:
        return self.value

    @property
    def name(self) -> str:
        return 'fixed'

    @property
    def args(self) -> List[str]:
        return [f'{self.value}']


class UniformRandomVariable(RandomVariable):
    def __init__(self, min_val: float, max_val: float):
        self.min_val = min_val
        self.max_val = max_val

    @property
    def mean(self) -> float:
        return (self.min_val + self.max_val) * 0.5

    @property
    def name(self) -> str:
        return 'uniform'

    @property
    def args(self) -> List[str]:
        return [f'{self.min_val}', f'{self.max_val}']


class EmpiricalRandomvVariable(RandomVariable):
    def __init__(self, cdf_file: str, interpolation: EmpiricalInterpolation):
        self.cdf_file = cdf_file
        self.interpolation = interpolation

    @property
    def name(self) -> str:
        return 'empirical'

    @property
    def args(self) -> List[str]:
        return [self.cdf_file, str(self.interpolation.code)]

    @property
    def mean(self) -> float:
        return self.interpolation.mean(self._read_cdf())

    def _read_cdf(self) -> List[Tuple[float, float]]:
        with open(self.cdf_file) as cdf_file:
            return [(float(x), float(z))
                    for x, _, z in [line.strip().split() for line in cdf_file
                                    if line.strip()]]


class ExponentialRandomVariable(RandomVariable):
    def __init__(self, rate: float):
        self.rate = rate

    @property
    def name(self) -> str:
        return 'exponential'

    @property
    def mean(self) -> float:
        return 1.0 / self.rate

    @property
    def args(self) -> List[str]:
        return [f'{self.rate}']
