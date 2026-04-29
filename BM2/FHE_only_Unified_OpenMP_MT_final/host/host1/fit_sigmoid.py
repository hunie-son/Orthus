#!/usr/bin/env python3
import math, random

# 1) Your target sigmoid
def sigmoid(x):
    return 1.0 / (1.0 + math.exp(-x))

# 2) Evaluate a polynomial a[0] + a[1]*x + ... + a[d]*x^d
def poly(a, x):
    r = 0.0
    # Horner’s method:
    for coef in reversed(a):
        r = r * x + coef
    return r

# 3) Max absolute error over a grid
def max_error(a, xs):
    m = 0.0
    for x in xs:
        e = abs(poly(a, x) - sigmoid(x))
        if e > m: m = e
    return m

def main():
    deg = 7
    # 4) Initial guess: you can seed with your old coeffs or zeros
    a = [0.0] * (deg+1)
    # example seed (your last known good):
    a[0]=0.5;   a[1]=0.21788;  a[3]=-0.0083818
    a[5]=0.00017207; a[7]=-0.0000012523

    # 5) Build your evaluation grid
    N = 500  # grid points each side
    xs = [ -4 + 8*i/N for i in range(N+1) ]  # from -4 to +4

    best = max_error(a, xs)
    print(f"Init max error = {best:.6g}")

    # 6) Simple hill‐climbing / random‐tweak loop
    for it in range(200000):
        i = random.randrange(deg+1)
        old = a[i]
        # step size proportional to current best error
        step = best * 0.1
        a[i] += random.uniform(-step, step)
        err = max_error(a, xs)
        if err < best:
            best = err
            print(f"it={it:6d} new best err={best:.6g}  coeffs={['%.8g'%c for c in a]}")
        else:
            a[i] = old

    print("\nFinal coeffs (degree 7):")
    for i,coef in enumerate(a):
        print(f"  a[{i}] = {coef:.10g}")
    print("Final max error =", best)

if __name__=="__main__":
    main()

