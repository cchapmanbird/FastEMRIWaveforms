# Interpolated summation of modes in python for the FastEMRIWaveforms Package

# Copyright (C) 2020 Michael L. Katz, Alvin J.K. Chua, Niels Warburton, Scott A. Hughes
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import warnings

import numpy as np

# try to import cupy
try:
    import cupy as cp

except (ImportError, ModuleNotFoundError) as e:
    import numpy as np

# Cython imports
from pyinterp_cpu import interpolate_arrays_wrap as interpolate_arrays_wrap_cpu
from pyinterp_cpu import get_waveform_wrap as get_waveform_wrap_cpu

# Python imports
from few.utils.baseclasses import SummationBase, SchwarzschildEccentric, GPUModuleBase
from few.utils.citations import *
from few.utils.utility import get_fundamental_frequencies
from few.utils.constants import *

# Attempt Cython imports of GPU functions
try:
    from pyinterp import interpolate_arrays_wrap, get_waveform_wrap

except (ImportError, ModuleNotFoundError) as e:
    pass


class CubicSplineInterpolant(GPUModuleBase):
    """GPU-accelerated Multiple Cubic Splines

    This class produces multiple cubic splines on a GPU. It has a CPU option
    as well. The cubic splines are produced with "not-a-knot" boundary
    conditions.

    This class can be run out of Python similar to
    `scipy.interpolate.CubicSpline <https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.CubicSpline.html#scipy-interpolate-cubicspline>`_.
    However, the most efficient way to use this method is in a customized
    cuda kernel. See the
    `source code for the interpolated summation<https://github.com/BlackHolePerturbationToolkit/FastEMRIWaveforms/blob/master/src/interpolate.cu>`_
    in cuda for an example of this.

    This class can be run on GPUs and CPUs.

    args:
        t (1D double xp.ndarray): t values as input for the spline.
        y_all (1D or 2D double xp.ndarray): y values for the spline.
            Shape: (length,) or (ninterps, length).
        use_gpu (bool, optional): If True, prepare arrays for a GPU. Default is
            False.

    """

    def __init__(self, t, y_all, **kwargs):
        ParallelModuleBase.__init__(self, **kwargs)

        GPUModuleBase.__init__(self, use_gpu=use_gpu)

        if use_gpu:
            self.interpolate_arrays = interpolate_arrays_wrap

        else:
            self.interpolate_arrays = interpolate_arrays_wrap_cpu

        y_all = self.xp.atleast_2d(y_all)

        # hardcoded,only cubic spline is available
        self.degree = 3

        # get quantities related to how many interpolations
        ninterps, length = y_all.shape
        self.ninterps, self.length = ninterps, length

        # store this for reshaping flattened arrays
        self.reshape_shape = (self.degree + 1, ninterps, length)

        # interp array is (y, c1, c2, c3)
        interp_array = xp.zeros(self.reshape_shape)

        # fill y
        interp_array[0] = y_all

        interp_array = interp_array.flatten()

        # arrays to store banded matrix and solution
        B = xp.zeros((ninterps * length,))
        upper_diag = xp.zeros_like(B)
        diag = xp.zeros_like(B)
        lower_diag = xp.zeros_like(B)

        if t.ndim == 1:
            if len(t) < 2:
                raise ValueError("t must have length greater than 2.")

            # could save memory by adjusting c code to treat 1D differently
            self.t = xp.tile(t, (ninterps, 1)).flatten().astype(xp.float64)

        elif t.ndim == 2:
            if t.shape[1] < 2:
                raise ValueError("t must have length greater than 2 along time axis.")

            self.t = t.flatten().copy().astype(xp.float64)

        else:
            raise ValueError("t must be 1 or 2 dimensions.")

        self.t = t.astype(xp.float64)
        # perform interpolation
        self.interpolate_arrays(
            self.t, interp_array, ninterps, length, B, upper_diag, diag, lower_diag,
        )

        # set up storage of necessary arrays

        self.interp_array = self.xp.transpose(
            interp_array.reshape(self.reshape_shape), [0, 2, 1]
        ).flatten()

        # update reshape_shape
        self.reshape_shape = (self.degree + 1, length, ninterps)
        self.t = self.t.reshape((ninterps, length))

    def attributes_CubicSplineInterpolate(self):
        """
        attributes:
            interpolate_arrays (func): CPU or GPU function for mode interpolation.
            interp_array (1D double xp.ndarray): Array containing all spline
                coefficients. It is flattened after fitting from shape
                (4, length, ninterps). The 4 is the 4 spline coefficients.

        """

    @property
    def gpu_capability(self):
        """Confirms GPU capability"""
        return True

    @property
    def citation(self):
        """Return the citation for this class"""
        return few_citation + few_software_citation

    @property
    def y(self):
        """y values associated with the spline"""
        return self.interp_array.reshape(self.reshape_shape)[0].T

    @property
    def c1(self):
        """constants for the linear term"""
        return self.interp_array.reshape(self.reshape_shape)[1].T

    @property
    def c2(self):
        """constants for the quadratic term"""
        return self.interp_array.reshape(self.reshape_shape)[2].T

    @property
    def c3(self):
        """constants for the cubic term"""
        return self.interp_array.reshape(self.reshape_shape)[3].T

    def _get_inds(self, tnew):
        # find were in the old t array the new t values split

        if self.use_gpu:
            xp = cp
        else:
            xp = np

        inds = xp.zeros((self.ninterps, tnew.shape[1]), dtype=int)

        # Optional TODO: remove loop ? if speed needed
        for i, (t, tnew_i) in enumerate(zip(self.t, tnew)):
            inds[i] = xp.searchsorted(t, tnew_i, side="right") - 1

            # fix end value
            inds[i][tnew_i == t[-1]] = len(t) - 2

        # get values outside the edges
        inds_bad_left = tnew < self.t[:, 0][:, None]
        inds_bad_right = tnew > self.t[:, -1][:, None]

        if np.any(inds < 0) or np.any(inds >= self.t.shape[1]):
            warnings.warn(
                "New t array outside bounds of input t array. These points are filled with edge values."
            )
        return inds, inds_bad_left, inds_bad_right

    def __call__(self, tnew, deriv_order=0):
        """Evaluation function for the spline

        Put in an array of new t values at which all interpolants will be
        evaluated. If t values are outside of the original t values, edge values
        are used to fill the new array at these points.

        args:
            tnew (1D or 2D double xp.ndarray): Array of new t values. All of these new
                t values must be within the bounds of the input t values,
                including the beginning t and **excluding** the ending t. If tnew is 1D
                and :code:`self.t` is 2D, tnew will be cast to 2D.
            deriv_order (int, optional): Order of the derivative to evaluate. Default
                is 0 meaning the basic spline is evaluated. deriv_order of 1, 2, and 3
                correspond to their respective derivatives of the spline. Unlike :code:`scipy`,
                this is purely an evaluation of the derivative values, not a new class to evaluate
                for the derivative.

        raises:
            ValueError: a new t value is not in the bounds of the input t array.

        returns:
            xp.ndarray: 1D or 2D array of evaluated spline values (or derivatives).

        """
        if self.use_gpu:
            xp = cp
        else:
            xp = np

        tnew = xp.atleast_1d(tnew)

        # get values outside the edges
        inds_bad_left = tnew < self.t[0]
        inds_bad_right = tnew > self.t[-1]

        if np.any(inds < 0) or np.any(inds >= len(self.t)):
            warnings.warn(
                "New t array outside bounds of input t array. These points are filled with edge values."
            )

        # copy input to all splines
        elif tnew.ndim == 1:
            tnew = xp.tile(tnew, (self.t.shape[0], 1))

        out = (
            self.y[:, inds]
            + self.c1[:, inds] * x
            + self.c2[:, inds] * x2
            + self.c3[:, inds] * x3
        )

        # fix bad values
        if self.xp.any(inds_bad_left):
            out[:, inds_bad_left] = self.y[:, 0]

        if self.xp.any(inds_bad_right):
            out[:, inds_bad_right] = self.y[:, -1]
        return out.squeeze()

        # get indices into spline
        inds, inds_bad_left, inds_bad_right = self._get_inds(tnew)

        # x value for spline

        # indexes for which spline
        inds0 = np.tile(np.arange(self.ninterps), (tnew.shape[1], 1)).T
        t_here = self.t[(inds0.flatten(), inds.flatten())].reshape(
            self.ninterps, tnew.shape[1]
        )

        x = tnew - t_here
        x2 = x * x
        x3 = x2 * x

        # get spline coefficients
        y = self.y[(inds0.flatten(), inds.flatten())].reshape(
            self.ninterps, tnew.shape[1]
        )
        c1 = self.c1[(inds0.flatten(), inds.flatten())].reshape(
            self.ninterps, tnew.shape[1]
        )
        c2 = self.c2[(inds0.flatten(), inds.flatten())].reshape(
            self.ninterps, tnew.shape[1]
        )
        c3 = self.c3[(inds0.flatten(), inds.flatten())].reshape(
            self.ninterps, tnew.shape[1]
        )

        # evaluate spline
        if deriv_order == 0:
            out = y + c1 * x + c2 * x2 + c3 * x3
            # fix bad values
            if xp.any(inds_bad_left):
                temp = xp.tile(self.y[:, 0], (tnew.shape[1], 1)).T
                out[inds_bad_left] = temp[inds_bad_left]

            if xp.any(inds_bad_right):
                temp = xp.tile(self.y[:, -1], (tnew.shape[1], 1)).T
                out[inds_bad_right] = temp[inds_bad_right]

        else:
            # derivatives
            if xp.any(inds_bad_right) or xp.any(inds_bad_left):
                raise ValueError(
                    "x points outside of the domain of the spline are not supported when taking derivatives."
                )
            if deriv_order == 1:
                out = c1 + 2 * c2 * x + 3 * c3 * x2
            elif deriv_order == 2:
                out = 2 * c2 + 6 * c3 * x
            elif deriv_order == 3:
                out = 6 * c3
            else:
                raise ValueError("deriv_order must be within 0 <= deriv_order <= 3.")

        return out.squeeze()


