from few.waveform import EMRIInspiral
import numpy as np
import matplotlib.pyplot as plt

traj_res = EMRIInspiral(func="pn5", orbital_resonances=True)

M = 1e6
mu = 1e2
a = 0.5
p0 = 10.
e0 = 0.2
Y0 = 0.7

dt=1e5
T=10.


trh = traj_res(M, mu, a, p0, e0, Y0, T=T,dt=dt,)