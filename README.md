# Overview

Distortion via a sigmoid function, with optional oversampling.

The equation is:

	y' = 1 / (1 + e**(-abs(y*pregain)**exponent * scale + bias)) * postgain

The function can be hard to visualise. Try putting this into Maxima:

	plot2d(1 / (1 + %e**(-abs(y)**0.25 * 20 + 17)),[y,-1,1]);

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

### Scale

Has the effect of making the duration of the sigmoid's transition longer or shorter.

### Bias

Offsets the sigmoid to the left or right. Good starting values are half of `Scale`.

### Exp

Exponentiation applied to the absolute value of the input signal. Has the effect of making the sigmoid's transition smoother (values < 0) or sharper.

### Symmetric?

When true the distortion is symmetric, meaning that the parameters from the "positive" set are applied regardless of
whether the signal is greater than or less than zero and "negative" parameters are ignored. Otherwise, the "negative"
parameters are used whenever the signal is less than zero.

Symmetric distortion produces odd-numbered harmonics, whereas asymmetric distortion produces both odd and even
harmonics.
