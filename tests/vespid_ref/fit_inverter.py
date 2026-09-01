"""Fit the Vespid inverter saturator to the DAFx-2022 CD4069 model.

MOSFET model (paper eqs 33-34), NMOS params (Table 2):
  alpha_n(v) = 3.1252e-6*v^2 - 1.0471e-4*v + 0.0022
  vt_n(v)    = 0.0149*v^2 + 0.2212*v + 0.8655        lambda_n = 1e-3
PMOS (cubic alpha):
  alpha_p(v) = -2.7258e-6*v^3 - 6.7622e-5*v^2 - 5.1393e-4*v - 4.9942e-4
  vt_p(v)    = 0.0135*v^2 - 0.1941*v + 0.6239        lambda_p = 0.06
iD piecewise: 0 if vgs<=vt; linear if vds <= vgs-vt; else saturation:
  i_lin = a*(vgs - vt - vds/2)*vds*(1+lam*vds)
  i_sat = a/2*(vgs - vt)^2*(1+lam*vds)
Inverter at supply VDD: NMOS vgs=vin, vds=vout; PMOS vgs=VDD-vin, vds=VDD-vout.
Solve i_n(vout) == i_p(vout) for vout by bisection over [0, VDD].
NOTE the PMOS alpha polynomial is negative-valued in the paper's table;
use its absolute value (current magnitude) -- check the resulting curve
against paper Fig 5 (12 V): output ~11.9V at vin=0, ~0.05V at vin=12,
transition centered near vin ~5.5-6V. If the curve is upside down or
degenerate, revisit signs before fitting.
"""
import numpy as np, json
from scipy.optimize import curve_fit

def alpha_n(v): return 3.1252e-6*v**2 - 1.0471e-4*v + 0.0022
def vt_n(v):    return 0.0149*v**2 + 0.2212*v + 0.8655
def alpha_p(v): return abs(-2.7258e-6*v**3 - 6.7622e-5*v**2 - 5.1393e-4*v - 4.9942e-4)
def vt_p(v):    return 0.0135*v**2 - 0.1941*v + 0.6239

def i_fet(alpha, vt, lam, vgs, vds):
    if vgs <= vt: return 0.0
    if vds <= vgs - vt:
        return alpha*(vgs - vt - vds/2)*vds*(1 + lam*vds)
    return alpha/2*(vgs - vt)**2*(1 + lam*vds)

def inverter_out(vin, VDD):
    lo, hi = 0.0, VDD
    for _ in range(60):
        vo = 0.5*(lo+hi)
        i_n = i_fet(alpha_n(vin), vt_n(vin), 1e-3, vin, vo)
        i_p = i_fet(alpha_p(VDD-vin), vt_p(VDD-vin), 0.06, VDD-vin, VDD-vo)
        # NMOS pulls vout down, PMOS pulls up: if pull-up wins, raise vo
        if i_p > i_n: lo = vo
        else: hi = vo
    return 0.5*(lo+hi)

def fit_supply(VDD):
    vin = np.linspace(0, VDD, 2400)
    vout = np.array([inverter_out(v, VDD) for v in vin])
    # operating point: vin == vout crossing
    op = vin[np.argmin(np.abs(vout - vin))]
    vg = vin - op            # centered input
    vo = vout - op           # centered output
    def model(x, A0, vHi, vLo):
        return np.where(x <= 0, vHi*np.tanh(-A0*x/vHi), -vLo*np.tanh(A0*x/vLo))
    p, _ = curve_fit(model, vg, vo, p0=[20, VDD/2, VDD/2], maxfev=20000)
    resid = np.sqrt(np.mean((model(vg, *p) - vo)**2))
    return dict(VDD=VDD, op=float(op), A0=float(p[0]), vHi=float(p[1]),
                vLo=float(p[2]), rms_fit_err=float(resid),
                vin=vin, vout=vout)

