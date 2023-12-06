#include <math.h>
#include <stdio.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_odeiv2.h>
#include <gsl/gsl_sf_ellint.h>
#include <algorithm>

#include <hdf5.h>
#include <hdf5_hl.h>

#include <complex>
#include <cmath>

#include "Interpolant.h"
#include "global.h"
#include "Utility.hh"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>      // std::setprecision
#include <cstring>

#include "dIdt8H_5PNe10.h"
#include "ode.hh"
#include "KerrEquatorial.h"

#define pn5_Y
#define pn5_citation1 Pn5_citation
__deriv__
void pn5(double* pdot, double* edot, double* Ydot,
                  double* Omega_phi, double* Omega_theta, double* Omega_r,
                  double epsilon, double a, double p, double e, double Y, double* additional_args)
{
    // evaluate ODEs

    // the frequency variables are pointers!
    if (abs(Y) == 1.) {
        double x = Y;
        KerrGeoEquatorialCoordinateFrequencies(Omega_phi, Omega_theta, Omega_r, a, p, e, x);// shift to avoid problem in fundamental frequencies
    }
    else { 
        double x = Y_to_xI(a, p, e, Y);
        KerrGeoCoordinateFrequencies(Omega_phi, Omega_theta, Omega_r, a, p, e, x);
    }

	int Nv = 10;
    int ne = 10;
    *pdot = epsilon * dpdt8H_5PNe10 (a, p, e, Y, Nv, ne);

    // needs adjustment for validity
    Nv = 10;
    ne = 8;
	*edot = epsilon * dedt8H_5PNe10 (a, p, e, Y, Nv, ne);

    Nv = 7;
    ne = 10;
    *Ydot = epsilon * dYdt8H_5PNe10 (a, p, e, Y, Nv, ne);

}


// Initialize flux data for inspiral calculations
void load_and_interpolate_flux_data(struct interp_params *interps, const std::string& few_dir){

	// Load and interpolate the flux data
    std::string fp = "few/files/FluxNewMinusPNScaled_fixed_y_order.dat";
    fp = few_dir + fp;
	ifstream Flux_file(fp);

    if (Flux_file.fail())
    {
        throw std::runtime_error("The file FluxNewMinusPNScaled_fixed_y_order.dat did not open sucessfully. Make sure it is located in the proper directory (Path/to/Installation/few/files/).");
    }

	// Load the flux data into arrays
	string Flux_string;
	vector<double> ys, es, Edots, Ldots;
	double y, e, Edot, Ldot;
	while(getline(Flux_file, Flux_string)){

		stringstream Flux_ss(Flux_string);

		Flux_ss >> y >> e >> Edot >> Ldot;

		ys.push_back(y);
		es.push_back(e);
		Edots.push_back(Edot);
		Ldots.push_back(Ldot);
	}

	// Remove duplicate elements (only works if ys are perfectly repeating with no round off errors)
	sort( ys.begin(), ys.end() );
	ys.erase( unique( ys.begin(), ys.end() ), ys.end() );

	sort( es.begin(), es.end() );
	es.erase( unique( es.begin(), es.end() ), es.end() );

	Interpolant *Edot_interp = new Interpolant(ys, es, Edots);
	Interpolant *Ldot_interp = new Interpolant(ys, es, Ldots);

	interps->Edot = Edot_interp;
	interps->Ldot = Ldot_interp;

}


// Class to carry gsl interpolants for the inspiral data
// also executes inspiral calculations
SchwarzEccFlux::SchwarzEccFlux(std::string few_dir)
{
    interps = new interp_params;

    // prepare the data
    // python will download the data if
    // the user does not have it in the correct place
    load_and_interpolate_flux_data(interps, few_dir);
	//load_and_interpolate_amp_vec_norm_data(&amp_vec_norm_interp, few_dir);
}

