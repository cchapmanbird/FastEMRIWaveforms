#include "stdio.h"
#include "math.h"
#include "global.h"
#include <stdexcept>
#include "Utility.hh"

#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_ellint.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_roots.h>
#include "Python.h"

#ifdef __USE_OMP__
#include "omp.h"
#endif

int sanity_check(double a, double p, double e, double Y){
    int res = 0;
    
    if (p<0.0) return 1;
    if ((e>1.0)||(e<0.0)) return 1;
    if ((Y>1.0)||(Y<-1.0)) return 1;
    if ((a>1.0)||(a<0.0)) return 1;
    
    if (res==1){
        printf("a, p, e, Y = %f %f %f %f ",a, p, e, Y);
        // throw std::invalid_argument( "Sanity check wrong");
    }
    return res;
}

// Define elliptic integrals that use Mathematica's conventions
double EllipticK(double k){
    gsl_sf_result result;
    int status = gsl_sf_ellint_Kcomp_e(sqrt(k), GSL_PREC_DOUBLE, &result);
    if (status != GSL_SUCCESS)
    {
        char str[1000];
        sprintf(str, "EllipticK failed with argument k: %e", k);
        throw std::invalid_argument(str);
    }
    return result.val;
}

double EllipticF(double phi, double k){
    gsl_sf_result result;
    int status = gsl_sf_ellint_F_e(phi, sqrt(k), GSL_PREC_DOUBLE, &result);
    if (status != GSL_SUCCESS)
    {
        char str[1000];
        sprintf(str, "EllipticF failed with arguments phi:%e k: %e", phi, k);
        throw std::invalid_argument(str);
    }
    return result.val;
}

double EllipticE(double k){
    gsl_sf_result result;
    int status = gsl_sf_ellint_Ecomp_e(sqrt(k), GSL_PREC_DOUBLE, &result);
    if (status != GSL_SUCCESS)
    {
        char str[1000];
        sprintf(str, "EllipticE failed with argument k: %e", k);
        throw std::invalid_argument(str);
    }
    return result.val;
}

double EllipticEIncomp(double phi, double k){
    gsl_sf_result result;
    int status = gsl_sf_ellint_E_e(phi, sqrt(k), GSL_PREC_DOUBLE, &result);
    if (status != GSL_SUCCESS)
    {
        char str[1000];
        sprintf(str, "EllipticEIncomp failed with argument k: %e", k);
        throw std::invalid_argument(str);
    }
    return result.val;
}

double EllipticPi(double n, double k){
    gsl_sf_result result;
    int status = gsl_sf_ellint_Pcomp_e(sqrt(k), -n, GSL_PREC_DOUBLE, &result);
    if (status != GSL_SUCCESS)
    {
        char str[1000];
        printf("55: %e\n", k);
        sprintf(str, "EllipticPi failed with arguments (k,n): (%e,%e)", k, n);
        throw std::invalid_argument(str);
    }
    return result.val;
}

double EllipticPiIncomp(double n, double phi, double k){
    gsl_sf_result result;
    int status = gsl_sf_ellint_P_e(phi, sqrt(k), -n, GSL_PREC_DOUBLE, &result);
    if (status != GSL_SUCCESS)
    {
        char str[1000];
        sprintf(str, "EllipticPiIncomp failed with argument k: %e", k);
        throw std::invalid_argument(str);
    }
    return result.val;
}


double CapitalDelta(double r, double a)
{
    return pow(r, 2.) - 2. * r + pow(a, 2.);
}

double f(double r, double a, double zm)
{
    return pow(r, 4) + pow(a, 2) * (r * (r + 2) + pow(zm, 2) * CapitalDelta(r, a));
}

double g(double r, double a, double zm)
{
    return 2 * a * r;
}

double h(double r, double a, double zm)
{
    return r * (r - 2) + pow(zm, 2)/(1 - pow(zm, 2)) * CapitalDelta(r, a);
}

double d(double r, double a, double zm)
{
    return (pow(r, 2) + pow(a, 2) * pow(zm, 2)) * CapitalDelta(r, a);
}

double KerrGeoEnergy(double a, double p, double e, double x)
{

	double r1 = p/(1.-e);
	double r2 = p/(1.+e);

	double zm = sqrt(1.-pow(x, 2.));


    double Kappa    = d(r1, a, zm) * h(r2, a, zm) - h(r1, a, zm) * d(r2, a, zm);
    double Epsilon  = d(r1, a, zm) * g(r2, a, zm) - g(r1, a, zm) * d(r2, a, zm);
    double Rho      = f(r1, a, zm) * h(r2, a, zm) - h(r1, a, zm) * f(r2, a, zm);
    double Eta      = f(r1, a, zm) * g(r2, a, zm) - g(r1, a, zm) * f(r2, a, zm);
    double Sigma    = g(r1, a, zm) * h(r2, a, zm) - h(r1, a, zm) * g(r2, a, zm);

	return sqrt((Kappa * Rho + 2 * Epsilon * Sigma - x * 2 * sqrt(Sigma * (Sigma * pow(Epsilon, 2) + Rho * Epsilon * Kappa - Eta * pow(Kappa,2))/pow(x, 2)))/(pow(Rho, 2) + 4 * Eta * Sigma));
}

double KerrGeoAngularMomentum(double a, double p, double e, double x, double En)
{
    double r1 = p/(1-e);

	double zm = sqrt(1-pow(x,2));

    return (-En * g(r1, a, zm) + x * sqrt((-d(r1, a, zm) * h(r1, a, zm) + pow(En, 2) * (pow(g(r1, a, zm), 2) + f(r1, a, zm) * h(r1, a, zm)))/pow(x, 2)))/h(r1, a, zm);
}

