#pragma once

#include <complex>

namespace biem::dsp {

// Complex baseband I/Q sample. float (not double) throughout the DSP chain
// - plenty of precision for 8-bit-ADC-derived RTL-SDR data, and keeps
// buffers/cache footprint small for real-time processing.
using IqSample = std::complex<float>;

} // namespace biem::dsp
