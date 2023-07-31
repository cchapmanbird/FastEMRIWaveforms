# Online mode selection for FastEMRIWaveforms Packages

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


import numpy as np

import os

from few.utils.citations import *
from few.utils.utility import get_fundamental_frequencies
from few.utils.constants import *
from few.utils.baseclasses import ParallelModuleBase

# check for cupy
try:
    import cupy as cp

except (ImportError, ModuleNotFoundError) as e:
    import numpy as np

dir_path = os.path.dirname(os.path.realpath(__file__))

class ModeSelector(ParallelModuleBase):
    """Filter teukolsky amplitudes based on power contribution.

    This module takes teukolsky modes, combines them with their associated ylms,
    and determines the power contribution from each mode. It then filters the
    modes bases on the fractional accuracy on the total power (eps) parameter.
    Additionally, if a sensitivity curve is provided, the mode power is also
    weighted according to the PSD of the sensitivity.

    The mode filtering is a major contributing factor to the speed of these
    waveforms as it removes large numbers of useles modes from the final
    summation calculation.

    Be careful as this is built based on the construction that input mode arrays
    will in order of :math:`m=0`, :math:`m>0`, and then :math:`m<0`.

    args:
        m0mask (1D bool xp.ndarray): This mask highlights which modes have
            :math:`m=0`. Value is False if :math:`m=0`, True if not.
            This only includes :math:`m\geq0`.
        sensitivity_fn (object, optional): Sensitivity curve function that takes
            a frequency (Hz) array as input and returns the Power Spectral Density (PSD)
            of the sensitivity curve. Default is None. If this is not none, this
            sennsitivity is used to weight the mode values when determining which
            modes to keep. **Note**: if the sensitivity function is provided,
            and GPUs are used, then this function must accept CuPy arrays as input.
        **kwargs (dict, optional): Keyword arguments for the base classes:
            :class:`few.utils.baseclasses.ParallelModuleBase`.
            Default is {}.

    """

    def __init__(self, m0mask, sensitivity_fn=None, **kwargs):
        ParallelModuleBase.__init__(self, **kwargs)

        if self.use_gpu:
            xp = cp
        else:
            xp = np

        # store information releated to m values
        # the order is m = 0, m > 0, m < 0
        self.m0mask = m0mask
        self.num_m_zero_up = len(m0mask)
        self.num_m_1_up = len(xp.arange(len(m0mask))[m0mask])
        self.num_m0 = len(xp.arange(len(m0mask))[~m0mask])

        self.sensitivity_fn = sensitivity_fn

    @property
    def gpu_capability(self):
        """Confirms GPU capability"""
        return True

    def attributes_ModeSelector(self):
        """
        attributes:
            xp: cupy or numpy depending on GPU usage.
            num_m_zero_up (int): Number of modes with :math:`m\geq0`.
            num_m_1_up (int): Number of modes with :math:`m\geq1`.
            num_m0 (int): Number of modes with :math:`m=0`.
            sensitivity_fn (object): sensitivity generating function for power-weighting.

        """
        pass

    @property
    def citation(self):
        """Return citations related to this class."""
        return larger_few_citation + few_citation + few_software_citation

    def __call__(self, teuk_modes, ylms, modeinds, fund_freq_args=None, eps=1e-5):
        """Call to sort and filer teukolsky modes.

        This is the call function that takes the teukolsky modes, ylms,
        mode indices and fractional accuracy of the total power and returns
        filtered teukolsky modes and ylms.

        args:
            teuk_modes (2D complex128 xp.ndarray): Complex teukolsky amplitudes
                from the amplitude modules.
                Shape: (number of trajectory points, number of modes).
            ylms (1D complex128 xp.ndarray): Array of ylm values for each mode,
                including m<0. Shape is (num of m==0,) + (num of m>0,)
                + (num of m<0). Number of m<0 and m>0 is the same, but they are
                ordered as (m==0) first then m>0 then m<0.
            modeinds (list of int xp.ndarrays): List containing the mode index arrays. If in an
                equatorial model, need :math:`(l,m,n)` arrays. If generic,
                :math:`(l,m,k,n)` arrays. e.g. [l_arr, m_arr, n_arr].
            fund_freq_args (tuple, optional): Args necessary to determine
                fundamental frequencies along trajectory. The tuple will represent
                :math:`(M, a, e, p, \cos\iota)` where the large black hole mass (:math:`M`)
                and spin (:math:`a`) are scalar and the other three quantities are xp.ndarrays.
                This must be provided if sensitivity weighting is used. Default is None.
            eps (double, optional): Fractional accuracy of the total power used
                to determine the contributing modes. Lowering this value will
                calculate more modes slower the waveform down, but generally
                improving accuracy. Increasing this value removes modes from
                consideration and can have a considerable affect on the speed of
                the waveform, albeit at the cost of some accuracy (usually an
                acceptable loss). Default that gives good mismatch qualities is
                1e-5.

        """

        if self.use_gpu:
            xp = cp
        else:
            xp = np

        # get the power contribution of each mode including m < 0
        power = (
            xp.abs(
                xp.concatenate(
                    [teuk_modes, xp.conj(teuk_modes[:, self.m0mask])], axis=1
                )
                * ylms
            )
            ** 2
        )

        # if noise weighting
        if self.sensitivity_fn is not None:
            if fund_freq_args is None:
                raise ValueError(
                    "If sensitivity weighting is desired, the fund_freq_args kwarg must be provided."
                )

            M = fund_freq_args[0]
            Msec = M * MTSUN_SI

            # get dimensionless fundamental frequency
            OmegaPhi, OmegaTheta, OmegaR = get_fundamental_frequencies(
                *fund_freq_args[1:]
            )

            # get frequencies in Hz
            f_Phi, f_omega, f_r = OmegaPhi, OmegaTheta, OmegaR = (
                xp.asarray(OmegaPhi) / (Msec * 2 * PI),
                xp.asarray(OmegaTheta) / (Msec * 2 * PI),
                xp.asarray(OmegaR) / (Msec * 2 * PI),
            )

            # TODO: update when in kerr
            freqs = (
                modeinds[1][xp.newaxis, :] * f_Phi[:, xp.newaxis]
                + modeinds[2][xp.newaxis, :] * f_r[:, xp.newaxis]
            )

            freqs_shape = freqs.shape

            # make all frequencies positive
            freqs_in = xp.abs(freqs)
            PSD = self.sensitivity_fn(freqs_in.flatten()).reshape(freqs_shape)

            # weight by PSD
            power /= PSD

        # sort the power for a cumulative summation
        inds_sort = xp.argsort(power, axis=1)[:, ::-1]
        power = xp.sort(power, axis=1)[:, ::-1]
        cumsum = xp.cumsum(power, axis=1)

        # initialize and indices array for keeping modes
        inds_keep = xp.full(cumsum.shape, True)

        # keep modes that add to within the fractional power (1 - eps)
        inds_keep[:, 1:] = cumsum[:, :-1] < cumsum[:, -1][:, xp.newaxis] * (1 - eps)

        # finds indices of each mode to be kept
        temp = inds_sort[inds_keep]

        # adjust the index arrays to make -m indices equal to +m indices
        # if +m or -m contributes, we keep both because of structure of CUDA kernel
        temp = temp * (temp < self.num_m_zero_up) + (temp - self.num_m_1_up) * (
            temp >= self.num_m_zero_up
        )

        # if +m or -m contributes, we keep both because of structure of CUDA kernel
        keep_modes = xp.unique(temp)

        # set ylms

        # adust temp arrays specific to ylm setup
        temp2 = keep_modes * (keep_modes < self.num_m0) + (
            keep_modes + self.num_m_1_up
        ) * (keep_modes >= self.num_m0)

        # ylm duplicates the m = 0 unlike teuk_modes
        ylmkeep = xp.concatenate([keep_modes, temp2])

        # setup up teuk mode and ylm returns
        out1 = (teuk_modes[:, keep_modes], ylms[ylmkeep])

        # setup up mode values that have been kept
        out2 = tuple([arr[keep_modes] for arr in modeinds])

        return out1 + out2

