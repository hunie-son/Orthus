import numpy as np
from numpy.polynomial import Chebyshev

# Define the target function: sigmoid
def sigmoid(x):
    return 1 / (1 + np.exp(-x))

# Sample points on the interval [-8, 8]
x = np.linspace(-8, 8, 4001)
y = sigmoid(x)

# Compute a degree-7 Chebyshev (minimax-like) fit on [-8,8]
cheb = Chebyshev.fit(x, y, deg=7, domain=[-8, 8])

# Convert to power basis to get coefficients a0 + a1 x + ... + a7 x^7
power_poly = cheb.convert(kind=np.polynomial.Polynomial)
coeffs = power_poly.coef

# Display the coefficients
import pandas as pd
df = pd.DataFrame({
    'Degree': np.arange(len(coeffs)),
    'Coeff': coeffs
})
import ace_tools as tools; tools.display_dataframe_to_user(name="Chebyshev Approximation Coeffs", dataframe=df)