double KerrGeoCarterConstant(double a, double p, double e, double x, double En, double L)
{
    double zm = sqrt(1-pow(x,2));

	return pow(zm, 2) * (pow(a, 2) * (1 - pow(En, 2)) + pow(L, 2)/(1 - pow(zm, 2)));
}

void KerrGeoConstantsOfMotion(double* E_out, double* L_out, double* Q_out, double a, double p, double e, double x)
{
    *E_out = KerrGeoEnergy(a, p , e, x);
    *L_out = KerrGeoAngularMomentum(a, p, e, x, *E_out);
    *Q_out = KerrGeoCarterConstant(a, p, e, x, *E_out, *L_out);
}

void KerrGeoConstantsOfMotionVectorized(double* E_out, double* L_out, double* Q_out, double* a, double* p, double* e, double* x, int n)
{
    #ifdef __USE_OMP__
    #pragma omp parallel for
    #endif
    for (int i = 0; i < n; i += 1)
    {
        KerrGeoConstantsOfMotion(&E_out[i], &L_out[i], &Q_out[i], a[i], p[i], e[i], x[i]);
    }
}

void KerrGeoRadialRoots(double* r1_, double*r2_, double* r3_, double* r4_, double a, double p, double e, double x, double En, double Q)
{
    double M = 1.0;
    double r1 = p / (1 - e);
    double r2 = p /(1+e);
    double AplusB = (2 * M)/(1-pow(En, 2)) - (r1 + r2);
    double AB = (pow(a, 2) * Q)/((1-pow(En, 2)) * r1 *  r2);
    double r3 = (AplusB+sqrt(pow(AplusB, 2) - 4 * AB))/2;
    double r4 = AB/r3;

    *r1_ = r1;
    *r2_ = r2;
    *r3_ = r3;
    *r4_ = r4;
}


void KerrGeoMinoFrequencies(double* CapitalGamma_, double* CapitalUpsilonPhi_, double* CapitalUpsilonTheta_, double* CapitalUpsilonr_,
                              double a, double p, double e, double x)
{
    double M = 1.0;

    double En = KerrGeoEnergy(a, p, e, x);
    double L = KerrGeoAngularMomentum(a, p, e, x, En);
    double Q = KerrGeoCarterConstant(a, p, e, x, En, L);

    // get radial roots
    double r1, r2, r3, r4;
    KerrGeoRadialRoots(&r1, &r2, &r3, &r4, a, p, e, x, En, Q);

    double Epsilon0 = pow(a, 2) * (1 - pow(En, 2))/pow(L, 2);
    double zm = 1 - pow(x, 2);
    double a2zp =(pow(L, 2) + pow(a, 2) * (-1 + pow(En, 2)) * (-1 + zm))/( (-1 + pow(En, 2)) * (-1 + zm));

    double Epsilon0zp = -((pow(L, 2)+ pow(a, 2) * (-1 + pow(En, 2)) * (-1 + zm))/(pow(L, 2) * (-1 + zm)));

    double zmOverZp = zm/((pow(L, 2)+ pow(a, 2) * (-1 + pow(En, 2)) * (-1 + zm))/(pow(a, 2) * (-1 + pow(En, 2)) * (-1 + zm)));

    double kr = sqrt((r1-r2)/(r1-r3) * (r3-r4)/(r2-r4)); //(*Eq.(13)*)
    double kTheta = sqrt(zmOverZp); //(*Eq.(13)*)
    double CapitalUpsilonr = (M_PI * sqrt((1 - pow(En, 2)) * (r1-r3) * (r2-r4)))/(2 * EllipticK(pow(kr, 2))); //(*Eq.(15)*)
    double CapitalUpsilonTheta=(M_PI * L * sqrt(Epsilon0zp))/(2 * EllipticK(pow(kTheta, 2))); //(*Eq.(15)*)

    double rp = M + sqrt(pow(M, 2) - pow(a, 2));
    double rm = M - sqrt(pow(M, 2) - pow(a, 2));

    double hr = (r1 - r2)/(r1 - r3);
    double hp = ((r1 - r2) * (r3 - rp))/((r1 - r3) * (r2 - rp));
    double hm = ((r1 - r2) * (r3 - rm))/((r1 - r3) * (r2 - rm));

    // (*Eq. (21)*)
    double CapitalUpsilonPhi = (2 * CapitalUpsilonTheta)/(M_PI * sqrt(Epsilon0zp)) * EllipticPi(zm, pow(kTheta, 2)) + (2 * a * CapitalUpsilonr)/(M_PI * (rp - rm) * sqrt((1 - pow(En, 2)) * (r1 - r3) * (r2 - r4))) * ((2 * M * En * rp - a * L)/(r3 - rp) * (EllipticK(pow(kr, 2)) - (r2 - r3)/(r2 - rp) * EllipticPi(hp, pow(kr, 2))) - (2 * M * En * rm - a * L)/(r3 - rm) * (EllipticK(pow(kr, 2)) - (r2 - r3)/(r2 - rm) * EllipticPi(hm, pow(kr,2))));

    double CapitalGamma = 4 * pow(M, 2) * En + (2 * a2zp * En * CapitalUpsilonTheta)/(M_PI * L * sqrt(Epsilon0zp)) * (EllipticK(pow(kTheta, 2)) -  EllipticE(pow(kTheta, 2))) + (2 * CapitalUpsilonr)/(M_PI * sqrt((1 - pow(En, 2)) * (r1 - r3) * (r2 - r4))) * (En/2 * ((r3 * (r1 + r2 + r3) - r1 * r2) * EllipticK(pow(kr, 2)) + (r2 - r3) * (r1 + r2 + r3 + r4) * EllipticPi(hr,pow(kr, 2)) + (r1 - r3) * (r2 - r4) * EllipticE(pow(kr, 2))) + 2 * M * En * (r3 * EllipticK(pow(kr, 2)) + (r2 - r3) * EllipticPi(hr,pow(kr, 2))) + (2* M)/(rp - rm) * (((4 * pow(M, 2) * En - a * L) * rp - 2 * M * pow(a, 2) * En)/(r3 - rp) * (EllipticK(pow(kr, 2)) - (r2 - r3)/(r2 - rp) * EllipticPi(hp, pow(kr, 2))) - ((4 * pow(M, 2) * En - a * L) * rm - 2 * M * pow(a, 2) * En)/(r3 - rm) * (EllipticK(pow(kr, 2)) - (r2 - r3)/(r2 - rm) * EllipticPi(hm,pow(kr, 2)))));

    *CapitalGamma_ = CapitalGamma;
    *CapitalUpsilonPhi_ = CapitalUpsilonPhi;
    *CapitalUpsilonTheta_ = CapitalUpsilonTheta;
    *CapitalUpsilonr_ = CapitalUpsilonr;
}