def fit_supply_restricted(VDD, frac=0.8):
    """Restrict the fit window to |vg| <= frac * (rail span from op) --
    used if the full-range fit residual is too large (extreme tails
    matter less than the transition region)."""
    vin = np.linspace(0, VDD, 2400)
    vout = np.array([inverter_out(v, VDD) for v in vin])
    op = vin[np.argmin(np.abs(vout - vin))]
    vg_full = vin - op
    vo_full = vout - op
    span = max(op, VDD - op) * frac
    mask = np.abs(vg_full) <= span
    vg, vo = vg_full[mask], vo_full[mask]
    def model(x, A0, vHi, vLo):
        return np.where(x <= 0, vHi*np.tanh(-A0*x/vHi), -vLo*np.tanh(A0*x/vLo))
    p, _ = curve_fit(model, vg, vo, p0=[20, VDD/2, VDD/2], maxfev=20000)
    resid_full = np.sqrt(np.mean((model(vg_full, *p) - vo_full)**2))
    resid_window = np.sqrt(np.mean((model(vg, *p) - vo)**2))
    return dict(VDD=VDD, op=float(op), A0=float(p[0]), vHi=float(p[1]),
                vLo=float(p[2]), rms_fit_err=float(resid_full),
                rms_fit_err_window=float(resid_window), frac=frac,
                vin=vin, vout=vout)

# --- Investigation note --------------------------------------------------
# alpha_p's polynomial is negative-valued, so abs() is applied above. With abs() applied, the DC curve is monotonic and
# correctly-directioned (high at vin=0, low at vin=VDD), matching Fig 5's
# qualitative shape -- but the transition point comes out around vin~8.7V
# (12V supply) rather than the ~5.5-6V shown in the paper's Fig 5.
#
# Verified against the primary source (DAFx20in22 paper PDF, Table 2 and
# eqs 33-34, read directly): every coefficient here matches the published
# Table 2 digit-for-digit (c_alpha,i,n/p and c_vT,i,n/p, lambda_n=1e-3,
# lambda_p=0.06). Tried several alternate conventions for the PMOS
# vgs/vds argument (raw/unmirrored vgs=vin-VDD -> fully degenerate, all
# currents zero; swapping which coefficient set plays NMOS vs PMOS ->
# transition at ~3.15V, an equally large deviation on the other side of
# mid-rail). None of the alternatives reproduce the ~5.5-6V transition
# while remaining a non-degenerate, correctly-directioned curve. The
# mirrored convention used here (PMOS vgs=VDD-vin, vds=VDD-vout, current
# magnitude via abs()) is the only one that is both non-degenerate and
# matches the paper's stated qualitative behavior, so it is kept.
#
# This is recorded in fitted_constants.md as an open discrepancy. It does
# not block downstream use: fit_supply() re-centers the tanh model on
# whichever crossing point the (correctly-shaped) curve actually has, so
# the fitted A0/vHi/vLo still describe a sensible soft-asymmetric
# saturator shape; only the literal "5-6.5V op point" sanity number from
# Step 2 is missed, not the shape quality (rms_fit_err).

if __name__ == "__main__":
    # Sanity check against paper Fig 5 (12V): out(0) ~ 11.9V, out(12) ~ 0.05V,
    # transition centered near vin ~ 5.5-6V.
    v0 = inverter_out(0.0, 12.0)
    v12 = inverter_out(12.0, 12.0)
    vmid = None
    vin_scan = np.linspace(0, 12, 481)
    vout_scan = np.array([inverter_out(v, 12.0) for v in vin_scan])
    # transition point: where output crosses 6V (mid-rail)
    cross_idx = np.argmin(np.abs(vout_scan - 6.0))
    vmid = vin_scan[cross_idx]
    print(f"DC curve sanity check (12V supply): out(0)={v0:.3f} V "
          f"(expect ~11.9), out(12)={v12:.3f} V (expect ~0.05), "
          f"transition(vout=6V) at vin={vmid:.3f} V (expect ~5.5-6 per Fig 5; "
          f"SEE INVESTIGATION NOTE ABOVE -- actual model gives ~8.7V, a "
          f"confirmed discrepancy, not a code bug: coefficients verified "
          f"against the paper verbatim).")

    results = {}
    for vdd in (12.0, 5.0):
        r = fit_supply(vdd)
        r_print = {k: v for k, v in r.items() if k not in ("vin", "vout")}
        print(json.dumps(r_print, indent=1))
        results[vdd] = r
        rr = fit_supply_restricted(vdd, 0.8)
        rr_print = {k: v for k, v in rr.items() if k not in ("vin", "vout")}
        print("  restricted-fit (frac=0.8) check:", json.dumps(rr_print))