#define SchwarzEccFlux_num_add_args 0
#define SchwarzEccFlux_spinless
#define SchwarzEccFlux_equatorial
#define SchwarzEccFlux_file1 FluxNewMinusPNScaled_fixed_y_order.dat
__deriv__
void SchwarzEccFlux::deriv_func(double* pdot, double* edot, double* xdot,
                  double* Omega_phi, double* Omega_theta, double* Omega_r,
                  double epsilon, double a, double p, double e, double x, double* additional_args)
{
    if ((6.0 + 2. * e) > p)
    {
        *pdot = 0.0;
        *edot = 0.0;
        *xdot = 0.0;
        return;
    }

    SchwarzschildGeoCoordinateFrequencies(Omega_phi, Omega_r, p, e);
    *Omega_theta = *Omega_phi;

    double y1 = log((p -2.*e - 2.1));

    // evaluate ODEs, starting with PN contribution, then interpolating over remaining flux contribution

	double yPN = pow((*Omega_phi),2./3.);

	double EdotPN = (96 + 292*Power(e,2) + 37*Power(e,4))/(15.*Power(1 - Power(e,2),3.5)) * pow(yPN, 5);
	double LdotPN = (4*(8 + 7*Power(e,2)))/(5.*Power(-1 + Power(e,2),2)) * pow(yPN, 7./2.);

	double Edot = -epsilon*(interps->Edot->eval(y1, e)*pow(yPN,6.) + EdotPN);

	double Ldot = -epsilon*(interps->Ldot->eval(y1, e)*pow(yPN,9./2.) + LdotPN);

	*pdot = (-2*(Edot*Sqrt((4*Power(e,2) - Power(-2 + p,2))/(3 + Power(e,2) - p))*(3 + Power(e,2) - p)*Power(p,1.5) + Ldot*Power(-4 + p,2)*Sqrt(-3 - Power(e,2) + p)))/(4*Power(e,2) - Power(-6 + p,2));

    // handle e = 0.0
	if (e > 0.)
    {
        *edot = -((Edot*Sqrt((4*Power(e,2) - Power(-2 + p,2))/(3 + Power(e,2) - p))*Power(p,1.5)*
            	  (18 + 2*Power(e,4) - 3*Power(e,2)*(-4 + p) - 9*p + Power(p,2)) +
            	 (-1 + Power(e,2))*Ldot*Sqrt(-3 - Power(e,2) + p)*(12 + 4*Power(e,2) - 8*p + Power(p,2)))/
            	(e*(4*Power(e,2) - Power(-6 + p,2))*p));
    }
    else
    {
        *edot = 0.0;
    }

    *xdot = 0.0;
}


// destructor
SchwarzEccFlux::~SchwarzEccFlux()
{

    delete interps->Edot;
    delete interps->Ldot;
    delete interps;


}

//--------------------------------------------------------------------------------
Vector fill_vector(std::string fp){
	ifstream file_x(fp);

    if (file_x.fail())
    {
        throw std::runtime_error("The file  did not open sucessfully. Make sure it is located in the proper directory.");
    }
    else{
        // cout << "importing " + fp << endl;
    }

	// Load the flux data into arrays
	string string_x;
    Vector xs;
	double x;
	while(getline(file_x, string_x)){
		stringstream ss(string_x);
		ss >> x ;
		xs.push_back(x);
	}
    return xs;

}

KerrEccentricEquatorial::KerrEccentricEquatorial(std::string few_dir)
{

    // interpolant()
    std::string fp;
    fp = few_dir + "few/files/x0.dat";
    Vector x1 = fill_vector(fp);
    fp = few_dir + "few/files/x1.dat";
    Vector x2 = fill_vector(fp);
    fp = few_dir + "few/files/x2.dat";
    Vector x3 = fill_vector(fp);

    fp = few_dir + "few/files/coeff_edot.dat";
    Vector coeff2 = fill_vector(fp);
    fp = few_dir + "few/files/coeff_pdot.dat";
    Vector coeff = fill_vector(fp);

    fp = few_dir + "few/files/coeff_Endot.dat";
    Vector coeffEn = fill_vector(fp);
    fp = few_dir + "few/files/coeff_Ldot.dat";
    Vector coeffL = fill_vector(fp);
    
    edot_interp = new TensorInterpolant(x1, x2, x3, coeff2);
    pdot_interp = new TensorInterpolant(x1, x2, x3, coeff);

    Edot_interp = new TensorInterpolant(x1, x2, x3, coeffEn);
    Ldot_interp = new TensorInterpolant(x1, x2, x3, coeffL);

    // cout << "pdot=" << pdot_interp<< pdot_interp->eval(2.000000000000000111e-01, 1.260000000000000009e+00, 4.599900000000000100e-01) << '\n'<< endl;
    // cout << "edot=" << edot_interp<< edot_interp->eval(2.000000000000000111e-01, 1.260000000000000009e+00, 4.599900000000000100e-01) << '\n'<< endl;

}