void KerrEqGeoMinoFrequencies(double* CapitalGamma_, double* CapitalUpsilonPhi_, double* CapitalUpsilonTheta_, double* CapitalUpsilonr_,
                              double a, double p, double e, double x)
{
    double CapitalUpsilonr = Sqrt((p*(-2*Power(a,2) + 6*a*Sqrt(p) + (-5 + p)*p + (Power(a - Sqrt(p),2)*(Power(a,2) - 4*a*Sqrt(p) - (-4 + p)*p))/abs(Power(a,2) - 4*a*Sqrt(p) - (-4 + p)*p)))/(2*a*Sqrt(p) + (-3 + p)*p));
    double CapitalUpsilonTheta = abs((Power(p,0.25)*Sqrt(3*Power(a,2) - 4*a*Sqrt(p) + Power(p,2)))/Sqrt(2*a + (-3 + p)*Sqrt(p)));
    double CapitalUpsilonPhi = Power(p,1.25)/Sqrt(2*a + (-3 + p)*Sqrt(p));
    double CapitalGamma = (Power(p,1.25)*(a + Power(p,1.5)))/Sqrt(2*a + (-3 + p)*Sqrt(p));

    *CapitalGamma_ = CapitalGamma;
    *CapitalUpsilonPhi_ = CapitalUpsilonPhi;
    *CapitalUpsilonTheta_ = CapitalUpsilonTheta;
    *CapitalUpsilonr_ = CapitalUpsilonr;
}


void KerrGeoCoordinateFrequencies(double* OmegaPhi_, double* OmegaTheta_, double* OmegaR_,
                              double a, double p, double e, double x)
{
    // printf("here p e %f %f %f %f \n", a, p, e, x);
    double CapitalGamma, CapitalUpsilonPhi, CapitalUpsilonTheta, CapitalUpsilonR;
    if (abs(x)==1.0){
        KerrEqGeoMinoFrequencies(&CapitalGamma, &CapitalUpsilonPhi, &CapitalUpsilonTheta, &CapitalUpsilonR,
                                  a, p, e, x);
    }
    else{
        KerrGeoMinoFrequencies(&CapitalGamma, &CapitalUpsilonPhi, &CapitalUpsilonTheta, &CapitalUpsilonR,
                                  a, p, e, x);
    }
    

    if ((CapitalUpsilonPhi!=CapitalUpsilonPhi) || (CapitalGamma!=CapitalGamma) || (CapitalUpsilonR!=CapitalUpsilonR) ){
        printf("(a, p, e, x) = (%f , %f , %f , %f) \n", a, p, e, x);
        throw std::invalid_argument("Nan in fundamental frequencies");
    }
    // printf("here xhi %f %f\n", CapitalUpsilonPhi, CapitalGamma);
    *OmegaPhi_ = CapitalUpsilonPhi / CapitalGamma;
    *OmegaTheta_ = CapitalUpsilonTheta / CapitalGamma;
    *OmegaR_ = CapitalUpsilonR / CapitalGamma;

}

void SchwarzschildGeoCoordinateFrequencies(double* OmegaPhi, double* OmegaR, double p, double e)
{
    // Need to evaluate 4 different elliptic integrals here. Cache them first to avoid repeated calls.
	double EllipE 	= EllipticE(4*e/(p-6.0+2*e));
	double EllipK 	= EllipticK(4*e/(p-6.0+2*e));;
	double EllipPi1 = EllipticPi(16*e/(12.0 + 8*e - 4*e*e - 8*p + p*p), 4*e/(p-6.0+2*e));
	double EllipPi2 = EllipticPi(2*e*(p-4)/((1.0+e)*(p-6.0+2*e)), 4*e/(p-6.0+2*e));

    *OmegaPhi = (2*Power(p,1.5))/(Sqrt(-4*Power(e,2) + Power(-2 + p,2))*(8 + ((-2*EllipPi2*(6 + 2*e - p)*(3 + Power(e,2) - p)*Power(p,2))/((-1 + e)*Power(1 + e,2)) - (EllipE*(-4 + p)*Power(p,2)*(-6 + 2*e + p))/(-1 + Power(e,2)) +
          (EllipK*Power(p,2)*(28 + 4*Power(e,2) - 12*p + Power(p,2)))/(-1 + Power(e,2)) + (4*(-4 + p)*p*(2*(1 + e)*EllipK + EllipPi2*(-6 - 2*e + p)))/(1 + e) + 2*Power(-4 + p,2)*(EllipK*(-4 + p) + (EllipPi1*p*(-6 - 2*e + p))/(2 + 2*e - p)))/
        (EllipK*Power(-4 + p,2))));

    *OmegaR = (p*Sqrt((-6 + 2*e + p)/(-4*Power(e,2) + Power(-2 + p,2)))*Pi)/
   (8*EllipK + ((-2*EllipPi2*(6 + 2*e - p)*(3 + Power(e,2) - p)*Power(p,2))/((-1 + e)*Power(1 + e,2)) - (EllipE*(-4 + p)*Power(p,2)*(-6 + 2*e + p))/(-1 + Power(e,2)) +
        (EllipK*Power(p,2)*(28 + 4*Power(e,2) - 12*p + Power(p,2)))/(-1 + Power(e,2)) + (4*(-4 + p)*p*(2*(1 + e)*EllipK + EllipPi2*(-6 - 2*e + p)))/(1 + e) + 2*Power(-4 + p,2)*(EllipK*(-4 + p) + (EllipPi1*p*(-6 - 2*e + p))/(2 + 2*e - p)))/
      Power(-4 + p,2));
}

