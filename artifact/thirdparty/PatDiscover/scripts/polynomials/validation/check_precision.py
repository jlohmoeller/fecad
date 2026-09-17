from pathlib import Path

import numpy as np
from numpy.polynomial import Chebyshev

import matplotlib.pyplot as plt


def eval_composite(composite: list[np.array], value):
    res = value
    for coeffs in composite:
        res = Chebyshev(coeffs)(res)
    
    return res

def eval_lt(composite: list[np.array], v1, v2):
    sign = eval_composite(composite, v2 - v1)
    sign_sq = sign ** 2

    return 0.5 * sign_sq * (sign + 1)


root_dir = Path(__file__).parent.parent.parent.parent
poly_file = root_dir / "data" / "polynomials" / "16bit_sign.poly"

composite = []
with open(poly_file, "r") as f:
    for line in f.readlines():
        coeffs = np.array(list(map(lambda x: float(x), line.strip().split(" "))))
        composite.append(coeffs)

desired_precision = 16

smallest_step = 1 / (1 << desired_precision)

print(f"Results: {eval_composite(composite, -smallest_step)}, {eval_composite(composite, 0)}, {eval_composite(composite, smallest_step)}")
print(f"Results: {eval_lt(composite, 0, smallest_step)}, {eval_lt(composite, smallest_step, 0)}")


x = np.linspace(-1, 1, 1 << (desired_precision + 5))
y = eval_composite(composite, x)

plt.plot(x, y)
plt.vlines([-smallest_step, 0, smallest_step], -1.5, 1.5, linestyles='--', colors='gray')

plt.show()
