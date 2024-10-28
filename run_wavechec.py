from few.waveform import Pn5TrajPn5AdiabaticWaveform, EMRIInspiral
import numpy as np
import matplotlib.pyplot as plt


M = 1e6
mu = 1e2
a = 0.9
p0 = 9.524
e0 = 0.21
Y0 = np.cos(80*np.pi/180)

dt=1.
T=5e-3

wf = Pn5TrajPn5AdiabaticWaveform(inspiral_kwargs=dict(orbital_resonances=True, err=1e-15))
tr = EMRIInspiral(func="pn5")
print(len(tr(M, mu, a, p0, e0, Y0, T=T, dt=10.)[0]))
print(wf(M, mu, a, p0, e0, Y0, np.pi/3, 0., T=T, dt=dt))