void KerrGeoCoordinateFrequenciesVectorized(double* OmegaPhi_, double* OmegaTheta_, double* OmegaR_,
                              double* a, double* p, double* e, double* x, int length)
{

    #ifdef __USE_OMP__
    #pragma omp parallel for
    #endif
    for (int i = 0; i < length; i += 1)
    {
        if (a[i] != 0.0)
        {
            KerrGeoCoordinateFrequencies(&OmegaPhi_[i], &OmegaTheta_[i], &OmegaR_[i],
                                      a[i], p[i], e[i], x[i]);
        }
        else
        {
            SchwarzschildGeoCoordinateFrequencies(&OmegaPhi_[i], &OmegaR_[i], p[i], e[i]);
            OmegaTheta_[i] = OmegaPhi_[i];
        }

    }

}


struct params_holder
  {
    double a, p, e, x, Y;
  };

double separatrix_polynomial_full(double p, void *params_in)
{

    struct params_holder* params = (struct params_holder*)params_in;

    double a = params->a;
    double e = params->e;
    double x = params->x;

    return (-4*(3 + e)*Power(p,11) + Power(p,12) + Power(a,12)*Power(-1 + e,4)*Power(1 + e,8)*Power(-1 + x,4)*Power(1 + x,4) - 4*Power(a,10)*(-3 + e)*Power(-1 + e,3)*Power(1 + e,7)*p*Power(-1 + Power(x,2),4) - 4*Power(a,8)*(-1 + e)*Power(1 + e,5)*Power(p,3)*Power(-1 + x,3)*Power(1 + x,3)*(7 - 7*Power(x,2) - Power(e,2)*(-13 + Power(x,2)) + Power(e,3)*(-5 + Power(x,2)) + 7*e*(-1 + Power(x,2))) + 8*Power(a,6)*(-1 + e)*Power(1 + e,3)*Power(p,5)*Power(-1 + Power(x,2),2)*(3 + e + 12*Power(x,2) + 4*e*Power(x,2) + Power(e,3)*(-5 + 2*Power(x,2)) + Power(e,2)*(1 + 2*Power(x,2))) - 8*Power(a,4)*Power(1 + e,2)*Power(p,7)*(-1 + x)*(1 + x)*(-3 + e + 15*Power(x,2) - 5*e*Power(x,2) + Power(e,3)*(-5 + 3*Power(x,2)) + Power(e,2)*(-1 + 3*Power(x,2))) + 4*Power(a,2)*Power(p,9)*(-7 - 7*e + Power(e,3)*(-5 + 4*Power(x,2)) + Power(e,2)*(-13 + 12*Power(x,2))) + 2*Power(a,8)*Power(-1 + e,2)*Power(1 + e,6)*Power(p,2)*Power(-1 + Power(x,2),3)*(2*Power(-3 + e,2)*(-1 + Power(x,2)) + Power(a,2)*(Power(e,2)*(-3 + Power(x,2)) - 3*(1 + Power(x,2)) + 2*e*(1 + Power(x,2)))) - 2*Power(p,10)*(-2*Power(3 + e,2) + Power(a,2)*(-3 + 6*Power(x,2) + Power(e,2)*(-3 + 2*Power(x,2)) + e*(-2 + 4*Power(x,2)))) + Power(a,6)*Power(1 + e,4)*Power(p,4)*Power(-1 + Power(x,2),2)*(-16*Power(-1 + e,2)*(-3 - 2*e + Power(e,2))*(-1 + Power(x,2)) + Power(a,2)*(15 + 6*Power(x,2) + 9*Power(x,4) + Power(e,2)*(26 + 20*Power(x,2) - 2*Power(x,4)) + Power(e,4)*(15 - 10*Power(x,2) + Power(x,4)) + 4*Power(e,3)*(-5 - 2*Power(x,2) + Power(x,4)) - 4*e*(5 + 2*Power(x,2) + 3*Power(x,4)))) - 4*Power(a,4)*Power(1 + e,2)*Power(p,6)*(-1 + x)*(1 + x)*(-2*(11 - 14*Power(e,2) + 3*Power(e,4))*(-1 + Power(x,2)) + Power(a,2)*(5 - 5*Power(x,2) - 9*Power(x,4) + 4*Power(e,3)*Power(x,2)*(-2 + Power(x,2)) + Power(e,4)*(5 - 5*Power(x,2) + Power(x,4)) + Power(e,2)*(6 - 6*Power(x,2) + 4*Power(x,4)))) + Power(a,2)*Power(p,8)*(-16*Power(1 + e,2)*(-3 + 2*e + Power(e,2))*(-1 + Power(x,2)) + Power(a,2)*(15 - 36*Power(x,2) + 30*Power(x,4) + Power(e,4)*(15 - 20*Power(x,2) + 6*Power(x,4)) + 4*Power(e,3)*(5 - 12*Power(x,2) + 6*Power(x,4)) + 4*e*(5 - 12*Power(x,2) + 10*Power(x,4)) + Power(e,2)*(26 - 72*Power(x,2) + 44*Power(x,4)))));

}