// #define KerrEccentricEquatorial_Y
#define KerrEccentricEquatorial_equatorial
#define KerrEccentricEquatorial_num_add_args 0
__deriv__
void KerrEccentricEquatorial::deriv_func(double* pdot, double* edot, double* xdot,
                  double* Omega_phi, double* Omega_theta, double* Omega_r,
                  double epsilon, double a, double p, double e, double x, double* additional_args)
{

    double p_sep = get_separatrix(a, e, x);
    // make sure we do not step into separatrix
    if ((e < 0.0)||(p<p_sep))
    {
        *pdot = 0.0;
        *edot = 0.0;
        *xdot = 0.0;
        return;
    }

    // evaluate ODEs
    // cout << "beginning" << " a =" << a  << "\t" << "p=" <<  p << "\t" << "e=" << e <<endl;
    
    // auto start = std::chrono::steady_clock::now();
    // the frequency variables are pointers!
    KerrGeoEquatorialCoordinateFrequencies(Omega_phi, Omega_theta, Omega_r, a, p, e, x);// shift to avoid problem in fundamental frequencies
    // auto end = std::chrono::steady_clock::now();
    // std::chrono::duration<double>  msec = end-start;
    // std::cout << "elapsed time fund freqs: " << msec.count() << "s\n";

    // get r variable
    // double *Omega_phi_sep_circ,*Omega_theta_sep_circ,*Omega_r_sep_circ;
    // KerrGeoEquatorialCoordinateFrequencies(Omega_phi_sep_circ, Omega_theta_sep_circ, Omega_r_sep_circ, a, p_sep, 0.0, x);// shift to avoid problem in fundamental frequencies
    
    double r,Omega_phi_sep_circ;
    // reference frequency
    Omega_phi_sep_circ = 1.0/ (a + pow(p_sep/( 1.0 + e ), 1.5) );
    r = pow(*Omega_phi/ Omega_phi_sep_circ, 2.0/3.0 ) * (1.0 + e);

    if (isnan(r)){
        cout << " a =" << a  << "\t" << "p=" <<  p << "\t" << "e=" << e <<  "\t" << "x=" << x << "\t" << r << " plso =" <<  p_sep << endl;
        cout << "omegaphi circ " <<  Omega_phi_sep_circ << " omegaphi " <<  *Omega_phi << " omegar " <<  *Omega_r <<endl;
        throw std::exception();
        }
    
    
    double pdot_out, edot_out, xdot_out;
    double Edot, Ldot, Qdot, E_here, L_here, Q_here;
    
    // Flux from Scott
    double risco = get_separatrix(a, 0.0, x);
    double u = log((p-p_sep + 4.0 - 0.05)/4.0);
    double w = sqrt(e);
    // p, e
    pdot_out = pdot_interp->eval(a, u, w) * ((8.*pow(1. - pow(e,2),1.5)*(8. + 7.*pow(e,2)))/(5.*p*(pow(p - risco,2) - pow(-risco + p_sep,2))));
    edot_out = edot_interp->eval(a, u, w) * ((pow(1. - pow(e,2),1.5)*(304. + 121.*pow(e,2)))/(15.*pow(p,2)*(pow(p - risco,2) - pow(-risco + p_sep,2))));
    // E, L
    // Edot = Edot_interp->eval(a,u,w) * (32./5. * pow(p,-5) * pow(1-e*e,1.5) * (1. + 73./24.* e*e + 37./96. * e*e*e*e));
    // Ldot = Ldot_interp->eval(a,u,w) * (32./5. * pow(p,-7/2) * pow(1-e*e,1.5) * (1. + 7./8. * e*e) );
    // Qdot = 0.0;
    // cout << " E =" << Edot  << endl;
    
    // Flux from Susanna
    // Edot = Edot_GR(a*copysign(1.0,x),e,r,p);
    // Ldot = Ldot_GR(a*copysign(1.0,x),e,r,p)*copysign(1.0,x);
    // Qdot = 0.0;
    // cout << " E =" << Edot / Edot_GR(a*copysign(1.0,x),e,r,p)  << endl;
    
    // KerrGeoConstantsOfMotion(&E_here, &L_here, &Q_here, a, p, e, x);
    // Jac(a, p, e, x, E_here, L_here, Q_here, -Edot, -Ldot, Qdot, pdot_out, edot_out, xdot_out);
    
    // needs adjustment for validity
    if (e > 1e-6)
    {
        *pdot = epsilon * pdot_out;
        *edot = epsilon * edot_out;
    }
    else{
        *edot = 0.0;
        *pdot = epsilon * pdot_out;
        
    }

    
    *xdot = 0.0;

    // delete GKR;
    return;

}


// destructor
KerrEccentricEquatorial::~KerrEccentricEquatorial()
{

    delete pdot_interp;
    delete edot_interp;
    delete Edot_interp;
    delete Ldot_interp;

}