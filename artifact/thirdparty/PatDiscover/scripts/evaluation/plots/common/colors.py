import matplotlib.cm as mplcm
import matplotlib.colors as colors
import matplotlib.pyplot as plt

import scienceplots

plt.style.use(['science', 'ieee', 'vibrant'])
_palette = plt.rcParams['axes.prop_cycle'].by_key()['color']

def blend_color(c, perc):
    r, g, b = colors.to_rgb(c)
    return (1.0 * (1 - perc) + r * perc, 1.0 * (1 - perc) + g * perc, 1.0 * (1 - perc) + b * perc)

# precise_color = _palette[0]
# approx_color = _palette[1]

server_color = '#7EA6E0'
client_color = '#FFD966'

# attribute_size_color = _palette[0]

patient_attribute_color = '#7EA6E0'
query_color = blend_color('#D81159', 0.75)
result_color = '#FFD966'