double separatrix_polynomial_polar(double p, void *params_in)
{
    struct params_holder* params = (struct params_holder*)params_in;

    double a = params->a;
    double e = params->e;
    double x = params->x;

    return (Power(a,6)*Power(-1 + e,2)*Power(1 + e,4) + Power(p,5)*(-6 - 2*e + p) + Power(a,2)*Power(p,3)*(-4*(-1 + e)*Power(1 + e,2) + (3 + e*(2 + 3*e))*p) - Power(a,4)*Power(1 + e,2)*p*(6 + 2*Power(e,3) + 2*e*(-1 + p) - 3*p - 3*Power(e,2)*(2 + p)));
}


double separatrix_polynomial_equat(double p, void *params_in)
{
    struct params_holder* params = (struct params_holder*)params_in;

    double a = params->a;
    double e = params->e;
    double x = params->x;

    return (Power(a,4)*Power(-3 - 2*e + Power(e,2),2) + Power(p,2)*Power(-6 - 2*e + p,2) - 2*Power(a,2)*(1 + e)*p*(14 + 2*Power(e,2) + 3*p - e*p));
}



double
solver (struct params_holder* params, double (*func)(double, void*), double x_lo, double x_hi)
{
    gsl_set_error_handler_off();
    int status;
    int iter = 0, max_iter = 1000;
    const gsl_root_fsolver_type *T;
    gsl_root_fsolver *s;
    double r = 0, r_expected = sqrt (5.0);
    gsl_function F;

    F.function = func;
    F.params = params;

    T = gsl_root_fsolver_brent;
    s = gsl_root_fsolver_alloc (T);
    gsl_root_fsolver_set (s, &F, x_lo, x_hi);

    // printf("-----------START------------------- \n");
    // printf("xlo xhi %f %f\n", x_lo, x_hi);
    double epsrel=0.001;

    do
      {
        iter++;
        status = gsl_root_fsolver_iterate (s);
        r = gsl_root_fsolver_root (s);
        x_lo = gsl_root_fsolver_x_lower (s);
        x_hi = gsl_root_fsolver_x_upper (s);
        status = gsl_root_test_interval (x_lo, x_hi, 0.0, epsrel);
       
        // printf("result %f %f %f \n", r, x_lo, x_hi);
      }
    while (status == GSL_CONTINUE && iter < max_iter);
    
    // printf("result %f %f %f \n", r, x_lo, x_hi);
    // printf("stat, iter, GSL_SUCCESS %d %d %d\n", status, iter, GSL_SUCCESS);
    // printf("-----------END------------------- \n");

    if (status != GSL_SUCCESS)
    {
        // warning if it did not converge otherwise throw error
        if (iter == max_iter){
            printf("WARNING: Maximum iteration reached in Utility.cc in Brent root solver.\n");
            printf("Result=%f, x_low=%f, x_high=%f \n", r, x_lo, x_hi);
            printf("a, p, e, Y, sep = %f %f %f %f %f\n", params->a, params->p, params->e, params->Y, get_separatrix(params->a, params->e, r));
        }
        else
        {
            throw std::invalid_argument( "In Utility.cc Brent root solver failed");
        }
    }

    gsl_root_fsolver_free (s);
    return r;
}

double get_separatrix(double a, double e, double x)
{
    double p_sep, z1, z2;
    double sign;
    if (a == 0.0)
    {
        p_sep = 6.0 + 2.0 * e;
        return p_sep;
    }
    else if ((e == 0.0) & (abs(x) == 1.0))
    {
        z1 = 1. + pow((1. - pow(a,  2)), 1./3.) * (pow((1. + a), 1./3.)
             + pow((1. - a), 1./3.));

        z2 = sqrt(3. * pow(a, 2) + pow(z1, 2));

        // prograde
        if (x > 0.0)
        {
            sign = -1.0;
        }
        // retrograde
        else
        {
            sign = +1.0;
        }

        p_sep = (3. + z2 + sign * sqrt((3. - z1) * (3. + z1 + 2. * z2)));
        return p_sep;
    }
    else
    {
        // fills in p and Y with zeros
        struct params_holder params = {a, 0.0, e, x, 0.0};
        double x_lo, x_hi;

        // solve for polar p_sep
        x_lo = 1.0 + sqrt(3.0) + sqrt(3.0 + 2.0 * sqrt(3.0));
        x_hi = 8.0;



        double polar_p_sep = solver (&params, &separatrix_polynomial_polar, x_lo, x_hi);
        if (x == 0.0) return polar_p_sep;

        double equat_p_sep;
        if (x > 0.0)
        {
            x_lo = 1.0 + e;
            x_hi = 6 + 2. * e;

            equat_p_sep = solver (&params, &separatrix_polynomial_equat, x_lo, x_hi);

            x_lo = equat_p_sep;
            x_hi = polar_p_sep;
        } else
        {
            x_lo = polar_p_sep;
            x_hi = 12.0;
        }

        p_sep = solver (&params, &separatrix_polynomial_full, x_lo, x_hi);

        return p_sep;
    }
}

void get_separatrix_vector(double* separatrix, double* a, double* e, double* x, int length)
{

    #ifdef __USE_OMP__
    #pragma omp parallel for
    #endif
    for (int i = 0; i < length; i += 1)
    {
        separatrix[i] = get_separatrix(a[i], e[i], x[i]);
    }

}