class NeuralModeSelector(ParallelModuleBase):
    """Filter teukolsky amplitudes based on power contribution.

    This module uses a combination of a pre-computed mask and a feed-forward neural
    network to predict the mode content of the waveform given its parameters. Therefore,
    the results will vary compared to manually computing the mode selection, but it should be
    very accurate especially for the stronger modes.

    The mode filtering is a major contributing factor to the speed of these
    waveforms as it removes large numbers of useles modes from the final
    summation calculation.

    args:
        mode_ind_list (list): A list of all sets of mode indices, in order. The pre-computed mask
            will be applied to this list, and the result will be further reduced for output upon
            evaluation of the mode selector.
        **kwargs (dict, optional): Keyword arguments for the base classes:
            :class:`few.utils.baseclasses.ParallelModuleBase`.
            Default is {}.
    """

    def __init__(self, mode_ind_list, **kwargs):
        ParallelModuleBase.__init__(self, **kwargs)

        self.precomputed_mask = np.load(dir_path+'/modeselector_files/precomputed_mode_mask.npy')
        self.base_mode_list = mode_ind_list[self.precomputed_mask]

        # we set the pytorch device here for use with the neural network
        if self.use_gpu:
            xp = cp
            self.device=f"cuda:{cp.cuda.runtime.getDevice()}"
        else:
            xp = np
            self.device="cpu"
        # if torch doesn't import properly, raise
        import torch
        # if torch wasn't installed with GPU capability, raise
        if self.use_gpu and not torch.cuda.is_available():
            raise RuntimeError("pytorch has not been installed with CUDA capability. Fix installation or set use_gpu=False.")
        
        try:
            self.load_model(dir_path+"modeselector_files/mode_model.pt")  # TODO fix path
        except FileNotFoundError:
            raise FileNotFoundError("Model file not found. Check it's in the directory.")

    @property
    def gpu_capability(self):
        """Confirms GPU capability"""
        return True

    def attributes_ModeSelector(self):
        """
        attributes:
            xp: cupy or numpy depending on GPU usage.

        """
        pass

    @property
    def citation(self):
        """Return citations related to this class."""
        return larger_few_citation + few_citation + few_software_citation

    def load_model(self, fp):
        self.model = torch.load(fp)
        self.model.to(self.device)
        self.model.eval()
    
    def __call__(self, M, mu, p0, e0, theta, phi, T, eps, threshold=0.5):
        """Call to predict the mode content of the waveform.

        This is the call function that takes the waveform parameters, applies a 
        precomputed mask and then evaluates the remaining mode content with a
        neural network classifier. 

        args:
            M (double): Mass of larger black hole in solar masses.
            mu (double): Mass of compact object in solar masses.
            p0 (double): Initial semilatus rectum (Must be greater than
                the separatrix at the the given e0 and x0).
                See documentation for more information on :math:`p_0<10`.
            e0 (double): Initial eccentricity.
            theta (double): Polar viewing angle.
            phi (double): Azimuthal viewing angle.
            T (double): Duration of waveform in years.
            eps (double): Mode selection threshold power.
            threshold (double): The network threshold value for mode retention. Decrease to keep more modes,
                minimising missed modes but slowing down the waveform computation. Defaults to 0.5 (the optimal value for accuracy).
        """

        #wrap angles to training bounds
        phi = phi % (2*np.pi)
        theta = theta % (np.pi) - np.pi

        # no rescaling for now, just some functions.
        inps = torch.as_tensor([xp.log(M), mu, p0, e0, T, theta, phi, xp.log10(eps)], device=self.device)

        with torch.inference_mode():
            mode_predictions = self.model(inps)
            # mode_predictions[mode_predictions > threshold] = 1
            # mode_predictions[mode_predictions < threshold] = 0
            keep_inds = torch.where(mode_predictions > threshold).int().cpu().numpy()
        selected_modes = [self.base_mode_list[ind] for ind in keep_inds]
        return selected_modes