class InterpolatedModeSum(SummationBase, SchwarzschildEccentric, GPUModuleBase):
    """Create waveform by interpolating sparse trajectory.

    It interpolates all of the modes of interest and phases at sparse
    trajectories. Within the summation phase, the values are calculated using
    the interpolants and summed.

    This class can be run on GPUs and CPUs.

    """

    def __init__(self, *args, **kwargs):

        GPUModuleBase.__init__(self, *args, **kwargs)
        SchwarzschildEccentric.__init__(self, *args, **kwargs)
        SummationBase.__init__(self, *args, **kwargs)

        self.kwargs = kwargs

        if self.use_gpu:
            self.get_waveform = get_waveform_wrap

        else:
            self.get_waveform = get_waveform_wrap_cpu

    def attributes_InterpolatedModeSum(self):
        """
        attributes:
            get_waveform (func): CPU or GPU function for waveform creation.

        """

    @property
    def gpu_capability(self):
        """Confirms GPU capability"""
        return True

    @property
    def citation(self):
        return few_citation + few_software_citation

    def sum(
        self,
        t,
        teuk_modes,
        ylms,
        Phi_phi,
        Phi_r,
        m_arr,
        n_arr,
        *args,
        dt=10.0,
        **kwargs,
    ):
        """Interpolated summation function.

        This function interpolates the amplitude and phase information, and
        creates the final waveform with the combination of ylm values for each
        mode.

        args:
            t (1D double xp.ndarray): Array of t values.
            teuk_modes (2D double xp.array): Array of complex amplitudes.
                Shape: (len(t), num_teuk_modes).
            ylms (1D complex128 xp.ndarray): Array of ylm values for each mode,
                including m<0. Shape is (num of m==0,) + (num of m>0,)
                + (num of m<0). Number of m<0 and m>0 is the same, but they are
                ordered as (m==0 first then) m>0 then m<0.
            Phi_phi (1D double xp.ndarray): Array of azimuthal phase values
                (:math:`\Phi_\phi`).
            Phi_r (1D double xp.ndarray): Array of radial phase values
                 (:math:`\Phi_r`).
            m_arr (1D int xp.ndarray): :math:`m` values associated with each mode.
            n_arr (1D int xp.ndarray): :math:`n` values associated with each mode.
            *args (list, placeholder): Added for future flexibility.
            dt (double, optional): Time spacing between observations (inverse of sampling
                rate). Default is 10.0.
            **kwargs (dict, placeholder): Added for future flexibility.

        """

        init_len = len(t)
        num_teuk_modes = teuk_modes.shape[1]
        num_pts = self.num_pts

        length = init_len
        ninterps = self.ndim + 2 * num_teuk_modes  # 2 for re and im
        y_all = xp.zeros((ninterps, length))

        y_all[:num_teuk_modes] = teuk_modes.T.real
        y_all[num_teuk_modes : 2 * num_teuk_modes] = teuk_modes.T.imag

        y_all[-2] = Phi_phi
        y_all[-1] = Phi_r

        spline = CubicSplineInterpolant(t, y_all, use_gpu=self.use_gpu)

        try:
            h_t = t.get()
        except:
            h_t = t

        if not self.use_gpu:
            dev = 0
        else:
            dev = int(xp.cuda.runtime.getDevice())

        # the base class function __call__ will return the waveform
        self.get_waveform(
            self.waveform,
            spline.interp_array,
            m_arr,
            n_arr,
            init_len,
            num_pts,
            num_teuk_modes,
            ylms,
            dt,
            h_t,
            dev,
        )