double Y_to_xI_eq(double x, void *params_in)
{
    struct params_holder* params = (struct params_holder*)params_in;

    double a = params->a;
    double p = params->p;
    double e = params->e;
    double Y = params->Y;

    double E, L, Q;

    // get constants of motion
    KerrGeoConstantsOfMotion(&E, &L, &Q, a, p, e, x);
    double Y_ = L / sqrt(pow(L, 2) + Q);

    return Y - Y_;
}

#define YLIM 0.998
double Y_to_xI(double a, double p, double e, double Y)
{
    // TODO: check this
    if (abs(Y) > YLIM) return Y;
    // fills in x with 0.0
    struct params_holder params = {a, p, e, 0.0, Y};
    double x_lo, x_hi;

    // set limits
    // assume Y is close to x
    x_lo = Y - 0.15;
    x_hi = Y + 0.15;

    x_lo = x_lo > -YLIM? x_lo : -YLIM;
    x_hi = x_hi < YLIM? x_hi : YLIM;

    double x = solver (&params, &Y_to_xI_eq, x_lo, x_hi);
   
    return x;
}

void Y_to_xI_vector(double* x, double* a, double* p, double* e, double* Y, int length)
{

    #ifdef __USE_OMP__
    #pragma omp parallel for
    #endif
    for (int i = 0; i < length; i += 1)
    {
        x[i] = Y_to_xI(a[i], p[i], e[i], Y[i]);
    }

}


void set_threads(int num_threads)
{
    omp_set_num_threads(num_threads);
}

int get_threads()
{
    int num_threads;
    #pragma omp parallel for
    for (int i = 0; i < 1; i +=1)
    {
        num_threads = omp_get_num_threads();
    }

    return num_threads;
}


// Secondary Spin functions

double P(double r, double a, double En, double xi)
{
    return En*r*r - a*xi;
}

double deltaP(double r, double a, double En, double xi, double deltaEn, double deltaxi)
{
    return deltaEn*r*r - xi/r - a*deltaxi;
}


double deltaRt(double r, double am1, double a0, double a1, double a2)
{
    return am1/r + a0 + r*( a1 + r*a2 );
}


