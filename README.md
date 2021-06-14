# Overview

Distortion via an exponential function, with optional oversampling.

The equation is:

	y' = (1 - exp(-abs(data_abs * pregain) / plerp(shape0, shape1, data_abs, shape_exp))) * postgain

This is inspired by the capacitor discharge voltage equation.

`plerp` is a linear interpolation with exponentiation applied to the alpha value, to make the curve a little more interesting.

# Building

`Autoreconf` can be used to regenerated the configure script:

	autoreconf -i

When configuring, supply your buzztrax prefix to the configure script. I.e.:

	mkdir build
	cd build
	../configure --prefix ~/opt/buzztrax
	make

# Preferences

### Oversample

Oversample the input before applying distortion, to avoid aliasing. 8x oversampling is applied by default. The factor
is applied to the output rate of the effect, i.e. if downstream requests 44.1khz, then the oversampling rate is
`44.1khz * factor`.

# Properties

### Pregain

Gain in dB applied before distortion when the input signal is above zero (Positive Pregain) or below zero (Negative Pregain).

### Shape A/B

These parameters are called "shape" because they affect the distortion curve. These A and B values define two curves, and there is an interpolation applied between them as the input value increases.

### Shape exp

The exponent applied to the alpha value that interpolates between the two shapes.

### Symmetric?

When true the distortion is symmetric, meaning that the parameters from the "positive" set are applied regardless of
whether the signal is greater than or less than zero and "negative" parameters are ignored. Otherwise, the "negative"
parameters are used whenever the signal is less than zero.

Symmetric distortion produces odd-numbered harmonics, whereas asymmetric distortion produces both odd and even
harmonics.