def DirichletKernel(f, T, dt):
    num = np.sin(np.pi * f * T)
    denom = np.sin(Pi * f * dt)

    out = np.ones_like(f)
    inds = denom != 0
    out[inds] = num[inds] / denom[inds]
    return np.exp(-1j * np.pi * f * (T - dt)) * out


def get_DFT(A, n, dt, f, f0=0.0, phi0=0.0):
    T = n * dt
    return (
        A
        * (
            DirichletKernel(f - f0, T, dt) * np.exp(-1j * phi0)
            + DirichletKernel(f + f0, T, dt) * np.exp(1j * phi0)
        )
        / 2
    )


class TFInterpolatedModeSum(SummationBase, SchwarzschildEccentric, GPUModuleBase):
    """Create waveform by interpolating sparse trajectory.

    It interpolates all of the modes of interest and phases at sparse
    trajectories. Within the summation phase, the values are calculated using
    the interpolants and summed.

    This class can be run on GPUs and CPUs.

    """

    def __init__(self, *args, **kwargs):

        GPUModuleBase.__init__(self, *args, **kwargs)
        SchwarzschildEccentric.__init__(self, *args, **kwargs)
        SummationBase.__init__(self, *args, **kwargs)

        self.kwargs = kwargs

        if self.use_gpu:
            self.get_waveform = get_waveform_wrap

        else:
            self.get_waveform = get_waveform_wrap_cpu

    def attributes_InterpolatedModeSum(self):
        """
        attributes:
            get_waveform (func): CPU or GPU function for waveform creation.

        """

    @property
    def gpu_capability(self):
        """Confirms GPU capability"""
        return True

    @property
    def citation(self):
        return few_citation + few_software_citation

    def sum(
        self,
        t,
        teuk_modes,
        ylms,
        Phi_phi,
        Phi_r,
        m_arr,
        n_arr,
        M,
        p,
        e,
        *args,
        dt=10.0,
        num_left_right=5,
        **kwargs,
    ):
        """Interpolated summation function.

        This function interpolates the amplitude and phase information, and
        creates the final waveform with the combination of ylm values for each
        mode.

        args:
            t (1D double xp.ndarray): Array of t values.
            teuk_modes (2D double xp.array): Array of complex amplitudes.
                Shape: (len(t), num_teuk_modes).
            ylms (1D complex128 xp.ndarray): Array of ylm values for each mode,
                including m<0. Shape is (num of m==0,) + (num of m>0,)
                + (num of m<0). Number of m<0 and m>0 is the same, but they are
                ordered as (m==0 first then) m>0 then m<0.
            Phi_phi (1D double xp.ndarray): Array of azimuthal phase values
                (:math:`\Phi_\phi`).
            Phi_r (1D double xp.ndarray): Array of radial phase values
                 (:math:`\Phi_r`).
            m_arr (1D int xp.ndarray): :math:`m` values associated with each mode.
            n_arr (1D int xp.ndarray): :math:`n` values associated with each mode.
            *args (list, placeholder): Added for future flexibility.
            dt (double, optional): Time spacing between observations (inverse of sampling
                rate). Default is 10.0.
            **kwargs (dict, placeholder): Added for future flexibility.

        """

        init_len = len(t)
        num_teuk_modes = teuk_modes.shape[1]
        num_pts = self.num_pts

        length = init_len
        ninterps = (2 * self.ndim) + 2 * num_teuk_modes  # 2 for re and im
        y_all = self.xp.zeros((ninterps, length))

        y_all[:num_teuk_modes] = teuk_modes.T.real
        y_all[num_teuk_modes : 2 * num_teuk_modes] = teuk_modes.T.imag

        y_all[-2] = Phi_phi
        y_all[-1] = Phi_r

        try:
            p, e = p.get(), e.get()
        except AttributeError:
            pass

        Omega_phi, Omega_theta, Omega_r = get_fundamental_frequencies(
            0.0, p, e, np.zeros_like(e)
        )

        f_phi, f_r = (
            Omega_phi / (2 * np.pi * M * MTSUN_SI),
            Omega_r / (2 * np.pi * M * MTSUN_SI),
        )

        y_all[-4] = f_phi
        y_all[-3] = f_r

        spline = CubicSplineInterpolant(t, y_all, use_gpu=self.use_gpu)

        try:
            h_t = t.get()
        except:
            h_t = t

        self.t_new

        new_y_all = spline(self.t_new)

        Amps = (
            new_y_all[:num_teuk_modes]
            + 1j * new_y_all[num_teuk_modes : 2 * num_teuk_modes]
        )

        f_phi_new = new_y_all[-4]
        f_r_new = new_y_all[-3]
        Phi_phi_new = new_y_all[-2]
        Phi_r_new = new_y_all[-1]

        # TODO: (IMPORTANT) Need to think about complex numbers
        # TODO: (IMPORTANT) Need to think about negative frequencies
        # TODO: should there be an n in amplitude function for DFT calculation
        # for initial testing
        stft_hp = self.xp.zeros(
            (self.num_windows_for_waveform, self.num_frequencies),
            dtype=self.xp.complex128,
        )

        stft_hx = self.xp.zeros(
            (self.num_windows_for_waveform, self.num_frequencies),
            dtype=self.xp.complex128,
        )

        t_test = np.arange(0, self.num_per_window) * dt

        temp_sig = np.zeros(self.num_per_window, dtype=np.complex128)
        temp_y_all = spline(t_test)

        Phi_phi_test = temp_y_all[-2]
        Phi_r_test = temp_y_all[-1]

        wind = 0

        for i, (Amp_i, f_phi_i, f_r_i, Phi_phi_i, Phi_r_i) in enumerate(
            zip(Amps.T, f_phi_new, f_r_new, Phi_phi_new, Phi_r_new)
        ):
            for j, (m, n, ylm_plus_m, ylm_minus_m) in enumerate(
                zip(m_arr, n_arr, ylms[:num_teuk_modes], ylms[num_teuk_modes:])
            ):

                freq_mode = m * f_phi_i + n * f_r_i
                amp = Amp_i[j] * ylm_plus_m
                phi0 = m * Phi_phi_i + n * Phi_r_i

                if i == wind:
                    temp_sig += amp.real * np.cos(
                        m * Phi_phi_test + n * Phi_r_test
                    ) + amp.imag * np.sin(m * Phi_phi_test + n * Phi_r_test)

                # get (-m, -n) mode
                max_bin = int(np.abs(freq_mode) / self.df)

                if num_left_right < 0:
                    temp_freqs = self.bin_frequencies
                    inds = self.xp.arange(self.num_frequencies)

                else:
                    inds = np.arange(
                        max_bin - num_left_right, max_bin + num_left_right + 1
                    )
                    inds = inds[(inds >= 0) & (inds < self.num_frequencies - 1)]
                    temp_freqs = self.bin_frequencies[inds]

                # cos term
                temp_DFT_cos = get_DFT(
                    1.0, self.num_per_window, dt, temp_freqs, f0=freq_mode, phi0=phi0,
                )

                # sin term
                temp_DFT_sin = get_DFT(
                    1.0,
                    self.num_per_window,
                    dt,
                    temp_freqs,
                    f0=freq_mode,
                    phi0=phi0 - np.pi / 2.0,
                )

                stft_hp[i, inds] += np.conj(
                    amp.real * temp_DFT_cos + amp.imag * temp_DFT_sin
                )
                stft_hx[i, inds] += -(amp.imag * temp_DFT_cos - amp.real * temp_DFT_sin)

                if m > 0:
                    freq_mode = -m * f_phi_i - n * f_r_i
                    phi0 = -m * Phi_phi_i - n * Phi_r_i
                    amp = self.xp.conjugate(amp) * ylm_minus_m

                    if i == wind:
                        temp_sig += amp.real * np.cos(
                            -m * Phi_phi_test - n * Phi_r_test
                        ) - amp.imag * np.sin(-m * Phi_phi_test - n * Phi_r_test)

                    # get (-m, -n) mode
                    max_bin = int(np.abs(freq_mode) / self.df)

                    if num_left_right < 0:
                        temp_freqs = self.bin_frequencies
                        inds = self.xp.arange(self.num_frequencies)

                    else:
                        inds = np.arange(
                            max_bin - num_left_right, max_bin + num_left_right + 1
                        )
                        inds = inds[(inds >= 0) & (inds < self.num_frequencies - 1)]
                        temp_freqs = self.bin_frequencies[inds]

                    # cos term
                    temp_DFT_cos = get_DFT(
                        1.0,
                        self.num_per_window,
                        dt,
                        temp_freqs,
                        f0=freq_mode,
                        phi0=phi0,
                    )

                    # sin term
                    temp_DFT_sin = get_DFT(
                        1.0,
                        self.num_per_window,
                        dt,
                        temp_freqs,
                        f0=freq_mode,
                        phi0=phi0 - np.pi / 2.0,
                    )

                    # TODO: check conj
                    stft_hp[i, inds] += (
                        amp.real * temp_DFT_cos + amp.imag * temp_DFT_sin
                    )
                    stft_hx[i, inds] += -(
                        amp.imag * temp_DFT_cos - amp.real * temp_DFT_sin
                    )

        """
        import matplotlib.pyplot as plt

        fig, (ax0, ax1, ax2) = plt.subplots(1, 3)

        hp_f = self.xp.fft.rfft(temp_sig.real)

        ax0.loglog(np.abs(hp_f))
        ax0.loglog(np.abs(stft_hp[wind]), "--")

        ax1.semilogx(hp_f.real)
        ax1.semilogx(stft_hp[wind].real, "--")

        ax2.semilogx(hp_f.imag)
        ax2.semilogx(stft_hp[wind].imag, "--")

        overlap = np.dot(hp_f.conj(), stft_hp[wind]) / np.sqrt(
            np.dot(hp_f.conj(), hp_f) * np.dot(stft_hp[wind].conj(), stft_hp[wind])
        )
        print(overlap)
        plt.show()
        breakpoint()
        """

        self.waveform[0 : self.num_windows_for_waveform] = stft_hp
        self.waveform[1 : self.num_windows_for_waveform] = stft_hx