void KerrEqSpinFrequenciesCorrection(double* deltaOmegaR_, double* deltaOmegaPhi_,
                              double a, double p, double e, double x)
{
    printf("a, p, e, x, sep = %f %f %f %f\n", a, p, e, x);
    double M=1.0;
    double En = KerrGeoEnergy(a, p, e, x);
    double xi = KerrGeoAngularMomentum(a, p, e, x, En) - a*En;
    
    // get radial roots
    double r1, r2, r3, r4;
    KerrGeoRadialRoots(&r1, &r2, &r3, &r4, a, p, e, x, En, 0.);

    double deltaEn, deltaxi;

    deltaEn = (xi*(-(a*pow(En,2)*pow(r1,2)*pow(r2,2)) - En*pow(r1,2)*pow(r2,2)*xi + pow(a,2)*En*(pow(r1,2) + r1*r2 + pow(r2,2))*xi + 
       a*(pow(r1,2) + r1*(-2 + r2) + (-2 + r2)*r2)*pow(xi,2)))/
   (pow(r1,2)*pow(r2,2)*(a*pow(En,2)*r1*r2*(r1 + r2) + En*(pow(r1,2)*(-2 + r2) + r1*(-2 + r2)*r2 - 2*pow(r2,2))*xi + 2*a*pow(xi,2)));

    deltaxi = ((pow(r1,2) + r1*r2 + pow(r2,2))*xi*(En*pow(r2,2) - a*xi)*(-(En*pow(r1,2)) + a*xi))/
   (pow(r1,2)*pow(r2,2)*(a*pow(En,2)*r1*r2*(r1 + r2) + En*(pow(r1,2)*(-2 + r2) + r1*(-2 + r2)*r2 - 2*pow(r2,2))*xi + 2*a*pow(xi,2)));


    double am1, a0, a1, a2;
    am1 = (-2*a*pow(xi,2))/(r1*r2);  
    a0 = -2*En*(-(a*deltaxi) + deltaEn*pow(r1,2) + deltaEn*r1*r2 + deltaEn*pow(r2,2)) + 2*(a*deltaEn + deltaxi)*xi;
    a1 = -2*deltaEn*En*(r1 + r2);
    a2 = -2*deltaEn*En;
    
    double kr = (r1-r2)/(r1-r3) * (r3-r4)/(r2-r4); //convention without the sqrt
    double hr = (r1 - r2)/(r1 - r3);

    double rp = M + sqrt(pow(M, 2) - pow(a, 2));
    double rm = M - sqrt(pow(M, 2) - pow(a, 2));

    double hp = ((r1 - r2) * (r3 - rp))/((r1 - r3) * (r2 - rp));
    double hm = ((r1 - r2) * (r3 - rm))/((r1 - r3) * (r2 - rm));

    double Kkr = EllipticK(kr); //(* Elliptic integral of the first kind *)
    double Ekr = EllipticE(kr); //(* Elliptic integral of the second kind *)
    double Pihrkr = EllipticPi(hr, kr); //(* Elliptic integral of the third kind *)
    double Pihmkr = EllipticPi(hm, kr);
    double Pihpkr = EllipticPi(hp, kr);
    

    double Vtr3 = a*xi + ((pow(a,2) + pow(r3,2))*P(r3, a, En, xi))/CapitalDelta(r3,a); 
    double deltaVtr3 = a*deltaxi + (r3*r3 + a*a)/CapitalDelta(r3, a)*deltaP(r3, a, En, xi, deltaEn, deltaxi);

    double deltaIt1 = (2*((deltaEn*Pihrkr*(r2 - r3)*(4 + r1 + r2 + r3))/2. + (Ekr*(r1 - r3)*(deltaEn*r1*r2*r3 + 2*xi))/(2.*r1*r3) + 
       ((r2 - r3)*((Pihmkr*(pow(a,2) + pow(rm,2))*deltaP(rm, a, En, xi, deltaEn, deltaxi))/((r2 - rm)*(r3 - rm)) - 
            (Pihpkr*(pow(a,2) + pow(rp,2))*deltaP(rp, a, En, xi, deltaEn, deltaxi))/((r2 - rp)*(r3 - rp))))/(-rm + rp) + 
       Kkr*(-0.5*(deltaEn*(r1 - r3)*(r2 - r3)) + deltaVtr3)))/sqrt((1 - pow(En,2))*(r1 - r3)*(r2 - r4));

    double cK = Kkr*(-0.5*(a2*En*(r1 - r3)*(r2 - r3)) + (pow(a,4)*En*r3*(-am1 + pow(r3,2)*(a1 + 2*a2*r3)) + 
        2*pow(a,2)*En*pow(r3,2)*(-(am1*(-2 + r3)) + a0*r3 + pow(r3,3)*(a1 - a2 + 2*a2*r3)) + 
        En*pow(r3,5)*(-2*a0 - am1 + r3*(a1*(-4 + r3) + 2*a2*(-3 + r3)*r3)) + 2*pow(a,3)*(2*am1 + a0*r3 - a2*pow(r3,3))*xi + 
        2*a*r3*(am1*(-6 + 4*r3) + r3*(2*a1*(-1 + r3)*r3 + a2*pow(r3,3) + a0*(-4 + 3*r3)))*xi)/(pow(r3,2)*pow(r3 - rm,2)*pow(r3 - rp,2)));
    double cEPi = (En*(a2*Ekr*r2*(r1 - r3) + Pihrkr*(r2 - r3)*(2*a1 + a2*(4 + r1 + r2 + 3*r3))))/2.;
    double cPi = ((-r2 + r3)*((Pihmkr*(pow(a,2) + pow(rm,2))*P(rm, a, En, xi)*deltaRt(rm, am1, a0, a1, a2))/((r2 - rm)*pow(r3 - rm,2)*rm) - 
       (Pihpkr*(pow(a,2) + pow(rp,2))*P(rp, a, En, xi)*deltaRt(rp, am1, a0, a1, a2))/((r2 - rp)*pow(r3 - rp,2)*rp)))/(-rm + rp);
    
    double cE = (Ekr*((2*am1*(-r1 + r3)*xi)/(a*r1) + (r2*Vtr3*deltaRt(r3, am1, a0, a1, a2))/(r2 - r3)))/pow(r3,2);

    double deltaIt2 = -((cE + cEPi + cK + cPi)/(pow(1 - pow(En,2),1.5)*sqrt((r1 - r3)*(r2 - r4))));
    double deltaIt = deltaIt1 + deltaIt2;

    double It = (2*((En*(Ekr*r2*(r1 - r3) + Pihrkr*(r2 - r3)*(4 + r1 + r2 + r3)))/2. + 
       ((r2 - r3)*((Pihmkr*(pow(a,2) + pow(rm,2))*P(rm, a, En, xi))/((r2 - rm)*(r3 - rm)) - (Pihpkr*(pow(a,2) + pow(rp,2))*P(rp, a, En, xi))/((r2 - rp)*(r3 - rp))))/
        (-rm + rp) + Kkr*(-0.5*(En*(r1 - r3)*(r2 - r3)) + Vtr3)))/sqrt((1 - pow(En,2))*(r1 - r3)*(r2 - r4));

    double VPhir3 = xi + a/CapitalDelta(r3, a)*P(r3, a, En, xi);
    double deltaVPhir3 = deltaxi + a/CapitalDelta(r3, a)*deltaP(r3, a, En, xi, deltaEn, deltaxi);

    double deltaIPhi1 = (2*((Ekr*(r1 - r3)*xi)/(a*r1*r3) + (a*(r2 - r3)*((Pihmkr*deltaP(rm, a, En, xi, deltaEn, deltaxi))/((r2 - rm)*(r3 - rm)) - 
      (Pihpkr*deltaP(rp, a, En, xi, deltaEn, deltaxi))/((r2 - rp)*(r3 - rp))))/(-rm + rp) + Kkr*deltaVPhir3))/
      sqrt((1 - pow(En,2))*(r1 - r3)*(r2 - r4));

    double dK = (Kkr*(-(a*En*pow(r3,2)*(2*a0*(-1 + r3)*r3 + (a1 + 2*a2)*pow(r3,3) + am1*(-4 + 3*r3))) - pow(a,3)*En*r3*(am1 - pow(r3,2)*(a1 + 2*a2*r3)) - 
       pow(a,2)*(am1*(-4 + r3) - 2*a0*r3 - (a1 + 2*a2*(-1 + r3))*pow(r3,3))*xi - pow(-2 + r3,2)*r3*(3*am1 + r3*(2*a0 + a1*r3))*xi))/
       (pow(r3,2)*pow(r3 - rm,2)*pow(r3 - rp,2));
    
    double dPi = -((a*(r2 - r3)*((Pihmkr*P(rm, a, En, xi)*deltaRt(rm, am1, a0, a1, a2))/((r2 - rm)*pow(r3 - rm,2)*rm) - 
      (Pihpkr*P(rp, a, En, xi)*deltaRt(rp, am1, a0, a1, a2))/((r2 - rp)*pow(r3 - rp,2)*rp)))/(-rm + rp));
    double dE = (Ekr*((-2*am1*(r1 - r3)*xi)/(pow(a,2)*r1) + (r2*VPhir3*deltaRt(r3, am1, a0, a1, a2))/(r2 - r3)))/pow(r3,2);

    double deltaIPhi2 = -((dE + dK + dPi)/(pow(1 - pow(En,2),1.5)*sqrt((r1 - r3)*(r2 - r4))));
    double deltaIPhi = deltaIPhi1 + deltaIPhi2;

    double IPhi = (2*((a*(r2 - r3)*((Pihmkr*P(rm, a, En, xi))/((r2 - rm)*(r3 - rm)) - (Pihpkr*P(rp, a, En, xi))/((r2 - rp)*(r3 - rp))))/(-rm + rp) + Kkr*VPhir3))/
   sqrt((1 - pow(En,2))*(r1 - r3)*(r2 - r4));

    double deltaOmegaR = -M_PI/pow(It,2)*deltaIt;
    double deltaOmegaPhi = deltaIPhi/It-IPhi/pow(It,2)*deltaIt;

    //                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     cE,                deltaIt2,           It,                deltaIt,            deltaIPhi1, dK, dPi, dE, deltaIPhi2, deltaIPhi, IPhi
    // 0.952696869207406, 2.601147313747245, 11.11111111111111, 9.09090909090909, 1.450341827498306, 0, -0.001534290476164244, -0.1322435748015139, -0.1205695381380546, -0.02411390762761123, 0.0590591407311683, 0.002923427466192832, 0.5641101056459328, 1.435889894354067, 0.03336154445124933, 0.2091139909146586, 0.0217342349165277, 0.0003947869154376093, 1.584149072588183, 1.55761216767624, 1.782193864035892, 1.601724642759611, 1.58446319264442, -112.2728614607676, 1.498105017522236, 111.1816561327114, 1.459858647716873, -7.095639020498573, 133.1766110081966, -7.640013106344259, -0.1508069013343114, -34.72953487758193, 34.00126350567278, 0.7853682931498268, -0.2170281681543273, -0.3678350694886387, 4.044867174992484
    // 0.952697 2.601147                     11.111111          9.090909          1.450342           0.000000 -0.001534        -0.132244            -0.120570            -0.024114             0.059059            0.002923               0.564110           1.435890           0.033362             0.209114            0.021734            0.000395               1.584149           1.557612          1.782194           1.601725           1.584463          -112.272861         1.498105           111.181656         5.642003           -22.992171          -208.799031        -23.536545          -0.150807            -34.729535          34.001264          0.785368            -0.217028            -0.367835            4.044867
    // 0.952697 2.601147 11.111111 9.090909 1.450342 0.000000 -0.001534 -0.132244 -0.120570 -0.024114 0.059059 0.002923 0.564110 1.435890 0.033362 0.209114 0.021734 0.000395 1.584149 1.557612 1.782194 1.601725 1.584463 -112.272861 1.498105 111.181656 1.459859 -7.095639 133.176611 -7.640013 -0.150807 -34.729535 34.001264 0.785368 -0.217028 -0.367835 4.044867
    // 0.952696869207406, 2.601147313747245, 11.11111111111111, 9.09090909090909, 1.450341827498306, 0, -0.001534290476164244, -0.1322435748015139, -0.1205695381380546, -0.02411390762761123, 0.0590591407311683, 0.002923427466192832, 0.5641101056459328, 1.435889894354067, 0.03336154445124933, 0.2091139909146586, 0.0217342349165277, 0.0003947869154376093, 1.584149072588183, 1.55761216767624, 1.782193864035892, 1.601724642759611, 1.58446319264442, -112.2728614607676, 1.498105017522236, 111.1816561327114, 1.459858647716873, -7.095639020498573, 133.1766110081966, -7.640013106344259, -0.1508069013343114, -34.72953487758193, 34.00126350567278, 0.7853682931498268, -0.2170281681543273, -0.3678350694886387, 4.044867174992484
    // printf("En, xi, r1, r2, r3, r4, deltaEn, deltaxi, am1, a0, a1, a2, rm, rp, kr, hr, hm, hp, Kkr, Ekr, Pihrkr, Pihmkr, Pihpkr, cK, cEPi, cPi, cE,deltaIt2, It, deltaIt, deltaPhi1, dK, dPi, dE, deltaPhi2, deltaPhi, IPhi =%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f\n",En, xi, r1, r2, r3, r4, deltaEn, deltaxi, am1, a0, a1, a2, rm, rp, kr, hr, hm, hp, Kkr, Ekr, Pihrkr, Pihmkr, Pihpkr, cK, cEPi, cPi, cE, deltaIt2, It, deltaIt, deltaIPhi1, dK, dPi, dE, deltaIPhi2, deltaIPhi, IPhi);
    *deltaOmegaR_ = deltaOmegaR;
    *deltaOmegaPhi_ = deltaOmegaPhi;
    printf("deltaOmegaR_, deltaOmegaPhi_, = %f %f\n", deltaOmegaR, deltaOmegaPhi);

}



void KerrEqSpinFrequenciesCorrVectorized(double* OmegaPhi_, double* OmegaTheta_, double* OmegaR_,
                              double* a, double* p, double* e, double* x, int length)
{

    #ifdef __USE_OMP__
    #pragma omp parallel for
    #endif
    for (int i = 0; i < length; i += 1)
    {

        KerrEqSpinFrequenciesCorrection(&OmegaR_[i],&OmegaPhi_[i],
                                    a[i], p[i], e[i], x[i]);
    }

}
















/*
int main()
{
    double a = 0.5;
    double p = 10.0;
    double e = 0.2;
    double iota = 0.4;
    double x = cos(iota);

    double temp = KerrGeoMinoFrequencies(a, p, e, x);

    //printf("%e %e %e\n", En, L, C);
}
*/

