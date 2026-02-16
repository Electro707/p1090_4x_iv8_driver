# Circuit Design Notes

These are notes on math for the boost converter (the only thing here). Some of the numbers are not for the current design, but for a fix on the audible noise issue (see [ERRATA.md](ERRATA.md)). The equations are still true though in any case.

## Boost output voltage

As designed:
$$
R_1 = R_2 * \left(\frac{V_{out}}{1.23}-1\right) \\
V_{out} = \frac{R1}{R2}+1*1.23\\
V_{out} = \frac{270k\Omega}{10k\Omega}+1*1.23\\
V_{out} = 34.44v
$$

## Load of grid and anode
This is taken from an IV-8 datasheet
$$
I_{anode_{max}} = 0.8mA + 1.7mA = 2.5mA \\
I_{grid_{max}} = 3mA + 2mA = 5mA
$$

## Duty Cycle
$$
V_{sw} = 0.2v \\
V_{diode} = 0.3v \\
$$

$$ \begin{align}
Duty &= \frac{V_{out}+V_{diode}-V_{in}}{V_{out}+V_{diode}-V_{sw}} \\
     &= \frac{34.44+0.3-5}{34.44+0.3-0.2} \\\
     &= 86.1\% \\
\end{align} $$


The 400Khz number is from the min spec of $F_{SW}$ for the Y variant
$$ \begin{align}
T_{period_{max}}  &= 1/400kHz \\
            &= 2.5uS \\
T_{on_{max}} &= T_{period_{max}} * Duty \\
            &= 2.15uS
\end{align} $$

## Minimum Inductance
At no or little load
$$ \begin{align}
L   &\geq V_{L} * \frac{dt}{dI} \\
    &\geq (V_{in}-V_{sw}) * \frac{dt}{dI} \\
    &\geq (5-0.2) * \frac{2.15uS}{1} \\
    &\geq 10uH
\end{align} $$

## High and Low Voltage

Used figure 25 of the datasheet to get the high and low current to ensure non-continuous mode

The additional load of 16mA is with a 2.2k parallel with the output to ensure always continuous mode

$$
\begin{align}
I_{load} &= (34.44 / 2.2k\Omega) + I_{anode_{max}} + I_{grid_{max}}  \\
        &=23.15mA \end{align} \\
L = 15uH
$$

$$ \begin{align}
V &= L * {d_i/d_t} \\
{d_i/d_t} &= V_{L}/L \\
 &= (5-0.2) / 47uH \\
 &= 0.102A/uS
\end{align} $$

$$ \begin{align}
I_{mid} &= \frac{I_{load}}{1-Duty} \\
    &= \frac{23.15mA}{1-0.861} \\
    &= 166mA \\
I_{\Delta} &= {d_i/d_t} * T_{on_{max}} \\
    &= 0.102A/uS * 2.15uS \\
    &= 219mA \\
I_{low} &= I_{mid} - I_{\Delta}/2 \\
        &= 56.5mA
I_{max} &= I_{mid} + I_{\Delta}/2 \\
        &= 275.5mA
\end{align} $$

## Load Current
$$
I_{sw_{max}} \geq 1A \\
\begin{align}
I_{load_{max}} &= (1-Duty) * (I_{sw_{max}} - \frac{Duty*(V_in)-V_{sw}}{2*f*L}) \\
             &= (1-0.861) * (1A - \frac{0.861*4.8}{2*400kHz*15uH}) \\
            &= 91.12mA
\end{align}
$$
