/*
DESCRIPTION: 

	By Iowa Hills Software, LLC  IowaHills.com
	Original Name: IIRFilterCode.cpp
	If you find a problem, please leave a note at:
	http://www.iowahills.com/feedbackcomments.html
	December 3, 2014

	This code calculates the coefficients for IIR filters from a set of 2nd order s plane coefficients.
	The s plane coefficients for Butterworth, Chebyshev, Gauss, Bessel, Adj Gauss, Inverse Chebyshev,
	and Elliptic filters are tabulated in the FilterCoefficients.hpp file for up to 10 poles.
	The s plane filters were frequency scaled so their 3 dB frequency is at s = omega = 1.
	We also ordered the poles and zeros in the tables in a manner appropriate for IIR filters.
	For a derivation of the formulas used here, see the IIREquationDerivations.pdf

	In addition to calculating the IIR coefficients, this file also contains a Form 1 Biquad
	filter implementation and the code to calculate the frequency response of an IIR filter.

	---------------------------------------------------------------------------
	Modifications by JRH: 4 October 2015
	- renamed from: IIRFilterCode.cpp
	- remove requirement for vcl.h header for "ShowMessage" - just print to stderr instead
	- include definitions & data types from the following header definitions 
		IIRFilterCode.h
		QuadRootsCode.h
	- include contents from this file in internal function GetSPlaneCoefficients
		FilterCoefficients.hpp
	---------------------------------------------------------------------------
	
USES: 

DEPENDENCIES: 
	None (formerly FFTCode.h,IIRFilterCode.h,QuadRootsCode.h,FilterCoefficients.hpp)

ARGUMENTS: 
		int NumPoles :  2-10 except the Elliptics, which are 4-10 (hence the defacto order of filters is 2-5)
		int *nsections : single-value result specifying number of sections (6 coefficients each) - calling function should pass address of the variable
		char *message : pre-allocated array to hold error message

RETURN VALUE:
	pointer to an array of 6*nsections coefficients, NULL on error
	nsections variable will be assigned 
	
SAMPLE CALL:

	int setpoles=6, nsections;
	char message[256];

	coefs= xf_filter_IIRcoef1_d(setpoles,&nsections,message);

*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DEFINITIONS 
#define NUM_PLOT_PTS 1024
#define M_SQRT3_2 0.8660254037844386467637231    // sqrt(3)/2
#define ZERO_PLUS   8.88178419700125232E-16      // 2^-50 = 4*DBL_EPSILON
#define ZERO_MINUS -8.88178419700125232E-16
#define ARRAY_DIM 25
#define OVERFLOW_LIMIT  1.0E20

// DECLARE DATA TYPES
enum TIIRPassTypes {iirLPF, iirHPF, iirBPF, iirNOTCH};
// The code relies on ftELLIPTIC_00 coming after the all pole filters, and being the 1st Elliptic.
enum TIIRFilterTypes {ftBUTTERWORTH, ftGAUSSIAN, ftBESSEL, ftADJGAUSS, ftCHEBYSHEV, ftINVERSE_CHEBY,
                       ftELLIPTIC_00, ftELLIPTIC_02, ftELLIPTIC_04, ftELLIPTIC_06,  ftELLIPTIC_08,
                       ftELLIPTIC_10, ftELLIPTIC_12, ftELLIPTIC_14, ftELLIPTIC_16, ftELLIPTIC_18,
                       ftELLIPTIC_20, ftCOUNT, ftNOT_IIR};
struct TSPlaneCoeff { double A[ARRAY_DIM]; double B[ARRAY_DIM]; double C[ARRAY_DIM]; double D[ARRAY_DIM]; double E[ARRAY_DIM]; double F[ARRAY_DIM]; int NumSections; };
struct TIIRCoeff {double a0[ARRAY_DIM]; double a1[ARRAY_DIM]; double a2[ARRAY_DIM]; double a3[ARRAY_DIM]; double a4[ARRAY_DIM]; double b0[ARRAY_DIM];
double b1[ARRAY_DIM]; double b2[ARRAY_DIM]; double b3[ARRAY_DIM]; double b4[ARRAY_DIM]; int NumSections; };

// DECLARE INTERNAL FUNCTIONS 
TIIRCoeff CalcIIRFilterCoeff(TIIRFilterTypes ProtoType, double Beta, TIIRPassTypes PassType, int NumPoles, double OmegaC, double BW);
TSPlaneCoeff GetSPlaneCoefficients(TIIRFilterTypes FilterType, int NumPoles, double Beta);
void FilterWithIIR(TIIRCoeff IIRCoeff, double *Signal, double *FilteredSignal, int NumSigPts);
void IIRFreqResponse(TIIRCoeff IIRCoeff, int NumSections, double *RealHofZ, double *ImagHofZ, int NumPts);
int QuadCubicRoots(int N, double *Coeff, double *RootsReal, double *RootsImag);
void QuadRoots(long double *P, long double *RealRoot, long double *ImagRoot);
int CubicRoots(long double *P, long double *RealRoot, long double *ImagRoot);
void BiQuadRoots(long double *P, long double *RealRoot, long double *ImagRoot);
double SectCalc(int j, int k, double x, TIIRCoeff IIRCoeff);


//---------------------------------------------------------------------------
// START OF THE MAIN FUNCTION -----------------------------------------------
//---------------------------------------------------------------------------

double *xf_filter_IIRcoef1_d(char *setfilt, double *params, int *nsections, char *message) {

	char *thisfunc="xf_filter_IIRcoef1_d\0"; 
	int i,j,k,NumPoles;
	double settype,setfreq,setsampfreq,setwidth;
	double Ripple,StopBanddB,OmegaC,BW,Gamma;
	double RealHofZ[NUM_PLOT_PTS];   // Real and imag parts of H(z). Used with the function IIRFreqResponse.
	double ImagHofZ[NUM_PLOT_PTS];
	double *coefs=NULL;
	TIIRCoeff IIRCoeff;          // These next 3 are defined in the header file.
	TIIRPassTypes PassType;      // iirLPF, iirHPF, iirBPF, iirNOTCH
	TIIRFilterTypes FiterType;   // ftBUTTERWORTH, ftGAUSSIAN, ftBESSEL, ftADJGAUSS, ftCHEBYSHEV, ftINVERSE_CHEBY, ftELLIPTIC_00

	NumPoles=(int)params[0];     // 2-10, for 1-5th-order filters
	settype=(int)params[1];     // 1=lowpass,2=highpass,3=bandpass,4=notch
	setfreq= params[2];          // corner frequency (Hz)
	setsampfreq= params[3];      // sample-frequency (Hz)
	setwidth= params[4];         // bandwidth (Hz) 
	Ripple= params[5];           // 0.0 - 1.0 dB for Chebyshev in increments of 0.10  0.0 - 0.2 for the Elliptics in increments 0f 0.02
	StopBanddB= params[6];       // 20 - 100 dB for the Inv Cheby  40 - 100 for the Elliptics
	Gamma= params[7];            // -1.0 <= Gamma <= 1.0  Gamma controls the transition BW on the Adjustable Gauss

	/* PARAMETER CONVERSION */
	OmegaC= 2.0 * (setfreq/setsampfreq) ; // 0.0 < OmegaC < 1.0
	BW= 2.0 * (setwidth/setsampfreq) ;    // 0.0 < BandWidth < 1.0
	if(settype==1) PassType = iirLPF;
	if(settype==2) PassType = iirHPF;
	if(settype==3) PassType = iirBPF;
	if(settype==4) PassType = iirNOTCH;
	if(strcmp(setfilt,"butterworth")==0) FiterType = ftBUTTERWORTH;
	if(strcmp(setfilt,"chebyshev")==0) FiterType = ftCHEBYSHEV;
	if(strcmp(setfilt,"chebyshevinv")==0) FiterType = ftINVERSE_CHEBY;
	if(strcmp(setfilt,"gauss")==0) FiterType = ftGAUSSIAN;
	if(strcmp(setfilt,"gaussadj")==0) FiterType = ftADJGAUSS;
	if(strcmp(setfilt,"bessel")==0) FiterType = ftBESSEL;
	if(strcmp(setfilt,"eliptic")==0) FiterType = ftELLIPTIC_00;
	
	
	// If using an Elliptic, we must select the ELLIPTIC according to the Ripple value.
	// ftELLIPTIC_00 is the 1st ELLIPTIC in TIIRFilterTypes.
	if(FiterType >= ftELLIPTIC_00)
	{
		if(Ripple >= 0.19)       FiterType = ftELLIPTIC_20; // Ripple = 0.20 dB
		else if(Ripple >= 0.17)  FiterType = ftELLIPTIC_18; // Ripple = 0.18 dB
		else if(Ripple >= 0.15)  FiterType = ftELLIPTIC_16; // Ripple = 0.16 dB
		else if(Ripple >= 0.13)  FiterType = ftELLIPTIC_14; // Ripple = 0.14 dB
		else if(Ripple >= 0.11)  FiterType = ftELLIPTIC_12; // Ripple = 0.12 dB
		else if(Ripple >= 0.09)  FiterType = ftELLIPTIC_10; // Ripple = 0.10 dB
		else if(Ripple >= 0.07)  FiterType = ftELLIPTIC_08; // Ripple = 0.08 dB
		else if(Ripple >= 0.05)  FiterType = ftELLIPTIC_06; // Ripple = 0.06 dB
		else if(Ripple >= 0.03)  FiterType = ftELLIPTIC_04; // Ripple = 0.04 dB
		else if(Ripple >= 0.01)  FiterType = ftELLIPTIC_02; // Ripple = 0.02 dB
		else                     FiterType = ftELLIPTIC_00; // Ripple = 0.00 dB
	}

	// Calling CalcIIRFilterCoeff() fills the IIRCoeff struct.
	// Beta, the 2nd function argument, has a different meaning, depending on the filter type.
	// Beta is also range checked in GetSPlaneCoefficients()

	// Beta is ignored and set to 0
		if(FiterType == ftBUTTERWORTH || FiterType == ftBESSEL || FiterType == ftGAUSSIAN){
		IIRCoeff = CalcIIRFilterCoeff(FiterType, 0, PassType, NumPoles, OmegaC, BW);
	}
	// Beta = Ripple in dB  >= 0
	if(FiterType == ftCHEBYSHEV){
		if(Ripple < 0.0)Ripple = -Ripple;
		IIRCoeff = CalcIIRFilterCoeff(FiterType, Ripple, PassType, NumPoles, OmegaC, BW);
	}
	// Beta = StopBanddB >= 0
	if(FiterType == ftINVERSE_CHEBY || FiterType >= ftELLIPTIC_00){
		if(StopBanddB < 0.0)StopBanddB = -StopBanddB;
		IIRCoeff = CalcIIRFilterCoeff(FiterType, StopBanddB, PassType, NumPoles, OmegaC, BW);
	}
	// Beta = Gamma, which adjusts the width of the transition band.  -1 <= Gamma <= 1
	if(FiterType == ftADJGAUSS) {
		if(Gamma < -1.0)Gamma = -1.0;
		if(Gamma > 1.0) Gamma = 1.0;
		IIRCoeff = CalcIIRFilterCoeff(FiterType, Gamma, PassType, NumPoles, OmegaC, BW);
	}


// This calculates the frequency response of the filter by doing a DFT of the IIR coefficients.
// ??? IIRFreqResponse(IIRCoeff, IIRCoeff.NumSections, RealHofZ, ImagHofZ, NUM_PLOT_PTS);

	/* added code to translate from IIR structure to something simpler */
	*nsections=IIRCoeff.NumSections; 
	if((coefs=(double *)realloc(coefs,((IIRCoeff.NumSections)*6)*sizeof(double)))==NULL) {sprintf(message,"%s [ERROR]: insufficient memory",thisfunc);return(NULL);};
	for(j=0;j<IIRCoeff.NumSections; j++) {
		coefs[j*6+0]=IIRCoeff.a0[j];
		coefs[j*6+1]=IIRCoeff.a1[j];
		coefs[j*6+2]=IIRCoeff.a2[j];
		coefs[j*6+3]=IIRCoeff.b0[j];
		coefs[j*6+4]=IIRCoeff.b1[j];
		coefs[j*6+5]=IIRCoeff.b2[j];
	}
	return(coefs);
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// H(s) = ( Ds^2 + Es + F ) / ( As^2 + Bs + C )
// H(z) = ( b2z^2 + b1z + b0 ) / ( a2z^2 + a1z + a0 )
TIIRCoeff CalcIIRFilterCoeff(TIIRFilterTypes ProtoType, double Beta, TIIRPassTypes PassType, int NumPoles, double OmegaC, double BW)
{
 int j, k;
 double Scalar;
 double Coeff[5], RealRoot[4], ImagRoot[4];
 double A, B, C, D, E, F, T, Q, Arg;
 double a2[ARRAY_DIM], a1[ARRAY_DIM], a0[ARRAY_DIM], b2[ARRAY_DIM], b1[ARRAY_DIM], b0[ARRAY_DIM];

 TSPlaneCoeff SPlaneCoeff;
 TIIRCoeff IIR; // This gets returned.

 // Init the IIR structure. a3, a4, b3,and b4 are only used to calculate the 2nd order
 // sections for the bandpass and notch filters. They get discarded.
 for(j=0; j<ARRAY_DIM; j++)
  {
   IIR.a0[j] = 0.0;  IIR.b0[j] = 0.0;
   IIR.a1[j] = 0.0;  IIR.b1[j] = 0.0;
   IIR.a2[j] = 0.0;  IIR.b2[j] = 0.0;
   IIR.a3[j] = 0.0;  IIR.b3[j] = 0.0;
   IIR.a4[j] = 0.0;  IIR.b4[j] = 0.0;
  }
 IIR.NumSections = 0;

 // This gets the 2nd order s plane filter coefficients from the tables in FilterCoefficients.hpp.
 // If you have code that computes these coefficients, then use that instead.
 SPlaneCoeff = GetSPlaneCoefficients(ProtoType, NumPoles, Beta);
 IIR.NumSections = SPlaneCoeff.NumSections;  // NumSections = (NumPoles + 1)/2

 // T sets the filter's corner frequency, or center freqency.
 // The Bilinear transform is defined as:  s = 2/T * tan(Omega/2) = 2/T * (1 - z)/(1 + z)
 T = 2.0 * tan(OmegaC * M_PI_2);
 Q = 1.0 + OmegaC;
 if(Q > 1.95)Q = 1.95;
 Q = 0.8 * tan(Q * M_PI_4); // This is a correction factor for Q. Q is used with band pass and notch flters.
 Q = OmegaC / BW / Q;       // This is the corrected Q.

 k = 0;
 for(j=0; j<SPlaneCoeff.NumSections; j++)
  {
   A = SPlaneCoeff.A[j];   // We use A - F to make the code easier to read.
   B = SPlaneCoeff.B[j];
   C = SPlaneCoeff.C[j];
   D = SPlaneCoeff.D[j];
   E = SPlaneCoeff.E[j];
   F = SPlaneCoeff.F[j];

  // b's are the numerator  a's are the denominator
  if(PassType == iirLPF)
   {
	if(A == 0.0 && D == 0.0) // 1 pole case
	 {
	  Arg = (2.0*B + C*T);
	  IIR.a2[j] = 0.0;
	  IIR.a1[j] = (-2.0*B + C*T) / Arg;
	  IIR.a0[j] = 1.0;     // The filter implementation depends on a0 = 1.

	  IIR.b2[j] = 0.0;
	  IIR.b1[j] = (-2.0*E + F*T) / Arg * C/F;
	  IIR.b0[j] = ( 2.0*E + F*T) / Arg * C/F;
	 }
	else // 2 poles
	 {
	  Arg = (4.0*A + 2.0*B*T + C*T*T);
	  IIR.a2[j] = (4.0*A - 2.0*B*T + C*T*T) / Arg;
	  IIR.a1[j] = (2.0*C*T*T - 8.0*A) / Arg;
	  IIR.a0[j] = 1.0;     // The filter implementation depends on a0 = 1.

	  // With all pole filters, our LPF numerator is (z+1)^2, so all our Z Plane zeros are at -1
	  IIR.b2[j] = (4.0*D - 2.0*E*T + F*T*T) / Arg * C/F;
	  IIR.b1[j] = (2.0*F*T*T - 8.0*D) / Arg * C/F;
	  IIR.b0[j] = (4*D + F*T*T + 2.0*E*T) / Arg * C/F;
	 }
   }

  if(PassType == iirHPF)
   {
	if(A == 0.0 && D == 0.0) // 1 pole
	 {
	  Arg = 2.0*C + B*T;
	  IIR.a2[j] = 0.0;
	  IIR.a1[j] = (B*T - 2.0*C) / Arg;
	  IIR.a0[j] = 1.0;     // The filter implementation depends on a0 = 1.

	  IIR.b2[j] = 0.0;
	  IIR.b1[j] = (E*T - 2.0*F) / Arg * C/F;
	  IIR.b0[j] = (E*T + 2.0*F) / Arg * C/F;
	 }
	else  // 2 poles
	 {
	  Arg = A*T*T + 4.0*C + 2.0*B*T;
	  IIR.a2[j] = (A*T*T + 4.0*C - 2.0*B*T) / Arg;
	  IIR.a1[j] = (2.0*A*T*T - 8.0*C) / Arg;
	  IIR.a0[j] = 1.0;

	  // With all pole filters, our HPF numerator is (z-1)^2, so all our Z Plane zeros are at 1
	  IIR.b2[j] = (D*T*T - 2.0*E*T + 4.0*F) / Arg * C/F;
	  IIR.b1[j] = (2.0*D*T*T - 8.0*F) / Arg * C/F;
	  IIR.b0[j] = (D*T*T + 4.0*F + 2.0*E*T) / Arg * C/F;
	 }
   }

  if(PassType == iirBPF)
   {
	if(A == 0.0 && D == 0.0) // 1 pole
	 {
	  Arg = 4.0*B*Q + 2.0*C*T + B*Q*T*T;
	  a2[k] = (B*Q*T*T + 4.0*B*Q - 2.0*C*T) / Arg;
	  a1[k] = (2.0*B*Q*T*T - 8.0*B*Q) / Arg ;
	  a0[k] = 1.0;

	  b2[k] = (E*Q*T*T + 4.0*E*Q - 2.0*F*T) / Arg * C/F;
	  b1[k] = (2.0*E*Q*T*T - 8.0*E*Q) / Arg * C/F;
	  b0[k] = (4.0*E*Q + 2.0*F*T + E*Q*T*T) / Arg * C/F;
	  k++;
	 }
	else //2 Poles
	 {
	  IIR.a4[j] = (16.0*A*Q*Q + A*Q*Q*T*T*T*T + 8.0*A*Q*Q*T*T - 2.0*B*Q*T*T*T - 8.0*B*Q*T + 4.0*C*T*T) * F;
	  IIR.a3[j] = (4.0*T*T*T*T*A*Q*Q - 4.0*Q*T*T*T*B + 16.0*Q*B*T - 64.0*A*Q*Q) * F;
	  IIR.a2[j] = (96.0*A*Q*Q - 16.0*A*Q*Q*T*T + 6.0*A*Q*Q*T*T*T*T - 8.0*C*T*T) * F;
	  IIR.a1[j] = (4.0*T*T*T*T*A*Q*Q + 4.0*Q*T*T*T*B - 16.0*Q*B*T - 64.0*A*Q*Q) * F;
	  IIR.a0[j] = (16.0*A*Q*Q + A*Q*Q*T*T*T*T + 8.0*A*Q*Q*T*T + 2.0*B*Q*T*T*T + 8.0*B*Q*T + 4.0*C*T*T) * F;

	  // With all pole filters, our BPF numerator is (z-1)^2 * (z+1)^2 so the zeros come back as +/- 1 pairs
	  IIR.b4[j] = (8.0*D*Q*Q*T*T - 8.0*E*Q*T + 16.0*D*Q*Q - 2.0*E*Q*T*T*T + D*Q*Q*T*T*T*T + 4.0*F*T*T) * C;
	  IIR.b3[j] = (16.0*E*Q*T - 4.0*E*Q*T*T*T - 64.0*D*Q*Q + 4.0*D*Q*Q*T*T*T*T) * C;
	  IIR.b2[j] = (96.0*D*Q*Q - 8.0*F*T*T + 6.0*D*Q*Q*T*T*T*T - 16.0*D*Q*Q*T*T) * C;
	  IIR.b1[j] = (4.0*D*Q*Q*T*T*T*T - 64.0*D*Q*Q + 4.0*E*Q*T*T*T - 16.0*E*Q*T) * C;
	  IIR.b0[j] = (16.0*D*Q*Q + 8.0*E*Q*T + 8.0*D*Q*Q*T*T + 2.0*E*Q*T*T*T + 4.0*F*T*T + D*Q*Q*T*T*T*T) * C;

      // T = 2 makes these values approach 0.0 (~ 1.0E-12) The root solver needs 0.0 for numerical reasons.
      if(fabs(T-2.0) < 0.0005)
       {
        IIR.a3[j] = 0.0;
        IIR.a1[j] = 0.0;
        IIR.b3[j] = 0.0;
        IIR.b1[j] = 0.0;
       }

      // We now have a 4th order poly in the form a4*s^4 + a3*s^3 + a2*s^2 + a2*s + a0
	  // We find the roots of this so we can form two 2nd order polys.
	  Coeff[0] = IIR.a4[j];
	  Coeff[1] = IIR.a3[j];
	  Coeff[2] = IIR.a2[j];
	  Coeff[3] = IIR.a1[j];
	  Coeff[4] = IIR.a0[j];
      QuadCubicRoots(4, Coeff, RealRoot, ImagRoot);

	  // In effect, the root finder scales the poly by 1/a4 so we have to apply
	  // this factor back into the two 2nd order equations we are forming.
	  if(IIR.a4[j] < 0.0)Scalar = -sqrt(-IIR.a4[j]);
	  else               Scalar = sqrt(IIR.a4[j]);

      // Form the 2nd order polys from the roots.
	  a2[k] = Scalar;
	  a1[k] = -(RealRoot[0] + RealRoot[1]) * Scalar;
	  a0[k] =  (RealRoot[0] * RealRoot[1] - ImagRoot[0] * ImagRoot[1]) * Scalar;
	  k++;
	  a2[k] = Scalar;
	  a1[k] = -(RealRoot[2] + RealRoot[3]) * Scalar;
	  a0[k] =  (RealRoot[2] * RealRoot[3] - ImagRoot[2] * ImagRoot[3]) * Scalar;
	  k--;

	  // Now do the same with the numerator.
	  Coeff[0] = IIR.b4[j];
	  Coeff[1] = IIR.b3[j];
	  Coeff[2] = IIR.b2[j];
	  Coeff[3] = IIR.b1[j];
	  Coeff[4] = IIR.b0[j];
	  QuadCubicRoots(4, Coeff, RealRoot, ImagRoot);

	  if(IIR.b4[j] < 0.0)Scalar = -sqrt(-IIR.b4[j]);
	  else               Scalar = sqrt(IIR.b4[j]);

	  b2[k] = Scalar;
	  if(ProtoType == ftINVERSE_CHEBY || ProtoType >= ftELLIPTIC_00)
	   b1[k] = -(RealRoot[0] + RealRoot[1]) * Scalar;
	  else  // else the prototype is an all pole filter
	   b1[k] = 0.0;  // b1 = 0 for all pole filters, but the addition above won't always equal zero exactly.
	  b0[k] =  (RealRoot[0] * RealRoot[1] - ImagRoot[0] * ImagRoot[1]) * Scalar;
	  k++;

	  b2[k] = Scalar;
	  if(ProtoType == ftINVERSE_CHEBY || ProtoType >= ftELLIPTIC_00)
	   b1[k] = -(RealRoot[2] + RealRoot[3]) * Scalar;
	  else
	   b1[k] = 0.0;
	  b0[k] =  (RealRoot[2] * RealRoot[3] - ImagRoot[2] * ImagRoot[3]) * Scalar;
	  k++;
	  // Go below to see where we store these 2nd order polys back into IIR
	 }
   }

  if(PassType == iirNOTCH)
   {
	if(A == 0.0 && D == 0.0) // 1 pole
	 {
	  Arg = 2.0*B*T + C*Q*T*T + 4.0*C*Q;
	  a2[k] = (4.0*C*Q - 2.0*B*T + C*Q*T*T) / Arg;
	  a1[k] = (2.0*C*Q*T*T - 8.0*C*Q) / Arg;
	  a0[k] = 1.0;

	  b2[k] = (4.0*F*Q - 2.0*E*T + F*Q*T*T) / Arg * C/F;
	  b1[k] = (2.0*F*Q*T*T - 8.0*F*Q) / Arg *C/F;
	  b0[k] = (2.0*E*T + F*Q*T*T +4.0*F*Q) / Arg *C/F;
	  k++;
	 }
	else
	 {
	  IIR.a4[j] = (4.0*A*T*T - 2.0*B*T*T*T*Q + 8.0*C*Q*Q*T*T - 8.0*B*T*Q + C*Q*Q*T*T*T*T + 16.0*C*Q*Q) * -F;
	  IIR.a3[j] = (16.0*B*T*Q + 4.0*C*Q*Q*T*T*T*T - 64.0*C*Q*Q - 4.0*B*T*T*T*Q) * -F;
	  IIR.a2[j] = (96.0*C*Q*Q - 8.0*A*T*T - 16.0*C*Q*Q*T*T + 6.0*C*Q*Q*T*T*T*T) * -F;
	  IIR.a1[j] = (4.0*B*T*T*T*Q - 16.0*B*T*Q - 64.0*C*Q*Q + 4.0*C*Q*Q*T*T*T*T) * -F;
	  IIR.a0[j] = (4.0*A*T*T + 2.0*B*T*T*T*Q + 8.0*C*Q*Q*T*T + 8.0*B*T*Q + C*Q*Q*T*T*T*T + 16.0*C*Q*Q) * -F;

	  // Our Notch Numerator isn't simple. [ (4+T^2)*z^2 - 2*(4-T^2)*z + (4+T^2) ]^2
	  IIR.b4[j] = (2.0*E*T*T*T*Q - 4.0*D*T*T - 8.0*F*Q*Q*T*T + 8.0*E*T*Q - 16.0*F*Q*Q - F*Q*Q*T*T*T*T) * C;
	  IIR.b3[j] = (64.0*F*Q*Q + 4.0*E*T*T*T*Q - 16.0*E*T*Q - 4.0*F*Q*Q*T*T*T*T) * C;
	  IIR.b2[j] = (8.0*D*T*T - 96.0*F*Q*Q + 16.0*F*Q*Q*T*T - 6.0*F*Q*Q*T*T*T*T) * C;
	  IIR.b1[j] = (16.0*E*T*Q - 4.0*E*T*T*T*Q + 64.0*F*Q*Q - 4.0*F*Q*Q*T*T*T*T) * C;
	  IIR.b0[j] = (-4.0*D*T*T - 2.0*E*T*T*T*Q - 8.0*E*T*Q - 8.0*F*Q*Q*T*T - F*Q*Q*T*T*T*T - 16.0*F*Q*Q) * C;

      // T = 2 makes these values approach 0.0 (~ 1.0E-12) The root solver needs 0.0 for numerical reasons.
      if(fabs(T-2.0) < 0.0005)
       {
        IIR.a3[j] = 0.0;
        IIR.a1[j] = 0.0;
        IIR.b3[j] = 0.0;
        IIR.b1[j] = 0.0;
       }

	  // We now have a 4th order poly in the form a4*s^4 + a3*s^3 + a2*s^2 + a2*s + a0
	  // We find the roots of this so we can form two 2nd order polys.
	  Coeff[0] = IIR.a4[j];
	  Coeff[1] = IIR.a3[j];
	  Coeff[2] = IIR.a2[j];
	  Coeff[3] = IIR.a1[j];
	  Coeff[4] = IIR.a0[j];
	  QuadCubicRoots(4, Coeff, RealRoot, ImagRoot);

      // In effect, the root finder scales the poly by 1/a4 so we have to apply
	  // this factor back into the two 2nd order equations we are forming.
	  if(IIR.a4[j] < 0.0)Scalar = -sqrt(-IIR.a4[j]);
	  else               Scalar = sqrt(IIR.a4[j]);
	  a2[k] = Scalar;
	  a1[k] = -(RealRoot[0] + RealRoot[1]) * Scalar;
	  a0[k] =  (RealRoot[0] * RealRoot[1] - ImagRoot[0] * ImagRoot[1]) * Scalar;
	  k++;
	  a2[k] = Scalar;
	  a1[k] = -(RealRoot[2] + RealRoot[3]) * Scalar;
	  a0[k] =  (RealRoot[2] * RealRoot[3] - ImagRoot[2] * ImagRoot[3]) * Scalar;
	  k--;

	  // Now do the same with the numerator.
	  Coeff[0] = IIR.b4[j];
	  Coeff[1] = IIR.b3[j];
	  Coeff[2] = IIR.b2[j];
	  Coeff[3] = IIR.b1[j];
	  Coeff[4] = IIR.b0[j];
	  QuadCubicRoots(4, Coeff, RealRoot, ImagRoot);

	  if(IIR.b4[j] < 0.0)Scalar = -sqrt(-IIR.b4[j]);
	  else               Scalar = sqrt(IIR.b4[j]);

	  b2[k] = Scalar;
	  b1[k] = -(RealRoot[0] + RealRoot[1]) * Scalar;
	  b0[k] =  (RealRoot[0] * RealRoot[1] - ImagRoot[0] * ImagRoot[1]) * Scalar;
	  k++;
	  b2[k] = Scalar;
	  b1[k] = -(RealRoot[2] + RealRoot[3]) * Scalar;
	  b0[k] =  (RealRoot[2] * RealRoot[3] - ImagRoot[2] * ImagRoot[3]) * Scalar;
	  k++;
	 }
   }
 }

 if(PassType == iirBPF || PassType == iirNOTCH)
  {
   // In the calcs above for the BPF and Notch, we didn't set a0=1, so we do it here.
   // k will equal the number of poles.
   for(j=0; j<k; j++)
	{
	 b2[j] /= a0[j];
	 b1[j] /= a0[j];
	 b0[j] /= a0[j];
	 a2[j] /= a0[j];
	 a1[j] /= a0[j];
	 a0[j] = 1.0;
	}

   for(j=0; j<k; j++)
	{
	 IIR.a0[j] = a0[j];
	 IIR.a1[j] = a1[j];
	 IIR.a2[j] = a2[j];
	 IIR.b0[j] = b0[j];
	 IIR.b1[j] = b1[j];
	 IIR.b2[j] = b2[j];
	}
  }

 return(IIR);  // IIR is the structure containing the coefficients.
}

//---------------------------------------------------------------------------


TSPlaneCoeff GetSPlaneCoefficients(TIIRFilterTypes FilterType, int NumPoles, double Beta)
{

/*
 This file was generated by Iowa Hills Software, LLC  IowaHills.com
 It contains the analog filter coefficients needed for IIR filter design.
 
 The first set of arrays given below define the limits for the various filters.
 In order to retrieve a set of filter coefficients, we call the function 
 GetSPlaneCoefficients(FilterType, SPlaneCoeff, NumPoles, Beta),
 FilterType and SPlaneCoeff are defined in the header file. 
 The use of Beta depends on the FilterType 
 Beta is ignored for Butterworth, Gauss, and Bessel. 
 Beta sets the Ripple for Chebyshev. 
 It sets Gamma for Adjustable Gauss, which controls the filters transition bandwidth, 
 and it sets the stop band attenuation for Inv Chebyshev and Elliptic. 
   
 Beta has different upper and lower limits, as well as step sizes.  
 These are defined in BetaMinArray, BetaMaxArray, BetaStepArray  
 In essence, every filter value given in this file 
 can be accessed with these 2 loops and function call.
 
 for(Beta=BetaMin; Beta<=BetaMax; Beta+=BetaStep) 
   for(NumPoles=MinNumPoles; NumPoles<=MaxNumPoles; NumPoles++)
    GetSPlaneCoefficients(FilterType, SPlaneCoeff, NumPoles, Beta) 
  
 As an example BetaMax for a Gauss would be accessed as BetaMaxArray[ftGAUSSIAN] 
 where ftGAUSSIAN is defined by an enum in the header file.
*/  
 const double BetaMinArray[17] = {0.000, 0.000, 0.000, -1.000, 0.000, 10.000, 40.000, 40.000, 40.000, 40.000, 40.000, 40.000, 40.000, 40.000, 40.000, 40.000, 40.000}; 
 const double BetaMaxArray[17] = {1.000, 1.000, 1.000, 1.010, 1.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010, 100.010}; 
 const double BetaStepArray[17] = {10.000, 10.000, 10.000, 0.050, 0.100, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000, 5.000}; 
 const double MinNumPolesArray[17] = {2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4}; 
 const double MaxNumPolesArray[17] = {10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10}; 

/*
 The arrays that follow contain the 2nd order s plane coefficients for analog filters.
 They form H(s) = (D*s^2 + E*s + F) / (A*s^2 + B*s + C)
 The denominators are arranged as: A, B, C, A, B, C, A, .... for the 1st, 2nd, ... sections
 The numerators are arranged as: D, E, F, D, E, F, D, .... for the 1st, 2nd, ... sections
*/
 const double ButterworthDenominator[9][30] = {
/* 2 Poles*/  { 1.000000000000000000 ,  1.414213562373094920 ,  1.000000000000000000  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.999999999999999889 ,  1.000000000000000000  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.847759065022573480 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.765366864730179675 ,  1.000000000000000000  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.618033988749894900 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.618033988749894902 ,  0.999999999999999889  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.931851652578136620 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.414213562373094920 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.517638090205041701 ,  1.000000000000000000  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.801937735804838290 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.246979603717467190 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.445041867912628897 ,  1.000000000000000000  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.961570560806460860 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.662939224605090690 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.111140466039204360 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.390180644032256829 ,  1.000000000000000000  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.879385241571816860 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.532088886237956030 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.999999999999999889 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.347296355333860607 ,  0.999999999999999889  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.975376681190275540 ,  1.000000000000000000 ,  1.000000000000000000 ,  1.782013048376735580 ,  0.999999999999999889 ,  1.000000000000000000 ,  1.414213562373094920 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.907980999479093609 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.312868930080461849 ,  1.000000000000000000  } } ;

 const double GaussDenominator[9][18] = {
/* 2 Poles*/  { 1.000000000000000000 ,  2.562762032325825330 ,  1.923649213602735000  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.507783057404046720 ,  1.000000000000000000 ,  2.742827569184909110 ,  2.761689015929092330  },
/* 4 Poles*/  { 1.000000000000000000 ,  3.250318994205980690 ,  2.795773783517888410 ,  1.000000000000000000 ,  2.832379983413935150 ,  3.622195915802465470  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.770642153639697010 ,  1.000000000000000000 ,  3.397341218447881060 ,  3.399386636205286920 ,  1.000000000000000000 ,  2.886584538482942720 ,  4.510967423167440590  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.741384040948879000 ,  3.603278462996431400 ,  1.000000000000000000 ,  3.504562371502952800 ,  4.068024960946009290 ,  1.000000000000000000 ,  2.927485861970021470 ,  5.440481388960881400  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.991673520784039700 ,  1.000000000000000000 ,  3.890544984247752410 ,  4.146485815647394270 ,  1.000000000000000000 ,  3.587270439345589420 ,  4.784863788846506870 ,  1.000000000000000000 ,  2.959751642986002020 ,  6.402387517098469870  },
/* 8 Poles*/  { 1.000000000000000000 ,  4.166660308167750240 ,  4.418168314605344980 ,  1.000000000000000000 ,  4.007493391330708480 ,  4.742681915065458220 ,  1.000000000000000000 ,  3.653583822536581320 ,  5.540247268775624790 ,  1.000000000000000000 ,  2.986038101923605300 ,  7.390971815484395970  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.189251568101834970 ,  1.000000000000000000 ,  4.312184794410650390 ,  4.928644579985883570 ,  1.000000000000000000 ,  4.102452170962153310 ,  5.380191424731960660 ,  1.000000000000000000 ,  3.708283145276707770 ,  6.327797768441124400 ,  1.000000000000000000 ,  3.007983206451544160 ,  8.402079774180554850  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.548346818975691260 ,  5.234134821752859600 ,  1.000000000000000000 ,  4.431525161860973230 ,  5.483613133328198330 ,  1.000000000000000000 ,  4.181575377663968140 ,  6.051802209656854890 ,  1.000000000000000000 ,  3.754396579842838480 ,  7.142928286935823050 ,  1.000000000000000000 ,  3.026659208542327840 ,  9.432561542636246800  } } ;

 const double BesselDenominator[9][18] = {
/* 2 Poles*/  { 1.000000000000000000 ,  2.200573065902578840 ,  1.614173939458624750  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.322534929442220180 ,  1.000000000000000000 ,  2.094595215162896370 ,  2.095149008724937370  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.738429969795070470 ,  2.042845036572955570 ,  1.000000000000000000 ,  1.989178525126444000 ,  2.567555811512362670  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.502115978124505480 ,  1.000000000000000000 ,  2.761386446285788660 ,  2.421570533797493760 ,  1.000000000000000000 ,  1.915097736490751100 ,  3.080529563482857560  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.141025286183457780 ,  2.569356346243208300 ,  1.000000000000000000 ,  2.761996647565074350 ,  2.849739983065274540 ,  1.000000000000000000 ,  1.860154961558326380 ,  3.623398016288688960  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.683568036750205720 ,  1.000000000000000000 ,  3.222545966157629670 ,  2.943079916781218940 ,  1.000000000000000000 ,  2.756496364791720670 ,  3.318050804792919810 ,  1.000000000000000000 ,  1.818871113781924900 ,  4.196423168516080440  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.514739132863289760 ,  3.162801216014918810 ,  1.000000000000000000 ,  3.273806492390737950 ,  3.356414959073778180 ,  1.000000000000000000 ,  2.747621718928341390 ,  3.814805073735351380 ,  1.000000000000000000 ,  1.785699977684952480 ,  4.790310799369735580  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.856116189429160680 ,  1.000000000000000000 ,  3.613398234874026560 ,  3.526561835606556010 ,  1.000000000000000000 ,  3.303930882657544200 ,  3.792199344586599350 ,  1.000000000000000000 ,  2.734463122897457850 ,  4.325829027348847330 ,  1.000000000000000000 ,  1.756340274805023640 ,  5.390414200854277920  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.854363231731809500 ,  3.772384349249080950 ,  1.000000000000000000 ,  3.683555165116549900 ,  3.920807904602451100 ,  1.000000000000000000 ,  3.322865148707902170 ,  4.250766284447993650 ,  1.000000000000000000 ,  2.720766088345151430 ,  4.854318475651427710 ,  1.000000000000000000 ,  1.731120295407992500 ,  6.002842552520727180  } } ;

 const double AdjGaussDenominator[41][9][30] = {
/* Adj Gauss Gamma = -1.00 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.472642927892390040 ,  1.845810303609127790  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.434232664359946780 ,  1.000000000000000000 ,  2.609031102395401370 ,  2.666211477434296650  },
/* 4 Poles*/  { 1.000000000000000000 ,  3.070544713552994850 ,  2.524039153369317830 ,  1.000000000000000000 ,  2.675721798490597440 ,  3.535562553032200130  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.672708406203873070 ,  1.000000000000000000 ,  3.209435177604524900 ,  3.130059680858111370 ,  1.000000000000000000 ,  2.726928314009328340 ,  4.480780101661694380  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.534449082931794720 ,  3.235160236348995520 ,  1.000000000000000000 ,  3.310725957149912800 ,  3.817417445273886930 ,  1.000000000000000000 ,  2.765567396153095550 ,  5.473375836334643200  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.881514587112356200 ,  1.000000000000000000 ,  3.675359974057057500 ,  3.768406918613481870 ,  1.000000000000000000 ,  3.388859463718067570 ,  4.564015551464658320 ,  1.000000000000000000 ,  2.796048565387218690 ,  6.503192086275783270  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.936203432715994490 ,  3.957548925222866390 ,  1.000000000000000000 ,  3.785839995792505960 ,  4.368934028787021970 ,  1.000000000000000000 ,  3.451505071290088540 ,  5.357220702288691070 ,  1.000000000000000000 ,  2.820881127259688980 ,  7.563394007297634220  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.068164645087352760 ,  1.000000000000000000 ,  4.073678998259618210 ,  4.450976695167204510 ,  1.000000000000000000 ,  3.875546630532830330 ,  5.021267916023388620 ,  1.000000000000000000 ,  3.503178989011403120 ,  6.188791483380960300 ,  1.000000000000000000 ,  2.841612453882321530 ,  8.649077965608979570  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.296778963943849840 ,  4.682819887757939450 ,  1.000000000000000000 ,  4.186418681669548110 ,  5.001370474562040870 ,  1.000000000000000000 ,  3.950293553678128960 ,  5.715797920904162940 ,  1.000000000000000000 ,  3.546741901754185470 ,  7.052888289783208360 ,  1.000000000000000000 ,  2.859255491255694890 ,  9.756565316629723480  } },
/* Adj Gauss Gamma = -0.95 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.447954507553498080 ,  1.824355577673157260  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.425932983485636680 ,  1.000000000000000000 ,  2.545862030334896000 ,  2.587114822064383370  },
/* 4 Poles*/  { 1.000000000000000000 ,  3.038142087804152070 ,  2.482170160041243360 ,  1.000000000000000000 ,  2.586532081899591610 ,  3.390891997621304110  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.655279940268647110 ,  1.000000000000000000 ,  3.134860075006171250 ,  3.023549695738264820 ,  1.000000000000000000 ,  2.607416460597007380 ,  4.222775738296133950  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.480465137988407510 ,  3.146891432727181300 ,  1.000000000000000000 ,  3.200654786450335190 ,  3.637134889105986260 ,  1.000000000000000000 ,  2.626907274295494420 ,  5.103819165259788630  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.852462935194060250 ,  1.000000000000000000 ,  3.579883350409992730 ,  3.609450358526589580 ,  1.000000000000000000 ,  3.241356380748103840 ,  4.276546528428654350 ,  1.000000000000000000 ,  2.636049106594660070 ,  5.984534068458731150  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.860871519203425970 ,  3.816910950857932420 ,  1.000000000000000000 ,  3.652637094515838620 ,  4.132331553562267780 ,  1.000000000000000000 ,  3.279632181333438370 ,  4.968687061794757830 ,  1.000000000000000000 ,  2.647814055549940090 ,  6.908372092072759150  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.032828177538978490 ,  1.000000000000000000 ,  3.965567240689430670 ,  4.251906019402009740 ,  1.000000000000000000 ,  3.707706245220151690 ,  4.692249794190978210 ,  1.000000000000000000 ,  3.311702212203964550 ,  5.691858157129354720 ,  1.000000000000000000 ,  2.657211873372713470 ,  7.848728074675425330  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.210685580169762690 ,  4.506494082248316600 ,  1.000000000000000000 ,  4.038419321728375700 ,  4.720504791730162350 ,  1.000000000000000000 ,  3.753944758344005090 ,  5.285841854780142240 ,  1.000000000000000000 ,  3.338916694420816620 ,  6.441122697843198350 ,  1.000000000000000000 ,  2.664821169202888470 ,  8.802627232805955160  } },
/* Adj Gauss Gamma = -0.90 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.423612372151742630 ,  1.803126734220281910  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.419893579534603930 ,  1.000000000000000000 ,  2.490678985448804280 ,  2.523192758617093470  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.998295020997606920 ,  2.427421682583202680 ,  1.000000000000000000 ,  2.495689640304843860 ,  3.241248526741512310  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.635427479664163460 ,  1.000000000000000000 ,  3.063892861641294370 ,  2.921655022595257110 ,  1.000000000000000000 ,  2.493401711453687500 ,  3.984144867183038400  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.423224063987967190 ,  3.052105101870095090 ,  1.000000000000000000 ,  3.098033025808005100 ,  3.471442723366445550 ,  1.000000000000000000 ,  2.494732036803664510 ,  4.763964023977220740  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.826783734292516610 ,  1.000000000000000000 ,  3.503859096164372740 ,  3.485834452618773230 ,  1.000000000000000000 ,  3.117632854065973460 ,  4.052487598956992620 ,  1.000000000000000000 ,  2.495231126815088630 ,  5.560837846384490750  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.786377434815600740 ,  3.677670702946699510 ,  1.000000000000000000 ,  3.538630957546500260 ,  3.934219236626466110 ,  1.000000000000000000 ,  3.125346301309524840 ,  4.639348246933548300 ,  1.000000000000000000 ,  2.489523116905189240 ,  6.342388108215198270  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.993618843480082340 ,  1.000000000000000000 ,  3.868359964772106530 ,  4.070487462858385360 ,  1.000000000000000000 ,  3.563445861970377050 ,  4.421488299985453450 ,  1.000000000000000000 ,  3.138180011696975580 ,  5.267750228317122920 ,  1.000000000000000000 ,  2.488734616569928450 ,  7.158229265057276970  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.119017319260699140 ,  4.318114828231631290 ,  1.000000000000000000 ,  3.913784639991594980 ,  4.484624081558734150 ,  1.000000000000000000 ,  3.581932561670595480 ,  4.930833620330481000 ,  1.000000000000000000 ,  3.149700386851017610 ,  5.916741711183890470 ,  1.000000000000000000 ,  2.487435497745306370 ,  7.981366140803391570  } },
/* Adj Gauss Gamma = -0.85 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.399610857615820160 ,  1.782122563841813450  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.413060767101348870 ,  1.000000000000000000 ,  2.437560563755960970 ,  2.462301250306447640  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.964487163752494330 ,  2.381932909092383090 ,  1.000000000000000000 ,  2.414350532233909610 ,  3.115229576080961400  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.617352951985903120 ,  1.000000000000000000 ,  3.002488877558453060 ,  2.835806122250154270 ,  1.000000000000000000 ,  2.390111893901802450 ,  3.780170272711409480  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.363403096688994060 ,  2.952807710336597950 ,  1.000000000000000000 ,  3.001895092875964010 ,  3.317356601664195100 ,  1.000000000000000000 ,  2.368927810667843750 ,  4.451588981374053230  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.793842465907693700 ,  1.000000000000000000 ,  3.421819746307307230 ,  3.347470054304594990 ,  1.000000000000000000 ,  2.995685116017108300 ,  3.831110854453087190 ,  1.000000000000000000 ,  2.355831817424043350 ,  5.149136564390635050  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.706485323840324590 ,  3.529045096201070210 ,  1.000000000000000000 ,  3.432291580044278590 ,  3.747997537957938440 ,  1.000000000000000000 ,  2.981187798216406560 ,  4.342039539400309070 ,  1.000000000000000000 ,  2.339406010052098580 ,  5.827381071167982540  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.949706170648024100 ,  1.000000000000000000 ,  3.770593754023223720 ,  3.885441297222932190 ,  1.000000000000000000 ,  3.431846446518994840 ,  4.176707463080366440 ,  1.000000000000000000 ,  2.975190454065830710 ,  4.885861310657315220 ,  1.000000000000000000 ,  2.329394643214455220 ,  6.532891255323471040  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.018504803514540310 ,  4.113797955754502130 ,  1.000000000000000000 ,  3.795999434227610880 ,  4.257113291232743180 ,  1.000000000000000000 ,  3.425453222844484280 ,  4.616874482596364790 ,  1.000000000000000000 ,  2.971297311489189850 ,  5.444737962395182150 ,  1.000000000000000000 ,  2.320156333774356040 ,  7.241034836308695160  } },
/* Adj Gauss Gamma = -0.80 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.381224269880960250 ,  1.769178699386812830  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.402382099411000780 ,  1.000000000000000000 ,  2.381008939829647900 ,  2.393339939992147780  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.930054043691392880 ,  2.335006527235869540 ,  1.000000000000000000 ,  2.336620206222923990 ,  2.997003958580190640  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.597837160840871680 ,  1.000000000000000000 ,  2.943177435268639290 ,  2.751917513933508360 ,  1.000000000000000000 ,  2.291683465644842600 ,  3.590882402179219390  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.309023923496859250 ,  2.863491926590111540 ,  1.000000000000000000 ,  2.917841002989212120 ,  3.186750478039952930 ,  1.000000000000000000 ,  2.254417471568210370 ,  4.183193225242380460  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.758830683053740800 ,  1.000000000000000000 ,  3.340897106971308710 ,  3.209993657185080010 ,  1.000000000000000000 ,  2.881994969041745680 ,  3.627807885679984250 ,  1.000000000000000000 ,  2.223512037349051340 ,  4.772951319594728400  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.631156418730947520 ,  3.390913933830437270 ,  1.000000000000000000 ,  3.338774107009006900 ,  3.585516706262419360 ,  1.000000000000000000 ,  2.853339206335043340 ,  4.090936753670010970 ,  1.000000000000000000 ,  2.202052510959449980 ,  5.383027831581705060  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.903101173168628040 ,  1.000000000000000000 ,  3.671957032605194900 ,  3.698704412532067830 ,  1.000000000000000000 ,  3.310193434874210360 ,  3.949985698453083670 ,  1.000000000000000000 ,  2.822962551122077280 ,  4.542275729240322410 ,  1.000000000000000000 ,  2.178737359521891740 ,  5.966841637862939860  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.913012788941640620 ,  3.903475039742210660 ,  1.000000000000000000 ,  3.681592906423093670 ,  4.033633846797770590 ,  1.000000000000000000 ,  3.282800480933546260 ,  4.334062482650949730 ,  1.000000000000000000 ,  2.803945690396624000 ,  5.021139649676627850 ,  1.000000000000000000 ,  2.162404621994630460 ,  6.573847726560098970  } },
/* Adj Gauss Gamma = -0.75 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.357847161676986940 ,  1.748545879432746860  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.394272347252724090 ,  1.000000000000000000 ,  2.331714518604919740 ,  2.337834266919029960  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.888559167917297280 ,  2.276648372041147450 ,  1.000000000000000000 ,  2.257234427816346310 ,  2.872846608302855210  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.573675951957542280 ,  1.000000000000000000 ,  2.879078815078186530 ,  2.657581166871577860 ,  1.000000000000000000 ,  2.193051340087733610 ,  3.399682153448690960  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.253038600720587590 ,  2.772010267393217210 ,  1.000000000000000000 ,  2.838222770853327860 ,  3.062591996452016300 ,  1.000000000000000000 ,  2.145584455583820830 ,  3.935860849996367870  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.726339871785363520 ,  1.000000000000000000 ,  3.268075419332498570 ,  3.087548696371357870 ,  1.000000000000000000 ,  2.781950673551492500 ,  3.454888391117747480 ,  1.000000000000000000 ,  2.102823107414510510 ,  4.449280382392353240  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.545415780002689310 ,  3.235777422476033390 ,  1.000000000000000000 ,  3.241528661598995330 ,  3.412191153387690520 ,  1.000000000000000000 ,  2.728330064747419570 ,  3.844478800770095180 ,  1.000000000000000000 ,  2.067109687746174540 ,  4.955650564785603510  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.859148139606466850 ,  1.000000000000000000 ,  3.580829093615543800 ,  3.528470582484635630 ,  1.000000000000000000 ,  3.203292209119840810 ,  3.752920297377133620 ,  1.000000000000000000 ,  2.687462728524295570 ,  4.251488935424467820 ,  1.000000000000000000 ,  2.040931827196828420 ,  5.479228895724366670  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.804957234972142110 ,  3.693065691166041110 ,  1.000000000000000000 ,  3.569250585590270310 ,  3.814140781977796380 ,  1.000000000000000000 ,  3.151567203156121270 ,  4.074043789330019650 ,  1.000000000000000000 ,  2.647895446469992200 ,  4.641484980161404650 ,  1.000000000000000000 ,  2.013663435887698580 ,  5.972858919620530390  } },
/* Adj Gauss Gamma = -0.70 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.334794782007852290 ,  1.728134519602128490  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.385652571679067970 ,  1.000000000000000000 ,  2.284114131150891240 ,  2.284554883049108030  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.853183793448981210 ,  2.227895686146070810 ,  1.000000000000000000 ,  2.186349296601639570 ,  2.768474335787811660  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.552255298914841260 ,  1.000000000000000000 ,  2.822973224959371750 ,  2.576860118614552330 ,  1.000000000000000000 ,  2.104119356101846080 ,  3.236693510682560950  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.188708699511884510 ,  2.667411752363306300 ,  1.000000000000000000 ,  2.756157884599628630 ,  2.930451466079868170 ,  1.000000000000000000 ,  2.037720780900886820 ,  3.691134813842350320  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.692885761646755420 ,  1.000000000000000000 ,  3.195677996017058930 ,  2.965879927575694630 ,  1.000000000000000000 ,  2.688077103392432670 ,  3.292943076485123300 ,  1.000000000000000000 ,  1.988428918785540620 ,  4.152867478994699550  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.466293937753038620 ,  3.095551432664411800 ,  1.000000000000000000 ,  3.154383651083592980 ,  3.258630603043262130 ,  1.000000000000000000 ,  2.617938343003058940 ,  3.634500303911234460 ,  1.000000000000000000 ,  1.943944448780980270 ,  4.588120779637680610  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.814277538827270720 ,  1.000000000000000000 ,  3.489376487299582500 ,  3.359622232134797940 ,  1.000000000000000000 ,  3.101825635277955410 ,  3.564252296569315080 ,  1.000000000000000000 ,  2.561791228582790400 ,  3.987564776456384410 ,  1.000000000000000000 ,  1.910546328864857650 ,  5.036929805412266430  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.704192359197679090 ,  3.501835941265656780 ,  1.000000000000000000 ,  3.466394540591362980 ,  3.616012975603038800 ,  1.000000000000000000 ,  3.036213350833670170 ,  3.848221395420014620 ,  1.000000000000000000 ,  2.508802573311286640 ,  4.320058405535340820 ,  1.000000000000000000 ,  1.877691547971163550 ,  5.456245471264829840  } },
/* Adj Gauss Gamma = -0.65 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.312061824457295690 ,  1.707943256544226300  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.376595034317244390 ,  1.000000000000000000 ,  2.238108420531614850 ,  2.233308488891013520  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.817448823424187680 ,  2.178550976841739480 ,  1.000000000000000000 ,  2.118555228704304130 ,  2.669869522984726460  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.530209301865661730 ,  1.000000000000000000 ,  2.768074402510442680 ,  2.497375264177884800 ,  1.000000000000000000 ,  2.019585154844679930 ,  3.084744463082055970  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.138010546763903450 ,  2.586741067527687840 ,  1.000000000000000000 ,  2.689768775216924370 ,  2.829251327171171670 ,  1.000000000000000000 ,  1.944487707774029680 ,  3.497350939829275700  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.658832290158554600 ,  1.000000000000000000 ,  3.123755129659953500 ,  2.845631455989347640 ,  1.000000000000000000 ,  2.599597721015312950 ,  3.140109539835322930 ,  1.000000000000000000 ,  1.880221754032175510 ,  3.881384947224798320  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.386433449388502210 ,  2.956752524191435420 ,  1.000000000000000000 ,  3.069415997297872070 ,  3.108852257554957980 ,  1.000000000000000000 ,  2.515004302193204480 ,  3.439745029103038920 ,  1.000000000000000000 ,  1.827527731556857660 ,  4.253472680811436920  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.768998550029637460 ,  1.000000000000000000 ,  3.398094715228873410 ,  3.193762403990210160 ,  1.000000000000000000 ,  3.004635388532380440 ,  3.382677505480321760 ,  1.000000000000000000 ,  2.445347888165523730 ,  3.746357124628119540 ,  1.000000000000000000 ,  1.787382792642147500 ,  4.636079465645161160  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.603070730887741210 ,  3.314737387312359560 ,  1.000000000000000000 ,  3.364898770416996590 ,  3.422697437107708130 ,  1.000000000000000000 ,  2.927610716989099690 ,  3.634046027463132770 ,  1.000000000000000000 ,  2.380210298217263260 ,  4.029714183236907670 ,  1.000000000000000000 ,  1.749375398027450460 ,  4.989916556354220490  } },
/* Adj Gauss Gamma = -0.60 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.289643070741982810 ,  1.687970694247545200  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.364041780874695190 ,  1.000000000000000000 ,  2.188599264839745650 ,  2.173967238514857400  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.787807278644240490 ,  2.138606296115656670 ,  1.000000000000000000 ,  2.058385883927970280 ,  2.588311512999794670  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.511146993159850240 ,  1.000000000000000000 ,  2.720440222189540510 ,  2.430191412612617170 ,  1.000000000000000000 ,  1.943685272876949810 ,  2.956255190520362230  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.079665644945686510 ,  2.494518479597396700 ,  1.000000000000000000 ,  2.619957548142241690 ,  2.718724081251951310 ,  1.000000000000000000 ,  1.851916313448954910 ,  3.302832376088150830  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.624450331493455750 ,  1.000000000000000000 ,  3.052409342615052260 ,  2.727385793252300240 ,  1.000000000000000000 ,  2.515818631324990400 ,  2.995006833163588400 ,  1.000000000000000000 ,  1.778070834945466800 ,  3.632582921853454660  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.306380846566884470 ,  2.820508043402437880 ,  1.000000000000000000 ,  2.986371069873645200 ,  2.963085472476321150 ,  1.000000000000000000 ,  2.418692235851365170 ,  3.257738872366551690 ,  1.000000000000000000 ,  1.717745716023639350 ,  3.948807053409105090  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.723669122712836900 ,  1.000000000000000000 ,  3.307399779511484890 ,  3.032052676312740220 ,  1.000000000000000000 ,  2.910981352333994020 ,  3.207711154636027300 ,  1.000000000000000000 ,  2.337334180207572220 ,  3.524190297942052300 ,  1.000000000000000000 ,  1.671296821829233670 ,  4.273043590745298380  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.502324877169442630 ,  3.133224419856883270 ,  1.000000000000000000 ,  3.264938801238955080 ,  3.235299232052041060 ,  1.000000000000000000 ,  2.824435596325215680 ,  3.429795570492213840 ,  1.000000000000000000 ,  2.261553470375180730 ,  3.765711219875403690 ,  1.000000000000000000 ,  1.628491684059468760 ,  4.569381034537498110  } },
/* Adj Gauss Gamma = -0.55 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.272640446549953900 ,  1.675738352171904920  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.354299279352597420 ,  1.000000000000000000 ,  2.145597451924500460 ,  2.126480826560076750  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.751559090441070500 ,  2.088581339123521730 ,  1.000000000000000000 ,  1.996162048857919040 ,  2.499371897148543020  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.488282085873212650 ,  1.000000000000000000 ,  2.667491317969268310 ,  2.352943885629723560 ,  1.000000000000000000 ,  1.867210076502922260 ,  2.822834934066896120  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.021131432006681990 ,  2.403337018925322700 ,  1.000000000000000000 ,  2.552607005427140590 ,  2.611782029019320550 ,  1.000000000000000000 ,  1.764409145102730610 ,  3.122742700907793710  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.593584081445539760 ,  1.000000000000000000 ,  2.988581643178579220 ,  2.623595034866839540 ,  1.000000000000000000 ,  2.441721381555979950 ,  2.869737827883354960 ,  1.000000000000000000 ,  1.685660929488351560 ,  3.419911249173300580  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.233917191330062390 ,  2.699893314330497060 ,  1.000000000000000000 ,  2.911759924504622620 ,  2.834526172255488420 ,  1.000000000000000000 ,  2.333522323439686020 ,  3.100687642945958440 ,  1.000000000000000000 ,  1.618157255287953070 ,  3.688115216619236580  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.678548251933679400 ,  1.000000000000000000 ,  3.217625556310656610 ,  2.875303519264412520 ,  1.000000000000000000 ,  2.820405866678426410 ,  3.039261962318279940 ,  1.000000000000000000 ,  2.236864019072084990 ,  3.318025025149137000 ,  1.000000000000000000 ,  1.562171009578118940 ,  3.944354260734040720  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.410229733155101690 ,  2.971704809583176840 ,  1.000000000000000000 ,  3.173919382841441370 ,  3.068547905612467820 ,  1.000000000000000000 ,  2.732001692475174440 ,  3.249432387597720240 ,  1.000000000000000000 ,  2.156918667060467860 ,  3.539883009467136080 ,  1.000000000000000000 ,  1.518338886003379070 ,  4.209559244832062670  } },
/* Adj Gauss Gamma = -0.50 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.250797097129737080 ,  1.656127570054386710  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.347392285791466640 ,  1.000000000000000000 ,  2.108801363955850050 ,  2.090204882994652460  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.715200001320419170 ,  2.038600635353307930 ,  1.000000000000000000 ,  1.936558345372938920 ,  2.414817021829911430  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.468586371850714830 ,  1.000000000000000000 ,  2.621499361938985030 ,  2.287607766724776060 ,  1.000000000000000000 ,  1.798690683111726640 ,  2.710151294473966300  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.969447491816326500 ,  2.324286407072838840 ,  1.000000000000000000 ,  2.493187690372433710 ,  2.519799820876691410 ,  1.000000000000000000 ,  1.685622423908683000 ,  2.969322356057950700  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.559053448291014330 ,  1.000000000000000000 ,  2.918611110785205740 ,  2.510246868539105060 ,  1.000000000000000000 ,  2.365516812442630010 ,  2.736892196825997380 ,  1.000000000000000000 ,  1.594905138218720710 ,  3.209261524979065250  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.161730688216446870 ,  2.582176304606476070 ,  1.000000000000000000 ,  2.838626045833452240 ,  2.709496166453813080 ,  1.000000000000000000 ,  2.253148288759201810 ,  2.951810613546545170 ,  1.000000000000000000 ,  1.524522276900598830 ,  3.449887078316394180  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.637574515398239730 ,  1.000000000000000000 ,  3.136211022812001480 ,  2.736567662329580490 ,  1.000000000000000000 ,  2.738895879955444810 ,  2.890600893445260940 ,  1.000000000000000000 ,  2.147972635089406610 ,  3.139839499269332810 ,  1.000000000000000000 ,  1.463231377273713550 ,  3.663431949025563130  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.319071005755126700 ,  2.815910725092723330 ,  1.000000000000000000 ,  3.084453844781983810 ,  2.907664958374192480 ,  1.000000000000000000 ,  2.643162416149047460 ,  3.076399383846680100 ,  1.000000000000000000 ,  2.060062494737642250 ,  3.330844029621979900 ,  1.000000000000000000 ,  1.414913098358346000 ,  3.884525366614372640  } },
/* Adj Gauss Gamma = -0.45 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.234308314378165240 ,  1.644163008575436180  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.337145165285533290 ,  1.000000000000000000 ,  2.068354380416155710 ,  2.045625355573435390  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.685031091195142940 ,  1.998089224584835840 ,  1.000000000000000000 ,  1.883802972894032650 ,  2.345154652105280400  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.445344695636078040 ,  1.000000000000000000 ,  2.570256470695539970 ,  2.212915280096728220 ,  1.000000000000000000 ,  1.729495388405067670 ,  2.592012833264552010  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.917787363536840760 ,  2.246366803432916730 ,  1.000000000000000000 ,  2.435555713038905170 ,  2.430349665868757110 ,  1.000000000000000000 ,  1.611140049327741460 ,  2.826447190034884200  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.528212842722458610 ,  1.000000000000000000 ,  2.856150823786943520 ,  2.411198260006835350 ,  1.000000000000000000 ,  2.297845437558262200 ,  2.621757010263862760 ,  1.000000000000000000 ,  1.513116657594005600 ,  3.029306627675677710  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.082930300253624360 ,  2.456373156469588540 ,  1.000000000000000000 ,  2.760571875782896530 ,  2.576310565147997170 ,  1.000000000000000000 ,  2.171958849164528530 ,  2.797216958280384520 ,  1.000000000000000000 ,  1.433356505725482450 ,  3.216960941830165680  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.596991656814050090 ,  1.000000000000000000 ,  3.055879562155714880 ,  2.602532855888805670 ,  1.000000000000000000 ,  2.659702650407095530 ,  2.747370555347330430 ,  1.000000000000000000 ,  2.064597552199714860 ,  2.972061944647337750 ,  1.000000000000000000 ,  1.370579382089375460 ,  3.408131288734166820  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.221692431173249550 ,  2.653940202132835770 ,  1.000000000000000000 ,  2.989781377099255180 ,  2.740355241553007030 ,  1.000000000000000000 ,  2.551654421824853360 ,  2.897242059175336500 ,  1.000000000000000000 ,  1.965528491263625940 ,  3.121585670998045180 ,  1.000000000000000000 ,  1.315103616816994950 ,  3.574341691611985010  } },
/* Adj Gauss Gamma = -0.40 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.213022414518393880 ,  1.624910303951199180  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.326709083544036050 ,  1.000000000000000000 ,  2.029127002463898770 ,  2.002433249195621820  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.648601505917534290 ,  1.948478340848933230 ,  1.000000000000000000 ,  1.828913045976454880 ,  2.268017507811367130  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.425376210521476890 ,  1.000000000000000000 ,  2.525761347070382360 ,  2.149794061688870880 ,  1.000000000000000000 ,  1.667606506029517540 ,  2.492347495409042770  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.859612809827034140 ,  2.159727828303517060 ,  1.000000000000000000 ,  2.374021022157247080 ,  2.332485259701400080 ,  1.000000000000000000 ,  1.537154863901336150 ,  2.680561468581565520  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.497521691971045450 ,  1.000000000000000000 ,  2.794469138124480610 ,  2.314720968050416160 ,  1.000000000000000000 ,  2.232831776448969130 ,  2.510972619858475860 ,  1.000000000000000000 ,  1.436220582433783250 ,  2.862925743105904530  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.012096245047980240 ,  2.345964213368109790 ,  1.000000000000000000 ,  2.690505280713350710 ,  2.459505528271763720 ,  1.000000000000000000 ,  2.099659934028327960 ,  2.662728600262003640 ,  1.000000000000000000 ,  1.351255106287001520 ,  3.017887825875948500  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.556909039196427310 ,  1.000000000000000000 ,  2.976791586804723890 ,  2.473446252890256770 ,  1.000000000000000000 ,  2.582723468634455340 ,  2.609682474677645030 ,  1.000000000000000000 ,  1.986064435017587340 ,  2.813509942631538860 ,  1.000000000000000000 ,  1.284048135635883270 ,  3.175768596815835560  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.133340696127841610 ,  2.511115882025079400 ,  1.000000000000000000 ,  2.903975639691916740 ,  2.592775522393450770 ,  1.000000000000000000 ,  2.469166772439135290 ,  2.739385679835077520 ,  1.000000000000000000 ,  1.881702479362539430 ,  2.939812539526327040 ,  1.000000000000000000 ,  1.225061424977939460 ,  3.309905401442357230  } },
/* Adj Gauss Gamma = -0.35 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.192026886224488710 ,  1.605870188222335180  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.316117387347370380 ,  1.000000000000000000 ,  1.991062374803061590 ,  1.960542700362955860  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.618400965024384690 ,  1.908286806368564070 ,  1.000000000000000000 ,  1.780425376568678610 ,  2.204653527220881590  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.405358418429417670 ,  1.000000000000000000 ,  2.481965565574370650 ,  2.087801701705356280 ,  1.000000000000000000 ,  1.608707250559967990 ,  2.398156040817792430  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.815086533436324420 ,  2.094876638007540760 ,  1.000000000000000000 ,  2.325115283499981090 ,  2.258935223690847320 ,  1.000000000000000000 ,  1.474223176996906300 ,  2.568381649465771410  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.467058245442525390 ,  1.000000000000000000 ,  2.733643818438803660 ,  2.220989145941533850 ,  1.000000000000000000 ,  2.170238645539779300 ,  2.404358001574564250 ,  1.000000000000000000 ,  1.363973369430716120 ,  2.708747167682069360  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.949033946552244160 ,  2.249820684586528950 ,  1.000000000000000000 ,  2.628084372507862200 ,  2.357826430873129380 ,  1.000000000000000000 ,  2.035460611307804420 ,  2.546283245985325740 ,  1.000000000000000000 ,  1.277468799472447180 ,  2.848280732618966300  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.517410447137279570 ,  1.000000000000000000 ,  2.899070786210481200 ,  2.349445587509388030 ,  1.000000000000000000 ,  2.507889212162712770 ,  2.477598887138964030 ,  1.000000000000000000 ,  1.911809873323786050 ,  2.663343608864967130 ,  1.000000000000000000 ,  1.203417888612627220 ,  2.963854090087788770  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.053713765076190080 ,  2.385773685706709560 ,  1.000000000000000000 ,  2.826667040720285850 ,  2.463219116615037230 ,  1.000000000000000000 ,  2.395073954856778100 ,  2.600876016313786730 ,  1.000000000000000000 ,  1.807347340649761100 ,  2.781912307934554910 ,  1.000000000000000000 ,  1.144049643150974570 ,  3.084777437742107330  } },
/* Adj Gauss Gamma = -0.30 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.176285686397608110 ,  1.594312761209584650  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.308478282279438740 ,  1.000000000000000000 ,  1.958716852849118560 ,  1.928944170250865400  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.588250458035535220 ,  1.868356091851294920 ,  1.000000000000000000 ,  1.733863286731206040 ,  2.143975540563384910  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.385342908266732340 ,  1.000000000000000000 ,  2.438867235606856370 ,  2.027015173068649200 ,  1.000000000000000000 ,  1.552632682986891940 ,  2.308974542600531700  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.764263038398576010 ,  2.021649307800431840 ,  1.000000000000000000 ,  2.272114848113411250 ,  2.176965502103271440 ,  1.000000000000000000 ,  1.411354659387310530 ,  2.451482962381942960  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.436885447858903040 ,  1.000000000000000000 ,  2.673739477325295160 ,  2.130123958822169870 ,  1.000000000000000000 ,  2.109878659000560930 ,  2.301784820968798770 ,  1.000000000000000000 ,  1.296122696590828790 ,  2.565548483113081700  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.879934984694491270 ,  2.146583624632320660 ,  1.000000000000000000 ,  2.560898144503220840 ,  2.248850578931473000 ,  1.000000000000000000 ,  1.969392483014760220 ,  2.423234924161442370 ,  1.000000000000000000 ,  1.205595969590931520 ,  2.679074572610832480  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.482055480152980830 ,  1.000000000000000000 ,  2.829484679785060130 ,  2.241144743178458580 ,  1.000000000000000000 ,  2.440905625674331340 ,  2.362259423646583920 ,  1.000000000000000000 ,  1.845728315943642840 ,  2.532891161271279670 ,  1.000000000000000000 ,  1.131090933602829420 ,  2.783238690422742410  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.968509209214493350 ,  2.255090235539988890 ,  1.000000000000000000 ,  2.744560580708903960 ,  2.328133550158887570 ,  1.000000000000000000 ,  2.317913034461903890 ,  2.456771012861033390 ,  1.000000000000000000 ,  1.733251332403879410 ,  2.620266249690972770 ,  1.000000000000000000 ,  1.066321345427067470 ,  2.866399120550787490  } },
/* Adj Gauss Gamma = -0.25 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.155821325721037680 ,  1.575624321404387910  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.297649195204150450 ,  1.000000000000000000 ,  1.922761301637730070 ,  1.889291786665744820  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.558195230542351430 ,  1.828773977098242960 ,  1.000000000000000000 ,  1.689127956720698710 ,  2.085790890211192570  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.365373246272869600 ,  1.000000000000000000 ,  2.396467985307062510 ,  1.967499503251111110 ,  1.000000000000000000 ,  1.499226103613904290 ,  2.224392601609941340  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.713905435557323550 ,  1.950259475229036310 ,  1.000000000000000000 ,  2.220501400917350930 ,  2.097491637239680350 ,  1.000000000000000000 ,  1.351930619669807990 ,  2.341696400675681300  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.410412177615793630 ,  1.000000000000000000 ,  2.621049367280139820 ,  2.051962685724247670 ,  1.000000000000000000 ,  2.056498721830953610 ,  2.213681250527968820 ,  1.000000000000000000 ,  1.235355915539837260 ,  2.443872082499892070  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.811913684489966590 ,  2.047251108139869660 ,  1.000000000000000000 ,  2.495228319502167840 ,  2.144085674665042070 ,  1.000000000000000000 ,  1.906098862448917240 ,  2.305760005775509210 ,  1.000000000000000000 ,  1.138501606159128430 ,  2.522830555637199710  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.443836342900040700 ,  1.000000000000000000 ,  2.754626832628217950 ,  2.126951297139942020 ,  1.000000000000000000 ,  2.370089753384902130 ,  2.240870588671264760 ,  1.000000000000000000 ,  1.778616020855157580 ,  2.397270990711374510 ,  1.000000000000000000 ,  1.061286944076069050 ,  2.604924436937434700  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.891969081550130220 ,  2.140834688887496730 ,  1.000000000000000000 ,  2.670794030986397340 ,  2.210004325524526520 ,  1.000000000000000000 ,  2.248664488340604620 ,  2.330769628596964350 ,  1.000000000000000000 ,  1.667245458791458250 ,  2.479603679104528170 ,  1.000000000000000000 ,  0.996800914641164271 ,  2.680334851257873300  } },
/* Adj Gauss Gamma = -0.20 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.140554665462150560 ,  1.564328429674090250  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.289813664883977530 ,  1.000000000000000000 ,  1.892330127493340180 ,  1.859584204691444810  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.528275425350592350 ,  1.789615073685789650 ,  1.000000000000000000 ,  1.646127198176001280 ,  2.029928328774412540  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.348728475807527080 ,  1.000000000000000000 ,  2.360445262329464280 ,  1.918520051449830400 ,  1.000000000000000000 ,  1.451829389879953020 ,  2.154391965004728520  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.670500991950699010 ,  1.889860524981146870 ,  1.000000000000000000 ,  2.175452179715779090 ,  2.030261372407762280 ,  1.000000000000000000 ,  1.298871193438025930 ,  2.249196089679425190  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.380916533377027240 ,  1.000000000000000000 ,  2.563040143986644370 ,  1.966689937895569120 ,  1.000000000000000000 ,  2.000084597034128550 ,  2.118520819545971710 ,  1.000000000000000000 ,  1.175417672712770310 ,  2.319004034756536380  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.751641202399496500 ,  1.961223274567655440 ,  1.000000000000000000 ,  2.436924429077953480 ,  2.053357722668877280 ,  1.000000000000000000 ,  1.849812648930888150 ,  2.204207368389066170 ,  1.000000000000000000 ,  1.078482302614195420 ,  2.389654778802121180  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.409732611133923230 ,  1.000000000000000000 ,  2.687799506437558160 ,  2.027566292858564710 ,  1.000000000000000000 ,  2.306822495785878060 ,  2.135228826202334230 ,  1.000000000000000000 ,  1.718784767653767310 ,  2.279553339175435230 ,  1.000000000000000000 ,  0.998908682568483419 ,  2.452806531529232450  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.817025075099321100 ,  2.031799081030713610 ,  1.000000000000000000 ,  2.598793858421550420 ,  2.097267792441781080 ,  1.000000000000000000 ,  2.181655525773328550 ,  2.210640160042819600 ,  1.000000000000000000 ,  1.604721603287716960 ,  2.346462819458382750 ,  1.000000000000000000 ,  0.932516364531867370 ,  2.510123847540463250  } },
/* Adj Gauss Gamma = -0.15 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.120604941517635570 ,  1.545987486699612830  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.278838611130709510 ,  1.000000000000000000 ,  1.858310549051745970 ,  1.821939455243326570  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.498526514622165800 ,  1.750942354833701750 ,  1.000000000000000000 ,  1.604774980716709760 ,  1.976235132763447980  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.328931277447870320 ,  1.000000000000000000 ,  2.319396337993744780 ,  1.861483646943434380 ,  1.000000000000000000 ,  1.403230806046675160 ,  2.077663648267998650  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.621194067353614270 ,  1.822093580119179770 ,  1.000000000000000000 ,  2.126384209973526840 ,  1.955476511091070350 ,  1.000000000000000000 ,  1.245632871233679270 ,  2.151433760009023200  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.355117601074088180 ,  1.000000000000000000 ,  2.512163294509326850 ,  1.893585619645118840 ,  1.000000000000000000 ,  1.950257373930419650 ,  2.036991629509995680 ,  1.000000000000000000 ,  1.121852882337675660 ,  2.212981629361464360  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.692383998426431280 ,  1.878391444862827160 ,  1.000000000000000000 ,  2.379952703409138780 ,  1.966065840133065510 ,  1.000000000000000000 ,  1.795733802469088540 ,  2.107018954688533620 ,  1.000000000000000000 ,  1.022431450629211370 ,  2.265870354820412660  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.376278244710466000 ,  1.000000000000000000 ,  2.622370148971438390 ,  1.932385600121723670 ,  1.000000000000000000 ,  2.245318041632426540 ,  2.034143925912923480 ,  1.000000000000000000 ,  1.661654014520085320 ,  2.167551592492631850 ,  1.000000000000000000 ,  0.940991345433045967 ,  2.312363141496414090  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.743724543389586930 ,  1.927875997036682110 ,  1.000000000000000000 ,  2.528577686211952890 ,  1.989819066546044460 ,  1.000000000000000000 ,  2.116818067637768590 ,  2.096256264417306610 ,  1.000000000000000000 ,  1.545370561003531760 ,  2.220453520550705220 ,  1.000000000000000000 ,  0.873145727551736361 ,  2.353961234096211720  } },
/* Adj Gauss Gamma = -0.10 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.105798161941948350 ,  1.534950462537536310  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.270886902490407260 ,  1.000000000000000000 ,  1.829639347756641500 ,  1.793928474524014720  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.468979729061500980 ,  1.712808553326710110 ,  1.000000000000000000 ,  1.564990962791305670 ,  1.924574620448880720  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.312483806509044730 ,  1.000000000000000000 ,  2.284643732042447530 ,  1.814708741196200850 ,  1.000000000000000000 ,  1.360211465362873850 ,  2.014413796469905820  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.578828756000755010 ,  1.764953613353526850 ,  1.000000000000000000 ,  2.083667098189092570 ,  1.892410015622692620 ,  1.000000000000000000 ,  1.198196660432450320 ,  2.069242674489721430  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.329684552355554940 ,  1.000000000000000000 ,  2.462206262036802150 ,  1.822904904422842700 ,  1.000000000000000000 ,  1.902043116767158090 ,  1.958508718605914470 ,  1.000000000000000000 ,  1.071506821717533640 ,  2.113525728879404040  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.634187692194970070 ,  1.798735244058495830 ,  1.000000000000000000 ,  2.324314668222262180 ,  1.882180189305790780 ,  1.000000000000000000 ,  1.743732820838610700 ,  2.014061010839688580 ,  1.000000000000000000 ,  0.970092098347148601 ,  2.150555617518005390  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.343495077908541280 ,  1.000000000000000000 ,  2.558367151868278010 ,  1.841337332001256710 ,  1.000000000000000000 ,  2.185547122939933470 ,  1.937529484182639370 ,  1.000000000000000000 ,  1.607036319355653830 ,  2.061036052279658030 ,  1.000000000000000000 ,  0.887238610856572740 ,  2.182368463569784200  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.672099413127808630 ,  1.828932606239778820 ,  1.000000000000000000 ,  2.460152281571056140 ,  1.887525183045614120 ,  1.000000000000000000 ,  2.054089148390425560 ,  1.987464648259701590 ,  1.000000000000000000 ,  1.488940781931049840 ,  2.101232793240752630 ,  1.000000000000000000 ,  0.818358714365790219 ,  2.210281914505029200  } },
/* Adj Gauss Gamma = -0.05 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.091210359713047580 ,  1.524032967648479180  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.259838017573176040 ,  1.000000000000000000 ,  1.797405694476383880 ,  1.758104304384605190  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.445671490653115400 ,  1.683520084662106790 ,  1.000000000000000000 ,  1.530460386014714120 ,  1.884070984551660110  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.296166816075258410 ,  1.000000000000000000 ,  2.250518543804828740 ,  1.769012360552487890 ,  1.000000000000000000 ,  1.319169501942058400 ,  1.954074801064144000  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.537024999338234110 ,  1.709395672011735150 ,  1.000000000000000000 ,  2.042043593806727840 ,  1.831306311028823460 ,  1.000000000000000000 ,  1.153282615157737330 ,  1.991441474606507220  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.307876772565444550 ,  1.000000000000000000 ,  2.419178527514015190 ,  1.763363320219387690 ,  1.000000000000000000 ,  1.859978778981318690 ,  1.892367603586718960 ,  1.000000000000000000 ,  1.026717776181030220 ,  2.030097604536867720  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.583481811992244470 ,  1.730772300939701360 ,  1.000000000000000000 ,  2.275640389297088450 ,  1.810603947824948710 ,  1.000000000000000000 ,  1.697901891029617300 ,  1.934767240449124740 ,  1.000000000000000000 ,  0.923503056136783296 ,  2.053056610715165320  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.314652880999147570 ,  1.000000000000000000 ,  2.502001800989448020 ,  1.763046643821419400 ,  1.000000000000000000 ,  2.132758567445242730 ,  1.854448153939736170 ,  1.000000000000000000 ,  1.558631773393124310 ,  1.969525916735885620 ,  1.000000000000000000 ,  0.839439480064762433 ,  2.072003099376693580  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.608626108774599220 ,  1.743438137808514200 ,  1.000000000000000000 ,  2.399454747003426650 ,  1.799128489760460250 ,  1.000000000000000000 ,  1.998355234566284500 ,  1.893453637127084480 ,  1.000000000000000000 ,  1.438784676967332700 ,  1.998362892766466730 ,  1.000000000000000000 ,  0.769733009451756156 ,  2.088058886649666770  } },
/* Adj Gauss Gamma = 0.00 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.071986012792678800 ,  1.506171718053994190  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.251833740831295840 ,  1.000000000000000000 ,  1.770360254119864420 ,  1.731631924725461680  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.416580329246259760 ,  1.646465590871148970 ,  1.000000000000000000 ,  1.493528780018591680 ,  1.835950285096329400  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.279999999999996250 ,  1.000000000000000000 ,  2.217025033688171830 ,  1.724416000000009270 ,  1.000000000000000000 ,  1.279999999999999580 ,  1.896448000000004570  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.502070631764892190 ,  1.663744849559490420 ,  1.000000000000000000 ,  2.006504857261337540 ,  1.781045374026254620 ,  1.000000000000000000 ,  1.113526187609971800 ,  1.927316735541470210  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.283208020050124580 ,  1.000000000000000000 ,  2.371059251357287680 ,  1.697262711886398550 ,  1.000000000000000000 ,  1.814730185300813980 ,  1.819518219106664340 ,  1.000000000000000000 ,  0.982124899102385762 ,  1.941773726326929910  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.527426413745648140 ,  1.657049690955218150 ,  1.000000000000000000 ,  2.222581487406657170 ,  1.733070520913793100 ,  1.000000000000000000 ,  1.649660431877233660 ,  1.849541189616042520 ,  1.000000000000000000 ,  0.877765981868382950 ,  1.951963776748782740  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.283208020050162100 ,  1.000000000000000000 ,  2.440806698461730040 ,  1.679642905181422780 ,  1.000000000000000000 ,  2.076274191077541960 ,  1.766090603374265380 ,  1.000000000000000000 ,  1.508501499617775470 ,  1.872945834839038200 ,  1.000000000000000000 ,  0.793066171027434041 ,  1.959393533031888570  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.540293625278340790 ,  1.653626301104222710 ,  1.000000000000000000 ,  2.334494143466303130 ,  1.706295812933828150 ,  1.000000000000000000 ,  1.939567829921722280 ,  1.794912638731894330 ,  1.000000000000000000 ,  1.387509265850956020 ,  1.891341562444198270 ,  1.000000000000000000 ,  0.723042952896311597 ,  1.964967082126405580  } },
/* Adj Gauss Gamma = 0.05 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.043898224012146600 ,  1.484964644044175720  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.235821037501122490 ,  1.000000000000000000 ,  1.718497129629561250 ,  1.680719804477722290  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.365197740049370000 ,  1.582288071128143290 ,  1.000000000000000000 ,  1.427276953137589510 ,  1.753555748179748350  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.248182361406524830 ,  1.000000000000000000 ,  2.151944431969893490 ,  1.638574506032507650 ,  1.000000000000000000 ,  1.206891177069300490 ,  1.788636840450782640  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.421458785136097360 ,  1.560279495969800980 ,  1.000000000000000000 ,  1.928411360491566920 ,  1.668096966373425300 ,  1.000000000000000000 ,  1.034874777276467710 ,  1.790301138484756470  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.238308891890746550 ,  1.000000000000000000 ,  2.283574606163466300 ,  1.580236948304776460 ,  1.000000000000000000 ,  1.732921579161924570 ,  1.690948182769282720 ,  1.000000000000000000 ,  0.903022458061486732 ,  1.789025141504679390  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.431186397357476550 ,  1.534254830727848960 ,  1.000000000000000000 ,  2.131301923536785380 ,  1.603967462906919610 ,  1.000000000000000000 ,  1.566474514061438670 ,  1.707899288416459660 ,  1.000000000000000000 ,  0.799210669881070701 ,  1.786566317333086350  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.228720682381357680 ,  1.000000000000000000 ,  2.334733217524036060 ,  1.539908909151036060 ,  1.000000000000000000 ,  1.978314275730917340 ,  1.618117580456464480 ,  1.000000000000000000 ,  1.421713076680189980 ,  1.711614198503829120 ,  1.000000000000000000 ,  0.713750999734900748 ,  1.774637061900257610  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.421128560427470160 ,  1.502686382466180030 ,  1.000000000000000000 ,  2.221212859754806420 ,  1.550287927278215960 ,  1.000000000000000000 ,  1.837143095302510300 ,  1.629403431432967950 ,  1.000000000000000000 ,  1.298615344120374270 ,  1.712145216154157270 ,  1.000000000000000000 ,  0.643598479312877170 ,  1.762883840670305790  } },
/* Adj Gauss Gamma = 0.10 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  2.011846488756007020 ,  1.457280433020789800  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.216807675444128380 ,  1.000000000000000000 ,  1.665241430264669860 ,  1.624153957076470920  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.320939269121208510 ,  1.528306902456714860 ,  1.000000000000000000 ,  1.369301593340785720 ,  1.685408984041400380  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.217140151379508370 ,  1.000000000000000000 ,  2.089413202773326090 ,  1.557229315805767290 ,  1.000000000000000000 ,  1.140169708685414120 ,  1.689758189303897540  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.355791556920142420 ,  1.478592244064475650 ,  1.000000000000000000 ,  1.864064992748599490 ,  1.579089840841449770 ,  1.000000000000000000 ,  0.969233301104167055 ,  1.683864977545557420  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.198216538354223810 ,  1.000000000000000000 ,  2.205597015193841330 ,  1.479327659322013710 ,  1.000000000000000000 ,  1.660616946524450470 ,  1.580557792417601530 ,  1.000000000000000000 ,  0.835129299026181493 ,  1.661155269248385970  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.345327178700989280 ,  1.428666220051490350 ,  1.000000000000000000 ,  2.050134607883459910 ,  1.493059099018553270 ,  1.000000000000000000 ,  1.493310863363094270 ,  1.586811904424010100 ,  1.000000000000000000 ,  0.732491216098609676 ,  1.648902951301514100  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.179895881485760740 ,  1.000000000000000000 ,  2.239805512574450490 ,  1.419867194270052920 ,  1.000000000000000000 ,  1.891091302354690920 ,  1.491150518669530630 ,  1.000000000000000000 ,  1.345538440919501480 ,  1.573938177344976100 ,  1.000000000000000000 ,  0.647051400208160876 ,  1.621251282888251270  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.320298878280699920 ,  1.380610918490895900 ,  1.000000000000000000 ,  2.125387736837094810 ,  1.424134762468471480 ,  1.000000000000000000 ,  1.750648729572366500 ,  1.495694538637132750 ,  1.000000000000000000 ,  1.224110090361108400 ,  1.568013293795099680 ,  1.000000000000000000 ,  0.579017733318635042 ,  1.604226456519580730  } },
/* Adj Gauss Gamma = 0.15 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.985405715090522660 ,  1.437041768392910870  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.200986977783952800 ,  1.000000000000000000 ,  1.618853787173203830 ,  1.578251050306138210  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.271960119838672300 ,  1.468773217827716420 ,  1.000000000000000000 ,  1.312075922655801640 ,  1.613454591747887080  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.190119302145368650 ,  1.000000000000000000 ,  2.034837230043807570 ,  1.488256906420043270 ,  1.000000000000000000 ,  1.082083861949320050 ,  1.607349610441894950  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.292720224823083350 ,  1.402079118230144860 ,  1.000000000000000000 ,  1.803332550560254210 ,  1.496122947328003770 ,  1.000000000000000000 ,  0.910237206649773922 ,  1.587414157824963650  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.162923727263484700 ,  1.000000000000000000 ,  2.137000719707957240 ,  1.393301651729643040 ,  1.000000000000000000 ,  1.597303539345499960 ,  1.486798558160526530 ,  1.000000000000000000 ,  0.777031311008941294 ,  1.554761681164056770  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.263596570332742970 ,  1.331580151497984140 ,  1.000000000000000000 ,  1.973448000596765620 ,  1.391208647086652710 ,  1.000000000000000000 ,  1.425610924721983700 ,  1.476281710842857860 ,  1.000000000000000000 ,  0.674155918890853911 ,  1.526553175684839610  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.136632196111700740 ,  1.000000000000000000 ,  2.155763340169603030 ,  1.317585243975769820 ,  1.000000000000000000 ,  1.814150080103884790 ,  1.383093206306869490 ,  1.000000000000000000 ,  1.279089326114168700 ,  1.457326020976906780 ,  1.000000000000000000 ,  0.591097603847977937 ,  1.494146209463069970  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.231216122054535680 ,  1.277045767389825890 ,  1.000000000000000000 ,  2.040853307583948780 ,  1.317143301783624490 ,  1.000000000000000000 ,  1.674688842563916370 ,  1.382451980380917210 ,  1.000000000000000000 ,  1.159520420258788540 ,  1.446574832649002440 ,  1.000000000000000000 ,  0.525455531886161786 ,  1.473506487829158160  } },
/* Adj Gauss Gamma = 0.20 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.955011069941803740 ,  1.410426996491726870  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.188435564514475070 ,  1.000000000000000000 ,  1.579007438880240420 ,  1.542532166960596470  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.230291891419712510 ,  1.419350665376582170 ,  1.000000000000000000 ,  1.262231036392605700 ,  1.554436889723634700  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.167198532329496660 ,  1.000000000000000000 ,  1.988168558582013020 ,  1.431095526977282260 ,  1.000000000000000000 ,  1.031856180734756820 ,  1.539971591098607820  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.238536673867598470 ,  1.338031775360435600 ,  1.000000000000000000 ,  1.750943650179883180 ,  1.426868900191717640 ,  1.000000000000000000 ,  0.859556679226490039 ,  1.508140400395499240  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.132471996096095170 ,  1.000000000000000000 ,  2.077766401286619050 ,  1.321189237653219940 ,  1.000000000000000000 ,  1.542618869581055870 ,  1.408457109925261410 ,  1.000000000000000000 ,  0.727567028280966421 ,  1.467331232794967910  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.198366033925535760 ,  1.256604632391348670 ,  1.000000000000000000 ,  1.911887900840066430 ,  1.312599192966969360 ,  1.000000000000000000 ,  1.370691198321592140 ,  1.391136595039295100 ,  1.000000000000000000 ,  0.626632548704920245 ,  1.433459122477214850  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.102005440892979580 ,  1.000000000000000000 ,  2.088377538921940780 ,  1.238488910386111860 ,  1.000000000000000000 ,  1.752133978335620460 ,  1.299581622995319870 ,  1.000000000000000000 ,  1.225150079941032640 ,  1.367412515681641020 ,  1.000000000000000000 ,  0.545895409995061809 ,  1.397461287311216220  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.153770408993746170 ,  1.190287029994554490 ,  1.000000000000000000 ,  1.967413609294637270 ,  1.227545004674499780 ,  1.000000000000000000 ,  1.608872029998833050 ,  1.287742351568891500 ,  1.000000000000000000 ,  1.104071730213769920 ,  1.345467373154259900 ,  1.000000000000000000 ,  0.481235222949275954 ,  1.366572689790431340  } },
/* Adj Gauss Gamma = 0.25 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.930123303196933820 ,  1.391136972725917080  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.173050281483297710 ,  1.000000000000000000 ,  1.537328108668855720 ,  1.500809739581109300  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.196055113505441360 ,  1.379787274287642470 ,  1.000000000000000000 ,  1.219290068543239070 ,  1.507617758614933740  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.145277204750396960 ,  1.000000000000000000 ,  1.943986840450275590 ,  1.377630880180403360 ,  1.000000000000000000 ,  0.986088451267590149 ,  1.478303029579666420  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.187162928480758330 ,  1.278621301958237980 ,  1.000000000000000000 ,  1.701928712912695650 ,  1.362886860443694340 ,  1.000000000000000000 ,  0.814063341284966047 ,  1.436387145660833030  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.103762583840346870 ,  1.000000000000000000 ,  2.022134320360541970 ,  1.255007203596223860 ,  1.000000000000000000 ,  1.492017030182331010 ,  1.336889919308810630 ,  1.000000000000000000 ,  0.683810012651832610 ,  1.389006340599753480  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.137355644027700840 ,  1.188411792286210520 ,  1.000000000000000000 ,  1.854627395545233700 ,  1.241187816136789300 ,  1.000000000000000000 ,  1.320414041567192780 ,  1.314177866470466860 ,  1.000000000000000000 ,  0.585197843482141100 ,  1.350876233675325720  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.069836044122675520 ,  1.000000000000000000 ,  2.025882519646633020 ,  1.167219484513328800 ,  1.000000000000000000 ,  1.694988407258318560 ,  1.224450049483335910 ,  1.000000000000000000 ,  1.176278378337293210 ,  1.286953709802681760 ,  1.000000000000000000 ,  0.507047453629015266 ,  1.312482443274824990  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.088026020677104630 ,  1.119039001057580580 ,  1.000000000000000000 ,  1.905044533316190500 ,  1.153992893548561050 ,  1.000000000000000000 ,  1.552982159593230270 ,  1.210087954063826080 ,  1.000000000000000000 ,  1.057196553637875120 ,  1.262886549263407150 ,  1.000000000000000000 ,  0.444996396276803596 ,  1.280457626831561950  } },
/* Adj Gauss Gamma = 0.30 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.905972261276201780 ,  1.372300550087419250  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.157941009289075170 ,  1.000000000000000000 ,  1.497821851627280100 ,  1.461053544664761890  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.157298073513075120 ,  1.334918825670732720 ,  1.000000000000000000 ,  1.176260565936773970 ,  1.456110706015903530  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.124406370090301310 ,  1.000000000000000000 ,  1.902293872459694950 ,  1.327807584895125400 ,  1.000000000000000000 ,  0.944395187571555272 ,  1.421884558040457100  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.138671791316757000 ,  1.223729561094089260 ,  1.000000000000000000 ,  1.656204928363935290 ,  1.303984688160006570 ,  1.000000000000000000 ,  0.773239331548739472 ,  1.371455885526034590  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.080077262240175530 ,  1.000000000000000000 ,  1.976059797295480540 ,  1.201726292915453520 ,  1.000000000000000000 ,  1.449684963082900740 ,  1.279423644683797660 ,  1.000000000000000000 ,  0.647064773274575455 ,  1.326813776171003930  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.086983147848756380 ,  1.133580588880093610 ,  1.000000000000000000 ,  1.807142487770760700 ,  1.183823609706698980 ,  1.000000000000000000 ,  1.278434842821077890 ,  1.252529285519266460 ,  1.000000000000000000 ,  0.550781975938135382 ,  1.285487125045875920  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.046613757805470390 ,  1.000000000000000000 ,  1.980528130680785810 ,  1.117099793574940490 ,  1.000000000000000000 ,  1.652830526857321350 ,  1.171639589941069870 ,  1.000000000000000000 ,  1.139196964062516580 ,  1.230441999579897860 ,  1.000000000000000000 ,  0.476646542657121308 ,  1.253303715075713590  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.040676620483223400 ,  1.069138190632652470 ,  1.000000000000000000 ,  1.859773928095824310 ,  1.102496227601450940 ,  1.000000000000000000 ,  1.511749795143895850 ,  1.155735310716297310 ,  1.000000000000000000 ,  1.021737164535276190 ,  1.205147456547245090 ,  1.000000000000000000 ,  0.416963703986144130 ,  1.220820868036643910  } },
/* Adj Gauss Gamma = 0.35 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.882538331861183690 ,  1.353914109645079970  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.146262738606262930 ,  1.000000000000000000 ,  1.464356434897477130 ,  1.430951485105965930  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.126170011097185510 ,  1.299844886028895810 ,  1.000000000000000000 ,  1.139571830345262440 ,  1.416167955603476750  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.104630475934372580 ,  1.000000000000000000 ,  1.863089526960931330 ,  1.281559357465810400 ,  1.000000000000000000 ,  0.906435799108483931 ,  1.370334696299776220  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.099547506780169660 ,  1.180438693630364360 ,  1.000000000000000000 ,  1.618648164692711560 ,  1.257653132198914390 ,  1.000000000000000000 ,  0.738890567370328877 ,  1.320838289911937750  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.061642428340573430 ,  1.000000000000000000 ,  1.939890613097146940 ,  1.161096932046674630 ,  1.000000000000000000 ,  1.415647322071823090 ,  1.235706350646221810 ,  1.000000000000000000 ,  0.616710964413850982 ,  1.279939976420760630  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.047718732717643150 ,  1.091801367256533430 ,  1.000000000000000000 ,  1.769735670720177900 ,  1.140164254972778620 ,  1.000000000000000000 ,  1.244707111418316800 ,  1.205717397800637250 ,  1.000000000000000000 ,  0.522680683140303026 ,  1.236335399533405430  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.026232809748010230 ,  1.000000000000000000 ,  1.940716656466841440 ,  1.074037079934363260 ,  1.000000000000000000 ,  1.615839338912993920 ,  1.126338389371764230 ,  1.000000000000000000 ,  1.106830337478472930 ,  1.182186072908335550 ,  1.000000000000000000 ,  0.450960661576058408 ,  1.203460392342489360  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.999761910303663460 ,  1.026942831051709250 ,  1.000000000000000000 ,  1.820630953296141550 ,  1.058980128645607530 ,  1.000000000000000000 ,  1.476095788978365020 ,  1.109886776716439980 ,  1.000000000000000000 ,  0.991231907930806511 ,  1.156667644501346180 ,  1.000000000000000000 ,  0.393687437083030334 ,  1.171400853599261270  } },
/* Adj Gauss Gamma = 0.40 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.859802651455323200 ,  1.335974009361949430  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.134959649011210380 ,  1.000000000000000000 ,  1.432787696300707350 ,  1.402550768093373310  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.090526081509781250 ,  1.259569462468114190 ,  1.000000000000000000 ,  1.102428224316695630 ,  1.371252809847336220  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.085989938596700680 ,  1.000000000000000000 ,  1.826374489723932460 ,  1.238816289334645940 ,  1.000000000000000000 ,  0.871909362336328209 ,  1.323337179160352540  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.063649629313895950 ,  1.141398946512703240 ,  1.000000000000000000 ,  1.584372629586892730 ,  1.216016396524070010 ,  1.000000000000000000 ,  0.708324216073485613 ,  1.275964316476385860  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.041946293260440640 ,  1.000000000000000000 ,  1.901682644397134860 ,  1.118490635909610730 ,  1.000000000000000000 ,  1.381027252322050370 ,  1.190108478870705920 ,  1.000000000000000000 ,  0.588423674148609521 ,  1.231894276449790440  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.013548415291805500 ,  1.056097161684392600 ,  1.000000000000000000 ,  1.737125692845809870 ,  1.102909873796843470 ,  1.000000000000000000 ,  1.215308669667454660 ,  1.165941917407445240 ,  1.000000000000000000 ,  0.498702232354999109 ,  1.195107706369918480  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.008844730894788320 ,  1.000000000000000000 ,  1.906710640386762410 ,  1.037986168356020760 ,  1.000000000000000000 ,  1.584153045356645030 ,  1.088479091603822720 ,  1.000000000000000000 ,  1.079090291113994350 ,  1.142030674249237650 ,  1.000000000000000000 ,  0.429452008481731040 ,  1.162495445338915360  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.965648931070400840 ,  0.992426903847616115 ,  1.000000000000000000 ,  1.787909261150791850 ,  1.023412534659281950 ,  1.000000000000000000 ,  1.446163165285495380 ,  1.072481674321334120 ,  1.000000000000000000 ,  0.965575060477366409 ,  1.117288882066112880 ,  1.000000000000000000 ,  0.374582078185105194 ,  1.131743733124943580  } },
/* Adj Gauss Gamma = 0.45 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.837747088097379630 ,  1.318476605183699360  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.120874928483897470 ,  1.000000000000000000 ,  1.399051649521804610 ,  1.368013277137241080  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.062704968081711780 ,  1.229014302706207930 ,  1.000000000000000000 ,  1.071186598901655570 ,  1.337468754100475940  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.071926291023467260 ,  1.000000000000000000 ,  1.797860290730272310 ,  1.207163111715187310 ,  1.000000000000000000 ,  0.843227257199142155 ,  1.288801635324998700  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.031097328249142460 ,  1.106573692985801660 ,  1.000000000000000000 ,  1.553373649129389020 ,  1.179001700232564120 ,  1.000000000000000000 ,  0.681223738946687418 ,  1.236552925078599150  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.027851460569373380 ,  1.000000000000000000 ,  1.873933901934034860 ,  1.088540423049526580 ,  1.000000000000000000 ,  1.354800304454901070 ,  1.158156037560954090 ,  1.000000000000000000 ,  0.565713244441534036 ,  1.198541721476613860  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.984765991380884390 ,  1.026511818766724150 ,  1.000000000000000000 ,  1.709509887067428120 ,  1.072095775023073290 ,  1.000000000000000000 ,  1.190234139951455990 ,  1.133183783746342320 ,  1.000000000000000000 ,  0.478449708888778691 ,  1.161573620253808500  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.994645119988568416 ,  1.000000000000000000 ,  1.878857889816991420 ,  1.009023378958123770 ,  1.000000000000000000 ,  1.557988167368941300 ,  1.058125655874673670 ,  1.000000000000000000 ,  1.055962543284540310 ,  1.109977012631697010 ,  1.000000000000000000 ,  0.411684316983624676 ,  1.130204046731073890  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.946118792425884260 ,  0.973005889700152116 ,  1.000000000000000000 ,  1.768643941019884650 ,  1.003436707577390670 ,  1.000000000000000000 ,  1.427545748980696860 ,  1.051507748939813690 ,  1.000000000000000000 ,  0.948306316170531627 ,  1.095258662219271930 ,  1.000000000000000000 ,  0.360532895767693373 ,  1.110013311802271920  } },
/* Adj Gauss Gamma = 0.50 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.816354224791889570 ,  1.301418271218538700  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.110367716363087620 ,  1.000000000000000000 ,  1.371016947464069660 ,  1.342849130776738950  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.036648359308070870 ,  1.200728309100620010 ,  1.000000000000000000 ,  1.042419459142741590 ,  1.306583008951202720  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.059215129262759620 ,  1.000000000000000000 ,  1.772054279537668140 ,  1.178993870275101050 ,  1.000000000000000000 ,  0.817485698813436801 ,  1.258457933240302080  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.008954645629922990 ,  1.083411221504865680 ,  1.000000000000000000 ,  1.530947870207011170 ,  1.154519257752656180 ,  1.000000000000000000 ,  0.659593180918832012 ,  1.210744728010386910  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.012522336444704020 ,  1.000000000000000000 ,  1.844143956525970740 ,  1.056443750649835870 ,  1.000000000000000000 ,  1.327798049511075050 ,  1.124062250793591570 ,  1.000000000000000000 ,  0.544397491002776790 ,  1.163385382082575690  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.961745009500838410 ,  1.003195654280542470 ,  1.000000000000000000 ,  1.687158883744206640 ,  1.047870955180789430 ,  1.000000000000000000 ,  1.169543592646907100 ,  1.107556690630886460 ,  1.000000000000000000 ,  0.461604927347878280 ,  1.135700952989209480  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.983889953053756261 ,  1.000000000000000000 ,  1.857621901657124040 ,  0.987383938788946258 ,  1.000000000000000000 ,  1.537663409233734240 ,  1.035512406700577650 ,  1.000000000000000000 ,  1.037519548658119020 ,  1.086220658402113900 ,  1.000000000000000000 ,  0.397317401405175408 ,  1.106646021074039420  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.919969512446303650 ,  0.947216542899402159 ,  1.000000000000000000 ,  1.743532861150949520 ,  0.976915241301940185 ,  1.000000000000000000 ,  1.404587239061895640 ,  1.023750468724982540 ,  1.000000000000000000 ,  0.928869594952911659 ,  1.066342443398614610 ,  1.000000000000000000 ,  0.347138717762408511 ,  1.081523310319846050  } },
/* Adj Gauss Gamma = 0.55 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.795607343656289600 ,  1.284795419107275240  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.100307720030251570 ,  1.000000000000000000 ,  1.344630010361775870 ,  1.319239284979904390  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.012408664888596820 ,  1.174721085704068630 ,  1.000000000000000000 ,  1.015991711132645530 ,  1.278514466416143860  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.044349342139075270 ,  1.000000000000000000 ,  1.743054108448071070 ,  1.146475069005711940 ,  1.000000000000000000 ,  0.791797379661884793 ,  1.223837498313538940  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.983736521136145510 ,  1.057151074287089100 ,  1.000000000000000000 ,  1.506721867513032940 ,  1.126820818031784240 ,  1.000000000000000000 ,  0.638679125648942581 ,  1.181909578303657730  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.003292604462106890 ,  1.000000000000000000 ,  1.825647735180014000 ,  1.037422306403392500 ,  1.000000000000000000 ,  1.309541348864635690 ,  1.103995004016065100 ,  1.000000000000000000 ,  0.528141050174889881 ,  1.143046005677017310  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.937278715773827600 ,  0.978656156210878314 ,  1.000000000000000000 ,  1.663836901721271520 ,  1.022402315953630400 ,  1.000000000000000000 ,  1.148817151461500210 ,  1.080747438836059840 ,  1.000000000000000000 ,  0.446155536950777698 ,  1.108857475435587770  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.972897161106589747 ,  1.000000000000000000 ,  1.836035749149923960 ,  0.965518278226021875 ,  1.000000000000000000 ,  1.517362069827178810 ,  1.012711887126339640 ,  1.000000000000000000 ,  1.019725676977267570 ,  1.062408038999000270 ,  1.000000000000000000 ,  0.384516176479262672 ,  1.083213050812177780  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.901807674808911440 ,  0.929547808093437622 ,  1.000000000000000000 ,  1.725829856157699240 ,  0.958787277878162536 ,  1.000000000000000000 ,  1.387936241576443570 ,  1.004851729149493610 ,  1.000000000000000000 ,  0.914232972370696784 ,  1.046793145331250230 ,  1.000000000000000000 ,  0.336768436267223581 ,  1.062655219062969670  } },
/* Adj Gauss Gamma = 0.60 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.775490410788554740 ,  1.268604516687543700  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.090715819838304010 ,  1.000000000000000000 ,  1.319824214446014610 ,  1.297153090388216020  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.990041360285115470 ,  1.151006373824058570 ,  1.000000000000000000 ,  0.991784811547654521 ,  1.253205326278123530  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.034455594293187450 ,  1.000000000000000000 ,  1.722722872806932150 ,  1.125242370947146850 ,  1.000000000000000000 ,  0.771405246394847532 ,  1.201546326355736440  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.962390256333590880 ,  1.035234068039211590 ,  1.000000000000000000 ,  1.485935699323978510 ,  1.103818223347951210 ,  1.000000000000000000 ,  0.620559089319759005 ,  1.158258442953759240  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.992894416083025644 ,  1.000000000000000000 ,  1.805187738171452500 ,  1.016198161993289380 ,  1.000000000000000000 ,  1.290412020625010920 ,  1.081675795300447880 ,  1.000000000000000000 ,  0.512790342271641220 ,  1.120591030054942430  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.918861877373222220 ,  0.960438363659689376 ,  1.000000000000000000 ,  1.645955118265584940 ,  1.003565519952952070 ,  1.000000000000000000 ,  1.132411700727183220 ,  1.061053614079032980 ,  1.000000000000000000 ,  0.433588804226196667 ,  1.089474640678081090  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.965632704343265646 ,  1.000000000000000000 ,  1.821573411800616160 ,  0.951237884376396114 ,  1.000000000000000000 ,  1.503225316489503970 ,  0.997908886286003827 ,  1.000000000000000000 ,  1.006625616407745390 ,  1.047095945657740220 ,  1.000000000000000000 ,  0.374582387480978274 ,  1.068557495864874470  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.892542827605514290 ,  0.920666606671982413 ,  1.000000000000000000 ,  1.716331813306957170 ,  0.949739845292811635 ,  1.000000000000000000 ,  1.378170326119235070 ,  0.995522559094325410 ,  1.000000000000000000 ,  0.904654534590091819 ,  1.037322376639232550 ,  1.000000000000000000 ,  0.329240818898485887 ,  1.054085977885809160  } },
/* Adj Gauss Gamma = 0.65 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.755988061860472180 ,  1.252842106051839990  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.081611931537485470 ,  1.000000000000000000 ,  1.296539469055389300 ,  1.276566765582181740  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.969606978321972730 ,  1.129604954748958390 ,  1.000000000000000000 ,  0.969695852341214248 ,  1.230620697066227230  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.022365578920813260 ,  1.000000000000000000 ,  1.699067738736219410 ,  1.099514064774053560 ,  1.000000000000000000 ,  0.750730472756067857 ,  1.174668942711998950  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.937501704427510910 ,  1.009797394265556480 ,  1.000000000000000000 ,  1.462925766372041190 ,  1.077112836053772950 ,  1.000000000000000000 ,  0.602705330108862847 ,  1.130909376869693840  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.981055014156879679 ,  1.000000000000000000 ,  1.782261346649213210 ,  0.992289645785535357 ,  1.000000000000000000 ,  1.270019362214859000 ,  1.056569724639278010 ,  1.000000000000000000 ,  0.498063832249812377 ,  1.095393491887827420  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.907058690505146760 ,  0.948937552798154060 ,  1.000000000000000000 ,  1.633957895264212420 ,  0.991771213125030138 ,  1.000000000000000000 ,  1.120538616313341730 ,  1.048890633119558880 ,  1.000000000000000000 ,  0.423759538132682800 ,  1.077931800996266620  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.958030681022346231 ,  1.000000000000000000 ,  1.806552364204269120 ,  0.936412057736972003 ,  1.000000000000000000 ,  1.488872348838856000 ,  0.982568050279749472 ,  1.000000000000000000 ,  0.993858788424808792 ,  1.031306119433101690 ,  1.000000000000000000 ,  0.365682249272550219 ,  1.053473741842655190  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.874862039925930370 ,  0.903684562401738667 ,  1.000000000000000000 ,  1.699320980927778500 ,  0.932349304312502736 ,  1.000000000000000000 ,  1.362629461539841550 ,  0.977490812198178571 ,  1.000000000000000000 ,  0.891742957948285397 ,  1.018866442271479710 ,  1.000000000000000000 ,  0.321305398456226410 ,  1.036403827611270370  } },
/* Adj Gauss Gamma = 0.70 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.737085588443781470 ,  1.237504821097763940  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.069651789998653290 ,  1.000000000000000000 ,  1.270725860686865660 ,  1.249591741510745590  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.944350859212529150 ,  1.102795040744098510 ,  1.000000000000000000 ,  0.946316513113652058 ,  1.202296943090489960  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.011718217039065950 ,  1.000000000000000000 ,  1.678122806771842110 ,  1.077178008152655190 ,  1.000000000000000000 ,  0.732358488790799789 ,  1.151573745783964320  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.924361522395636290 ,  0.996758572374059626 ,  1.000000000000000000 ,  1.449192577373132900 ,  1.063671636063920230 ,  1.000000000000000000 ,  0.589712496051620016 ,  1.117624560661068190  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.976049391218634876 ,  1.000000000000000000 ,  1.771884643772931200 ,  0.982383979657592943 ,  1.000000000000000000 ,  1.259004450195514210 ,  1.046430831175610310 ,  1.000000000000000000 ,  0.487972127606879302 ,  1.085813654723972290  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.893636504033185060 ,  0.935883933329745843 ,  1.000000000000000000 ,  1.620762966477592660 ,  0.978373670170967769 ,  1.000000000000000000 ,  1.108271066948258280 ,  1.035099572006929370 ,  1.000000000000000000 ,  0.414631115232684699 ,  1.064780257997928640  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.954525703008551996 ,  1.000000000000000000 ,  1.799326374589768740 ,  0.929672904925563315 ,  1.000000000000000000 ,  1.481159442287367070 ,  0.975741418432385377 ,  1.000000000000000000 ,  0.985938130278378066 ,  1.024521218251284620 ,  1.000000000000000000 ,  0.359316549004192964 ,  1.047613641039506050  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.866060288049411260 ,  0.895348355561568332 ,  1.000000000000000000 ,  1.690466968926287010 ,  0.923889654609105104 ,  1.000000000000000000 ,  1.353861253605558530 ,  0.968856362422907957 ,  1.000000000000000000 ,  0.883664299585683266 ,  1.010266468217483780 ,  1.000000000000000000 ,  0.315772267290232334 ,  1.028729307887282560  } },
/* Adj Gauss Gamma = 0.75 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.723426835444596520 ,  1.229224884504704680  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.061532511603320960 ,  1.000000000000000000 ,  1.250303071505986630 ,  1.231896800244220770  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.927831827240761960 ,  1.085993951116905000 ,  1.000000000000000000 ,  0.928171787761567502 ,  1.185000083839771840  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.006581117628867930 ,  1.000000000000000000 ,  1.666587109609412920 ,  1.066737430193767540 ,  1.000000000000000000 ,  0.719040467192332500 ,  1.141319904655247570  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.907664038536361910 ,  0.980103876348844771 ,  1.000000000000000000 ,  1.433138710209394650 ,  1.046403955192463990 ,  1.000000000000000000 ,  0.576675097308601781 ,  1.100419295274932540  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.969693534196193196 ,  1.000000000000000000 ,  1.759174368487118610 ,  0.969835527869379987 ,  1.000000000000000000 ,  1.246707989390941410 ,  1.033524360276128600 ,  1.000000000000000000 ,  0.478192557621645309 ,  1.073431949833703050  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.877904803951828460 ,  0.920634859538066341 ,  1.000000000000000000 ,  1.605768464072746760 ,  0.962694797801201152 ,  1.000000000000000000 ,  1.095163533660322490 ,  1.018944498658906910 ,  1.000000000000000000 ,  0.405927023622639382 ,  1.049220373372077080  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.945511444429970371 ,  1.000000000000000000 ,  1.781779469225855730 ,  0.912301718682400398 ,  1.000000000000000000 ,  1.465148731645459220 ,  0.957777560045928400 ,  1.000000000000000000 ,  0.972864121076456367 ,  1.006091630877000490 ,  1.000000000000000000 ,  0.351705626911055058 ,  1.029837077728705140  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.856831745042256010 ,  0.886632788715135733 ,  1.000000000000000000 ,  1.681321895721293200 ,  0.915047997901899723 ,  1.000000000000000000 ,  1.345061043462672630 ,  0.959849444433135468 ,  1.000000000000000000 ,  0.875904255693965017 ,  1.001323844310553660 ,  1.000000000000000000 ,  0.310881114858233110 ,  1.020672490674688900  } },
/* Adj Gauss Gamma = 0.80 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.705684978566712310 ,  1.214721479606662990  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.053955622433181860 ,  1.000000000000000000 ,  1.231251405400395570 ,  1.215657496597714490  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.913459692009283010 ,  1.071613293129374030 ,  1.000000000000000000 ,  0.911915075308842526 ,  1.170436709121010170  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.995058138237910894 ,  1.000000000000000000 ,  1.644731133466554640 ,  1.042936947976950140 ,  1.000000000000000000 ,  0.702138236002830207 ,  1.116858406900633540  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.895361514899273070 ,  0.968032426931184076 ,  1.000000000000000000 ,  1.420715826794282990 ,  1.034052418111143460 ,  1.000000000000000000 ,  0.565902513492349324 ,  1.088450051677583240  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.961619582011299823 ,  1.000000000000000000 ,  1.743459435911056010 ,  0.953963173446749746 ,  1.000000000000000000 ,  1.232635163586040860 ,  1.017108238611305500 ,  1.000000000000000000 ,  0.468455642551277918 ,  1.057436557272144470  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.868984206822191570 ,  0.912127950580011948 ,  1.000000000000000000 ,  1.596756919251388410 ,  0.954076829769246682 ,  1.000000000000000000 ,  1.086481173722248620 ,  1.010300529241497050 ,  1.000000000000000000 ,  0.399487397672040834 ,  1.041395212374553130  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.945758103419902740 ,  1.000000000000000000 ,  1.781741479563973440 ,  0.912888513747941888 ,  1.000000000000000000 ,  1.463709962413709990 ,  0.958685082377144560 ,  1.000000000000000000 ,  0.969785684934705405 ,  1.007523519226078520 ,  1.000000000000000000 ,  0.348228196090075381 ,  1.032367136676381580  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.857628524057565840 ,  0.887503814552637271 ,  1.000000000000000000 ,  1.681337908809628520 ,  0.916108922128646364 ,  1.000000000000000000 ,  1.343763444689293430 ,  0.961254176337560828 ,  1.000000000000000000 ,  0.873316680272556711 ,  1.003278148562240180 ,  1.000000000000000000 ,  0.308246529331713537 ,  1.023683188979652490  } },
/* Adj Gauss Gamma = 0.85 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.688504285684442770 ,  1.200635980095668080  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.046941151430389680 ,  1.000000000000000000 ,  1.213533782194208930 ,  1.200875751390618530  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.894022014392101380 ,  1.051593344300804760 ,  1.000000000000000000 ,  0.894049458083543302 ,  1.149774888905558120  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.989236535292352004 ,  1.000000000000000000 ,  1.632536571483174990 ,  1.031264875867418330 ,  1.000000000000000000 ,  0.690158177061527933 ,  1.105440586904806510  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.878833330860498310 ,  0.951713554889125346 ,  1.000000000000000000 ,  1.405428405708539330 ,  1.017178166370129810 ,  1.000000000000000000 ,  0.554706344193825362 ,  1.071757818420483450  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.956391304949663734 ,  1.000000000000000000 ,  1.733006287693811040 ,  0.943834411301069487 ,  1.000000000000000000 ,  1.222600563507837630 ,  1.006839044609878720 ,  1.000000000000000000 ,  0.460882169180986345 ,  1.047840396856023700  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.857128883272073150 ,  0.900795004584789893 ,  1.000000000000000000 ,  1.585380611009872260 ,  0.942510845289832755 ,  1.000000000000000000 ,  1.076493694202240500 ,  0.998562390606404060 ,  1.000000000000000000 ,  0.393093999196949151 ,  1.030364372629999580  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.939631426189516561 ,  1.000000000000000000 ,  1.769747223582348150 ,  0.901213167160166195 ,  1.000000000000000000 ,  1.452602580259165550 ,  0.946730210312266451 ,  1.000000000000000000 ,  0.960576887049778616 ,  0.995466105475514729 ,  1.000000000000000000 ,  0.342974317910634596 ,  1.021041935131679290  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.845701963683779700 ,  0.876244975317733821 ,  1.000000000000000000 ,  1.669914692624674620 ,  0.904655167823742667 ,  1.000000000000000000 ,  1.333480561041270550 ,  0.949544846608439852 ,  1.000000000000000000 ,  0.865140746783979120 ,  0.991566313344394379 ,  1.000000000000000000 ,  0.303982240576749496 ,  1.012695603696923910  } },
/* Adj Gauss Gamma = 0.90 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.671872569983595900 ,  1.186965612582157940  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.040510172107354410 ,  1.000000000000000000 ,  1.197118541230568050 ,  1.187560468811499350  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.884038470048898350 ,  1.042089018418241860 ,  1.000000000000000000 ,  0.881375162965688075 ,  1.140653778511480980  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.985247527511950327 ,  1.000000000000000000 ,  1.623568588873765210 ,  1.023470534173903880 ,  1.000000000000000000 ,  0.680219113248041740 ,  1.098227323949226400  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.866658263709719550 ,  0.939875820988277377 ,  1.000000000000000000 ,  1.393676285265066820 ,  1.005100406580023310 ,  1.000000000000000000 ,  0.545546216310609733 ,  1.060134242726130220  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.954300588699528274 ,  1.000000000000000000 ,  1.728327201855110800 ,  0.939933935596213188 ,  1.000000000000000000 ,  1.216913100110956770 ,  1.003234784301170410 ,  1.000000000000000000 ,  0.455474585578144520 ,  1.045182955976911470  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.852480316730791500 ,  0.896478787696654234 ,  1.000000000000000000 ,  1.580284270526328690 ,  0.938292498793170449 ,  1.000000000000000000 ,  1.071038281729430390 ,  0.994628756867409036 ,  1.000000000000000000 ,  0.388798534871724888 ,  1.027359398531459740  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.937747294144768095 ,  1.000000000000000000 ,  1.765791022600685430 ,  0.897719471369049060 ,  1.000000000000000000 ,  1.448234850873966640 ,  0.943379359155015806 ,  1.000000000000000000 ,  0.956072678322812508 ,  0.992471272391521553 ,  1.000000000000000000 ,  0.339757997046345905 ,  1.018962574672090280  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.843363051024353270 ,  0.874118726535515034 ,  1.000000000000000000 ,  1.667236740522136220 ,  0.902633749351125503 ,  1.000000000000000000 ,  1.330323852778474200 ,  0.947746888795057529 ,  1.000000000000000000 ,  0.861808970899650362 ,  0.990213464311435088 ,  1.000000000000000000 ,  0.301700023943584972 ,  1.012223192945817860  } },
/* Adj Gauss Gamma = 0.95 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.660482161644064370 ,  1.180386049537211380  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.031054701808109760 ,  1.000000000000000000 ,  1.177832125213619240 ,  1.167491727131534370  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.868808140326779780 ,  1.026768545918131940 ,  1.000000000000000000 ,  0.866904307985813416 ,  1.125195781878553400  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.978627331270437994 ,  1.000000000000000000 ,  1.610455016785847350 ,  1.010272221547687810 ,  1.000000000000000000 ,  0.669164633255410646 ,  1.085241892171779470  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.859195527134765320 ,  0.932802834196874264 ,  1.000000000000000000 ,  1.385687130246158190 ,  0.998124470842585487 ,  1.000000000000000000 ,  0.538409896138849930 ,  1.053898902266288130  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.950137669055512002 ,  1.000000000000000000 ,  1.719976056156708570 ,  0.931975443250974700 ,  1.000000000000000000 ,  1.208886362812177540 ,  0.995313786728776795 ,  1.000000000000000000 ,  0.449647642816562110 ,  1.038017744656071440  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.844082082006423210 ,  0.888542456933373304 ,  1.000000000000000000 ,  1.572103822974019180 ,  0.930289264947382399 ,  1.000000000000000000 ,  1.063728661353328550 ,  0.986696265126761452 ,  1.000000000000000000 ,  0.384202203386857455 ,  1.020189086015423020  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.934140257321446010 ,  1.000000000000000000 ,  1.758632770406957540 ,  0.890945116054499664 ,  1.000000000000000000 ,  1.441366171975708490 ,  0.936587837487977026 ,  1.000000000000000000 ,  0.950131155670404737 ,  0.985866191039578843 ,  1.000000000000000000 ,  0.336322733280769060 ,  1.013124970375839600  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.838101819810305400 ,  0.869221972504225282 ,  1.000000000000000000 ,  1.661977714196373630 ,  0.897754545633643874 ,  1.000000000000000000 ,  1.325231364341675770 ,  0.942956871521049456 ,  1.000000000000000000 ,  0.857409543741064484 ,  0.985738463206728532 ,  1.000000000000000000 ,  0.299265816885772418 ,  1.008497952907410730  } },
/* Adj Gauss Gamma = 1.00 */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.644923569655017740 ,  1.167541286827850740  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.025787257424393360 ,  1.000000000000000000 ,  1.163893820811170210 ,  1.157033489359083460  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.855727947450719340 ,  1.013820412864835600 ,  1.000000000000000000 ,  0.854053657276512057 ,  1.112346323390733800  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.973769198034327044 ,  1.000000000000000000 ,  1.600420519844923060 ,  1.000778379809188490 ,  1.000000000000000000 ,  0.659960836741032786 ,  1.076244038402871080  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.846640527654444420 ,  0.920644002186827182 ,  1.000000000000000000 ,  1.374128208357579430 ,  0.985707334065049512 ,  1.000000000000000000 ,  0.530377857055792656 ,  1.041910151790999660  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.943366983713312624 ,  1.000000000000000000 ,  1.706982609621181090 ,  0.918964174283365409 ,  1.000000000000000000 ,  1.197830393578967810 ,  0.981998932342225239 ,  1.000000000000000000 ,  0.443103953644027160 ,  1.025194168062984130  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.843290564485575490 ,  0.887941741984539901 ,  1.000000000000000000 ,  1.570510992012091660 ,  0.929969034891782398 ,  1.000000000000000000 ,  1.061084401316505940 ,  0.986920988278435329 ,  1.000000000000000000 ,  0.381603692987601273 ,  1.021413645519304140  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.928135186205226059 ,  1.000000000000000000 ,  1.746999892470071590 ,  0.879645764895948146 ,  1.000000000000000000 ,  1.430947117120313860 ,  0.925039934712328593 ,  1.000000000000000000 ,  0.942042690845270259 ,  0.974251662466998569 ,  1.000000000000000000 ,  0.332373713225436962 ,  1.002075124906329550  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.828447419072206870 ,  0.860193731091495439 ,  1.000000000000000000 ,  1.652804364978233130 ,  0.888608310931661594 ,  1.000000000000000000 ,  1.317131102738158030 ,  0.933686733467427477 ,  1.000000000000000000 ,  0.851228996205661725 ,  0.976572705997276747 ,  1.000000000000000000 ,  0.296391515739343248 ,  0.999904700662466506  } } } ;

 const double ChebyshevDenominator[11][9][30] = {
/* Chebyshev Ripple = 0.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.407153622853495100 ,  0.994814273746082334  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.964555901435341423 ,  1.000000000000000000 ,  0.964555901435341423 ,  0.982232477945649163  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.674309613202162870 ,  0.847119723461009166 ,  1.000000000000000000 ,  0.693521749399987009 ,  0.972892761281281326  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.834838060456134179 ,  1.000000000000000000 ,  1.350796356920064940 ,  0.800997860005560280 ,  1.000000000000000000 ,  0.515958296463930544 ,  0.969343411728127990  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.474444956943965270 ,  0.610372554648067611 ,  1.000000000000000000 ,  1.079368621446688440 ,  0.790425219351279407 ,  1.000000000000000000 ,  0.395076335497277109 ,  0.970477884054491091  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.696396844125015235 ,  1.000000000000000000 ,  1.254863752524264960 ,  0.581501294109260547 ,  1.000000000000000000 ,  0.868392660717106302 ,  0.798408368616082109 ,  1.000000000000000000 ,  0.309925752317856684 ,  0.972354336826134258  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.249890263669745090 ,  0.428570729590645239 ,  1.000000000000000000 ,  1.059605801309480900 ,  0.588976982602940513 ,  1.000000000000000000 ,  0.708005961050356181 ,  0.815825681102378963 ,  1.000000000000000000 ,  0.248618631311334104 ,  0.976231934114674460  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.584603324900179078 ,  1.000000000000000000 ,  1.098694860991210390 ,  0.418534157641744398 ,  1.000000000000000000 ,  0.895664256937321324 ,  0.612930496633218969 ,  1.000000000000000000 ,  0.584603324900179078 ,  0.833989856970003607 ,  1.000000000000000000 ,  0.203030604053888952 ,  0.978276107149388396  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.063740893446996290 ,  0.307279527665073882 ,  1.000000000000000000 ,  0.959614523277792397 ,  0.435662192496540923 ,  1.000000000000000000 ,  0.761554397542627548 ,  0.643389707760140372 ,  1.000000000000000000 ,  0.488948021314499215 ,  0.851117223023739711 ,  1.000000000000000000 ,  0.168480006058924742 ,  0.979499887855206919  } },
/* Chebyshev Ripple = 0.10 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.208603380651428920 ,  0.860132111132188015  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.697100734583409376 ,  1.000000000000000000 ,  0.697100734583409265 ,  0.873779253623967911  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.046531092788706820 ,  0.419378561513079895 ,  1.000000000000000000 ,  0.433487372078218414 ,  0.895432042075226309  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.474912450636248706 ,  1.000000000000000000 ,  0.768424486809957008 ,  0.493844526285373064 ,  1.000000000000000000 ,  0.293512036173708357 ,  0.927967398813379307  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.781309227532317019 ,  0.219364467904911931 ,  1.000000000000000000 ,  0.571958050976048460 ,  0.580038467176755668 ,  1.000000000000000000 ,  0.209351176556268670 ,  0.940712466448599405  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.352669604230059774 ,  1.000000000000000000 ,  0.635488668133502244 ,  0.289310543192075387 ,  1.000000000000000000 ,  0.439771803325995914 ,  0.659915435306784426 ,  1.000000000000000000 ,  0.156952739422553389 ,  0.957117483367450506  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.609943335158696032 ,  0.130903249318031406 ,  1.000000000000000000 ,  0.517084991530886495 ,  0.374166813235597640 ,  1.000000000000000000 ,  0.345505145329619845 ,  0.718193444559034333 ,  1.000000000000000000 ,  0.121325272763860528 ,  0.961457008476600539  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.279017119847157102 ,  1.000000000000000000 ,  0.524380657186621968 ,  0.185792158620704306 ,  1.000000000000000000 ,  0.427479028387953208 ,  0.459109898150678430 ,  1.000000000000000000 ,  0.279017119847157158 ,  0.769915363843330813 ,  1.000000000000000000 ,  0.096901628798668649 ,  0.972779224083081262  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.497566500100198761 ,  0.086283470011128433 ,  1.000000000000000000 ,  0.448861224320735663 ,  0.255791092351318250 ,  1.000000000000000000 ,  0.356218284504713878 ,  0.530060186649926490 ,  1.000000000000000000 ,  0.228706217082642521 ,  0.804329280948534731 ,  1.000000000000000000 ,  0.078806791642608343 ,  0.973836903288724631  } },
/* Chebyshev Ripple = 0.20 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.128910255684909990 ,  0.808807241490167250  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.633879447063457779 ,  1.000000000000000000 ,  0.633879447063457668 ,  0.855900953688481514  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.932494762793567689 ,  0.362759636367577243 ,  1.000000000000000000 ,  0.386251977590977902 ,  0.884593675508174360  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.419613169938507335 ,  1.000000000000000000 ,  0.678948371087590541 ,  0.461808279383835674 ,  1.000000000000000000 ,  0.259335201149083205 ,  0.924134093496339570  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.685833311909132881 ,  0.184296855947774785 ,  1.000000000000000000 ,  0.502064829840718430 ,  0.560910343377590825 ,  1.000000000000000000 ,  0.183768482068414535 ,  0.937523830807406977  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.308639325295453737 ,  1.000000000000000000 ,  0.556148847003222868 ,  0.265757344141248519 ,  1.000000000000000000 ,  0.384866943548551277 ,  0.648865369060576347 ,  1.000000000000000000 ,  0.137357421840782229 ,  0.956094151133045744  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.532602873569025137 ,  0.108842542860475233 ,  1.000000000000000000 ,  0.451518914125212722 ,  0.358537727251149685 ,  1.000000000000000000 ,  0.301695293034985490 ,  0.711660043475692561 ,  1.000000000000000000 ,  0.105941298454823571 ,  0.961355227866366957  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.242975847862672872 ,  1.000000000000000000 ,  0.456645222531506467 ,  0.169032896366424296 ,  1.000000000000000000 ,  0.372260596134645572 ,  0.447551618048195266 ,  1.000000000000000000 ,  0.242975847862672900 ,  0.764271422424199209 ,  1.000000000000000000 ,  0.084384626396860896 ,  0.970995593078363695  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.432773711078475343 ,  0.071184262153561051 ,  1.000000000000000000 ,  0.390410804926364818 ,  0.243279749710701265 ,  1.000000000000000000 ,  0.309831769036035209 ,  0.521736097888638839 ,  1.000000000000000000 ,  0.198924240867587782 ,  0.800192446066576246 ,  1.000000000000000000 ,  0.068544622016336904 ,  0.972287933623716572  } },
/* Chebyshev Ripple = 0.30 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.070971878837166490 ,  0.773264807260397036  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592682524294650737 ,  1.000000000000000000 ,  0.592682524294650737 ,  0.846631607106168804  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.860573644733475418 ,  0.330250317035351348 ,  1.000000000000000000 ,  0.356461275069451244 ,  0.877491729062152892  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.385505581293712984 ,  1.000000000000000000 ,  0.623761133386013356 ,  0.443706775553762811 ,  1.000000000000000000 ,  0.238255552092300343 ,  0.921176021123971567  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.627946836916920770 ,  0.165213971068435828 ,  1.000000000000000000 ,  0.459688989075353938 ,  0.550196391645863425 ,  1.000000000000000000 ,  0.168257847841566915 ,  0.935178812223290801  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.282417944764801809 ,  1.000000000000000000 ,  0.508899551940142802 ,  0.253472868187868683 ,  1.000000000000000000 ,  0.352169416845514116 ,  0.643802374662072130 ,  1.000000000000000000 ,  0.125687809670173067 ,  0.956822335301814708  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.485888538159952088 ,  0.096876097088963484 ,  1.000000000000000000 ,  0.411916412815665989 ,  0.349406019617843122 ,  1.000000000000000000 ,  0.275233747651775384 ,  0.706537260963212232 ,  1.000000000000000000 ,  0.096649239408037449 ,  0.959067183492091857  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.221780245253822339 ,  1.000000000000000000 ,  0.416810519802211621 ,  0.160442795557668494 ,  1.000000000000000000 ,  0.339787048940509429 ,  0.442153682833245076 ,  1.000000000000000000 ,  0.221780245253822311 ,  0.762503483987566955 ,  1.000000000000000000 ,  0.077023470861702234 ,  0.971596966769065595  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.395162517671909075 ,  0.063470818850394808 ,  1.000000000000000000 ,  0.356481257183026701 ,  0.237546295729845730 ,  1.000000000000000000 ,  0.282905127490103103 ,  0.519206333928643682 ,  1.000000000000000000 ,  0.181636272802520787 ,  0.800866372127441939 ,  1.000000000000000000 ,  0.062587594198700011 ,  0.974941849006892958  } },
/* Chebyshev Ripple = 0.40 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.023915139990872800 ,  0.745270427241363831  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.561163376498458266 ,  1.000000000000000000 ,  0.561163376498458266 ,  0.839830645516588814  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.807479644369267668 ,  0.307898930905284474 ,  1.000000000000000000 ,  0.334469020037954312 ,  0.872466179145209297  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.361428024330197994 ,  1.000000000000000000 ,  0.584802827852984319 ,  0.433323399507334006 ,  1.000000000000000000 ,  0.223374803522786325 ,  0.923091257337233073  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.586262687886888290 ,  0.152539423398346224 ,  1.000000000000000000 ,  0.429174074115097282 ,  0.543257071089335519 ,  1.000000000000000000 ,  0.157088613771791119 ,  0.933974718780325119  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.263389592705451148 ,  1.000000000000000000 ,  0.474611646314219204 ,  0.245059177904290604 ,  1.000000000000000000 ,  0.328441449935148566 ,  0.639820015087451677 ,  1.000000000000000000 ,  0.117219396326380509 ,  0.956393627042538275  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.453367134510829661 ,  0.089343013870650245 ,  1.000000000000000000 ,  0.384346098064864194 ,  0.344756223288328278 ,  1.000000000000000000 ,  0.256811852294578202 ,  0.705965048056048317 ,  1.000000000000000000 ,  0.090180330017681040 ,  0.961378257473726294  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.206614733604435080 ,  1.000000000000000000 ,  0.388308681027467795 ,  0.154798502236547175 ,  1.000000000000000000 ,  0.316552137088370933 ,  0.438668085737129454 ,  1.000000000000000000 ,  0.206614733604435052 ,  0.761472665665253956 ,  1.000000000000000000 ,  0.071756543939096848 ,  0.972168391499262596  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.367521533730117700 ,  0.058158219717059340 ,  1.000000000000000000 ,  0.331545965335517334 ,  0.232901291293222790 ,  1.000000000000000000 ,  0.263116367837304022 ,  0.515641520402010833 ,  1.000000000000000000 ,  0.168931107015643789 ,  0.798381749510798877 ,  1.000000000000000000 ,  0.058209692427059895 ,  0.973124821086962299  } },
/* Chebyshev Ripple = 0.50 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  0.983719340948495224 ,  0.721920469624084071  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.536364081950201577 ,  1.000000000000000000 ,  0.536364081950201466 ,  0.837478936320064427  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.764550111091122475 ,  0.290620302934049834 ,  1.000000000000000000 ,  0.316687025127799449 ,  0.867199284010747085  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.341634710157459498 ,  1.000000000000000000 ,  0.552776572771488506 ,  0.423883451655334087 ,  1.000000000000000000 ,  0.211141862614028952 ,  0.920893619481666148  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.553636349282274787 ,  0.143252704834041317 ,  1.000000000000000000 ,  0.405289936591574196 ,  0.538356297382611260 ,  1.000000000000000000 ,  0.148346412690700535 ,  0.933459889931181230  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.248407283166428766 ,  1.000000000000000000 ,  0.447614457386345921 ,  0.238724747153200967 ,  1.000000000000000000 ,  0.309758815523406006 ,  0.636481860777906805 ,  1.000000000000000000 ,  0.110551641303488796 ,  0.955458299878370942  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.426912481207366579 ,  0.083427516590491094 ,  1.000000000000000000 ,  0.361918925914824108 ,  0.339812802044821394 ,  1.000000000000000000 ,  0.241826494954982790 ,  0.702396349927233099 ,  1.000000000000000000 ,  0.084918172300881112 ,  0.958781635381563468  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.194604419528839862 ,  1.000000000000000000 ,  0.365736674007151874 ,  0.150409681466509682 ,  1.000000000000000000 ,  0.298151268372924216 ,  0.435367929676792942 ,  1.000000000000000000 ,  0.194604419528839862 ,  0.759410493435251688 ,  1.000000000000000000 ,  0.067585405634227658 ,  0.970914255827357464  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.346214098363391065 ,  0.054351339313179793 ,  1.000000000000000000 ,  0.312324250200117559 ,  0.229765853389385405 ,  1.000000000000000000 ,  0.247861928336248427 ,  0.513592499284733051 ,  1.000000000000000000 ,  0.159137153971225381 ,  0.797419145180080502 ,  1.000000000000000000 ,  0.054834926201749665 ,  0.972833659256286176  } },
/* Chebyshev Ripple = 0.60 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  0.948445385621317527 ,  0.701912959451102103  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.514530862251239007 ,  1.000000000000000000 ,  0.514530862251238896 ,  0.833393958785152322  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.729376582351245806 ,  0.277363984423114718 ,  1.000000000000000000 ,  0.302117672487222710 ,  0.864248015930334179  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.325817827966764173 ,  1.000000000000000000 ,  0.527184319790890532 ,  0.417902447603715477 ,  1.000000000000000000 ,  0.201366491824126331 ,  0.922316761795895168  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.526410855445657688 ,  0.135832413081230924 ,  1.000000000000000000 ,  0.385359491842017232 ,  0.533901125381985531 ,  1.000000000000000000 ,  0.141051363603640539 ,  0.931969837682739999  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.236295590287371904 ,  1.000000000000000000 ,  0.425789940943094636 ,  0.234202882842949489 ,  1.000000000000000000 ,  0.294655781536731998 ,  0.634990515903821939 ,  1.000000000000000000 ,  0.105161430881009238 ,  0.956397242899238331  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.405430735875547843 ,  0.078918114647776427 ,  1.000000000000000000 ,  0.343707581577268328 ,  0.336281036147503654 ,  1.000000000000000000 ,  0.229658063700835469 ,  0.700247170184380319 ,  1.000000000000000000 ,  0.080645187481482111 ,  0.957610091684107823  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.184846486068617810 ,  1.000000000000000000 ,  0.347397757873770807 ,  0.147139450129726651 ,  1.000000000000000000 ,  0.283201246965868569 ,  0.433192637766564248 ,  1.000000000000000000 ,  0.184846486068617782 ,  0.758480320700092747 ,  1.000000000000000000 ,  0.064196510907902210 ,  0.970796776915810233  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.328895616882056763 ,  0.051446031776296829 ,  1.000000000000000000 ,  0.296701022351132049 ,  0.227535865782803243 ,  1.000000000000000000 ,  0.235463264514899517 ,  0.512455202278657818 ,  1.000000000000000000 ,  0.151176721778917111 ,  0.797374538774512365 ,  1.000000000000000000 ,  0.052091948205057685 ,  0.973464372781018605  } },
/* Chebyshev Ripple = 0.70 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  0.917586387415365024 ,  0.685445199102345359  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.496333800250782942 ,  1.000000000000000000 ,  0.496333800250782942 ,  0.832814886935249965  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.699168358751944941 ,  0.266473392337480741 ,  1.000000000000000000 ,  0.289605016577193286 ,  0.861801689916411706  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.312127226652286704 ,  1.000000000000000000 ,  0.505032461537642030 ,  0.412668047079021993 ,  1.000000000000000000 ,  0.192905234885355270 ,  0.922744591734799502  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.502681701008510284 ,  0.129520520399577677 ,  1.000000000000000000 ,  0.367988545173376924 ,  0.529084325139760892 ,  1.000000000000000000 ,  0.134693155835133499 ,  0.928648129879944384  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.225720075889555200 ,  1.000000000000000000 ,  0.406733522474121312 ,  0.229996971038804982 ,  1.000000000000000000 ,  0.281468330783834153 ,  0.632312868235176850 ,  1.000000000000000000 ,  0.100454884199267999 ,  0.954945167911127046  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.387364606116789356 ,  0.075334049630825642 ,  1.000000000000000000 ,  0.328391856304406815 ,  0.333680209667982119 ,  1.000000000000000000 ,  0.219424423249268669 ,  0.699036852979539436 ,  1.000000000000000000 ,  0.077051610841779403 ,  0.957383013016696371  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.176636578888623524 ,  1.000000000000000000 ,  0.331968179485015002 ,  0.144606630230431549 ,  1.000000000000000000 ,  0.270622939418354080 ,  0.431761080324110647 ,  1.000000000000000000 ,  0.176636578888623497 ,  0.758301072717453506 ,  1.000000000000000000 ,  0.061345240066660894 ,  0.971434915881841832  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.313715502917615296 ,  0.048946127249722707 ,  1.000000000000000000 ,  0.283006843707603317 ,  0.225035961256229100 ,  1.000000000000000000 ,  0.224595502810865100 ,  0.509955297752083592 ,  1.000000000000000000 ,  0.144199189250116183 ,  0.794874634247937917 ,  1.000000000000000000 ,  0.049687654350736951 ,  0.970964468254444490  } },
/* Chebyshev Ripple = 0.80 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  0.888539823922353866 ,  0.669258767228402163  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.479714200129741419 ,  1.000000000000000000 ,  0.479714200129741419 ,  0.831035293736676506  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.672305524287108103 ,  0.257019253258923608 ,  1.000000000000000000 ,  0.278478066218074471 ,  0.858800926978155244  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.299871230439182679 ,  1.000000000000000000 ,  0.485201843098849728 ,  0.407533201454287841 ,  1.000000000000000000 ,  0.185330612659666993 ,  0.921437699249972209  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.483157676297375049 ,  0.124829663845823446 ,  1.000000000000000000 ,  0.353695967116595622 ,  0.527409033775827707 ,  1.000000000000000000 ,  0.129461709180779511 ,  0.929988403705832245  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.217021081650278558 ,  1.000000000000000000 ,  0.391058476490819873 ,  0.227517574112720306 ,  1.000000000000000000 ,  0.270620862394600425 ,  0.632916340476615713 ,  1.000000000000000000 ,  0.096583467554059138 ,  0.958020909272552568  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.371788407071542038 ,  0.072399994216277866 ,  1.000000000000000000 ,  0.315186992365202368 ,  0.331735038172722374 ,  1.000000000000000000 ,  0.210601215248461399 ,  0.698490174534549313 ,  1.000000000000000000 ,  0.073953312214912456 ,  0.957825218490993890  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.169555740107220410 ,  1.000000000000000000 ,  0.318660555581296578 ,  0.142592737167552691 ,  1.000000000000000000 ,  0.259774465016123612 ,  0.430854821527689391 ,  1.000000000000000000 ,  0.169555740107220410 ,  0.758654369135397633 ,  1.000000000000000000 ,  0.058886090565172938 ,  0.972610328578860428  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.301164178200002630 ,  0.047059850873620551 ,  1.000000000000000000 ,  0.271684130103572008 ,  0.223828912153313325 ,  1.000000000000000000 ,  0.215609746417962295 ,  0.509847261463269197 ,  1.000000000000000000 ,  0.138429978511525414 ,  0.795865610773225152 ,  1.000000000000000000 ,  0.047699719810006341 ,  0.972634672052918092  } },
/* Chebyshev Ripple = 0.90 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  0.862253831002234850 ,  0.655201042151471902  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464776360344296424 ,  1.000000000000000000 ,  0.464776360344296480 ,  0.829734304705868619  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.647776420933328101 ,  0.248438381125589663 ,  1.000000000000000000 ,  0.268317778936087381 ,  0.854580777351228282  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.289161040565483085 ,  1.000000000000000000 ,  0.467872391857238723 ,  0.403617091273751949 ,  1.000000000000000000 ,  0.178711351291755693 ,  0.921392795713750545  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.465198571497338897 ,  0.120501290277773787 ,  1.000000000000000000 ,  0.340548989944515046 ,  0.524601260818300386 ,  1.000000000000000000 ,  0.124649581552823893 ,  0.928701231358827206  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.209005625607287304 ,  1.000000000000000000 ,  0.376615123777269023 ,  0.224794700057873081 ,  1.000000000000000000 ,  0.260625752194496407 ,  0.631748206189425510 ,  1.000000000000000000 ,  0.093016254024514730 ,  0.958099579474402940  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.358107186305167857 ,  0.069944641951200703 ,  1.000000000000000000 ,  0.303588613439933652 ,  0.330274258509832830 ,  1.000000000000000000 ,  0.202851426216103387 ,  0.698435932934438175 ,  1.000000000000000000 ,  0.071231948203629228 ,  0.958765549493070024  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.163334553863877213 ,  1.000000000000000000 ,  0.306968549970487781 ,  0.140961739460186641 ,  1.000000000000000000 ,  0.250243054713481072 ,  0.430337879147023428 ,  1.000000000000000000 ,  0.163334553863877185 ,  0.759404283729393614 ,  1.000000000000000000 ,  0.056725495257006695 ,  0.974187125337489190  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.289575180019326084 ,  0.045305396994278865 ,  1.000000000000000000 ,  0.261229543810118314 ,  0.222074458273971598 ,  1.000000000000000000 ,  0.207312939759520698 ,  0.508092807583927497 ,  1.000000000000000000 ,  0.133103100731075147 ,  0.794111156893883341 ,  1.000000000000000000 ,  0.045864202819237823 ,  0.970880218173576504  } },
/* Chebyshev Ripple = 1.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  0.838865636156314820 ,  0.643832629622071884  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.451007753530300870 ,  1.000000000000000000 ,  0.451007753530300814 ,  0.828113685452342208  },
/* 4 Poles*/  { 1.000000000000000000 ,  0.626051844653781497 ,  0.241246023523072284 ,  1.000000000000000000 ,  0.259319164804290347 ,  0.851796713564825447  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.279661491910629834 ,  1.000000000000000000 ,  0.452501799255902892 ,  0.400633207634350286 ,  1.000000000000000000 ,  0.172840307345273059 ,  0.922324026336472813  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.449210294562683909 ,  0.116820493775647322 ,  1.000000000000000000 ,  0.328844758902865963 ,  0.522449696518754836 ,  1.000000000000000000 ,  0.120365535659817960 ,  0.928078899261862489  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.201865873906698634 ,  1.000000000000000000 ,  0.363749735763701509 ,  0.222557091892173653 ,  1.000000000000000000 ,  0.251722627448255243 ,  0.631074298816514223 ,  1.000000000000000000 ,  0.089838765591252381 ,  0.958679662774736596  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.345253318776246043 ,  0.067594987878632384 ,  1.000000000000000000 ,  0.292691630721690854 ,  0.327924604437264344 ,  1.000000000000000000 ,  0.195570295145997830 ,  0.696086278861869578 ,  1.000000000000000000 ,  0.068675154983466880 ,  0.956415895420501649  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.157484947830803745 ,  1.000000000000000000 ,  0.295974886722919994 ,  0.139085071767545754 ,  1.000000000000000000 ,  0.241280938321338706 ,  0.428461211454382429 ,  1.000000000000000000 ,  0.157484947830803718 ,  0.757527616036752782 ,  1.000000000000000000 ,  0.054693948401581316 ,  0.972310457644848136  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.279221076366322096 ,  0.043796118470855842 ,  1.000000000000000000 ,  0.251888971963952546 ,  0.220565179750548623 ,  1.000000000000000000 ,  0.199900219972086596 ,  0.506583529060504523 ,  1.000000000000000000 ,  0.128343841662622526 ,  0.792601878370460366 ,  1.000000000000000000 ,  0.044224273907094554 ,  0.969370939650153196  } } } ;

 const double InvChebyDenominator[19][9][24] = {
/* Inv Cheby Stop Band Attenuation = 10.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.311743927225877070 ,  1.258220241403030350  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.827785671487271110 ,  1.000000000000000000 ,  0.650774177234397344 ,  1.189475716522949260  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.591426683459738370 ,  3.268499812483622870 ,  1.000000000000000000 ,  0.367255764130154649 ,  1.118288462461557220  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.857329036610875670 ,  1.000000000000000000 ,  1.320990918667306780 ,  2.332772818897076220 ,  1.000000000000000000 ,  0.234062988557124207 ,  1.082132998790198860  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.834077367545946300 ,  6.725087149238067500 ,  1.000000000000000000 ,  0.763101542310007264 ,  1.828429214005653280 ,  1.000000000000000000 ,  0.161629314545555985 ,  1.058046422549036690  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  3.921365589542033620 ,  1.000000000000000000 ,  1.895561446734370210 ,  4.125108921572491200 ,  1.000000000000000000 ,  0.496093389722789158 ,  1.560060438726290630 ,  1.000000000000000000 ,  0.118141663985849812 ,  1.040973196562926080  },
/* 8 Poles*/  { 1.000000000000000000 ,  5.081638727609984580 ,  11.570599110363970000 ,  1.000000000000000000 ,  1.080801233073232390 ,  2.902855712836433620 ,  1.000000000000000000 ,  0.350667098862769311 ,  1.409555081816159520 ,  1.000000000000000000 ,  0.090293398702446834 ,  1.033585182700645260  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  5.003720407577914030 ,  1.000000000000000000 ,  2.457424420762000850 ,  6.542705802011738570 ,  1.000000000000000000 ,  0.697919086603031036 ,  2.279366430918263740 ,  1.000000000000000000 ,  0.261652214156691543 ,  1.309234523663641130 ,  1.000000000000000000 ,  0.071115073143184895 ,  1.024600279583609330  },
/* 10 Poles*/  { 1.000000000000000000 ,  6.324139991578501710 ,  17.751650775299584200 ,  1.000000000000000000 ,  1.379116936630196740 ,  4.291186654771662390 ,  1.000000000000000000 ,  0.491478614572805284 ,  1.926978285911790540 ,  1.000000000000000000 ,  0.203455744244775305 ,  1.242454200716920140 ,  1.000000000000000000 ,  0.057485415212231467 ,  1.018784734511209770  } },
/* Inv Cheby Stop Band Attenuation = 15.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.378678405883137610 ,  1.155934531008836030  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.492257603974662230 ,  1.000000000000000000 ,  0.769425042077035770 ,  1.148180369727984430  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.514315240222505740 ,  2.513130116633195410 ,  1.000000000000000000 ,  0.458598192453551878 ,  1.106632118389870860  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.224939442266187230 ,  1.000000000000000000 ,  1.516091063306969080 ,  2.084758928596503140 ,  1.000000000000000000 ,  0.299022731311976486 ,  1.076490097860002050  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.628112250473910730 ,  4.912714159932955130 ,  1.000000000000000000 ,  0.940756733416945212 ,  1.740110758433538460 ,  1.000000000000000000 ,  0.209224711545501652 ,  1.057307483330768740  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  3.010019339902405560 ,  1.000000000000000000 ,  2.148764536001866790 ,  3.589370865455120190 ,  1.000000000000000000 ,  0.630937209751100570 ,  1.522986581298701790 ,  1.000000000000000000 ,  0.154055700717191268 ,  1.041948346918202040  },
/* 8 Poles*/  { 1.000000000000000000 ,  4.761745074051038530 ,  8.286538015360701340 ,  1.000000000000000000 ,  1.321914769376299590 ,  2.713551062576414540 ,  1.000000000000000000 ,  0.452704385486920324 ,  1.390774304879798700 ,  1.000000000000000000 ,  0.118219204818245730 ,  1.034268346579636290  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  3.813536590313469700 ,  1.000000000000000000 ,  2.767094423554218970 ,  5.614823187741383670 ,  1.000000000000000000 ,  0.883054173876145487 ,  2.198018296167024930 ,  1.000000000000000000 ,  0.340646166517283078 ,  1.299066620365910250 ,  1.000000000000000000 ,  0.093378333570944674 ,  1.025353955919390710  },
/* 10 Poles*/  { 1.000000000000000000 ,  5.903259724316964880 ,  12.614987335034427900 ,  1.000000000000000000 ,  1.681139307010449000 ,  3.982333922548636720 ,  1.000000000000000000 ,  0.633128777311053326 ,  1.889826376987998600 ,  1.000000000000000000 ,  0.266475042904362858 ,  1.238866845877986570 ,  1.000000000000000000 ,  0.075704675240355221 ,  1.021421779834509990  } },
/* Inv Cheby Stop Band Attenuation = 20.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.402739726027397270 ,  1.093154854986342790  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.312207506898469770 ,  1.000000000000000000 ,  0.848622496616743272 ,  1.113568810583411390  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.386128309597170820 ,  2.030711379474377410 ,  1.000000000000000000 ,  0.530431744133721073 ,  1.089832114724946120  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.861984049100175120 ,  1.000000000000000000 ,  1.622645038492212420 ,  1.867290304178559350 ,  1.000000000000000000 ,  0.354863485249677812 ,  1.069116199384928300  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.351171118385840590 ,  3.755145564023414370 ,  1.000000000000000000 ,  1.074029683470913540 ,  1.644012943298132520 ,  1.000000000000000000 ,  0.251646929420436594 ,  1.052372171383425090  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.476858043197730730 ,  1.000000000000000000 ,  2.266626386791274150 ,  3.115596996219575750 ,  1.000000000000000000 ,  0.744858557759659479 ,  1.479502073916585840 ,  1.000000000000000000 ,  0.187061316535187788 ,  1.041080311397485140  },
/* 8 Poles*/  { 1.000000000000000000 ,  4.347023456664419250 ,  6.185680142467987250 ,  1.000000000000000000 ,  1.495010259283464870 ,  2.509384471035094770 ,  1.000000000000000000 ,  0.542751237246338003 ,  1.363425048296887710 ,  1.000000000000000000 ,  0.144067469235413137 ,  1.030622492758708650  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  3.109999135348678670 ,  1.000000000000000000 ,  2.894327857868007840 ,  4.789522092797137810 ,  1.000000000000000000 ,  1.035665168209091820 ,  2.102304772636275580 ,  1.000000000000000000 ,  0.412683704500779069 ,  1.283445964172494240 ,  1.000000000000000000 ,  0.114272002171012216 ,  1.023292708037342400  },
/* 10 Poles*/  { 1.000000000000000000 ,  5.370127475345468860 ,  9.349830592175084830 ,  1.000000000000000000 ,  1.895734750108858610 ,  3.658775489706596050 ,  1.000000000000000000 ,  0.757967741725012134 ,  1.843337230745371480 ,  1.000000000000000000 ,  0.325256882892554555 ,  1.232022969646768610 ,  1.000000000000000000 ,  0.093012011146834134 ,  1.022458448185685100  } },
/* Inv Cheby Stop Band Attenuation = 25.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.404180081292161870 ,  1.044603205432956820  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.200048360691325700 ,  1.000000000000000000 ,  0.897557017430925086 ,  1.077111827394977570  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.265166652440004920 ,  1.716033526433079490 ,  1.000000000000000000 ,  0.586452395892203660 ,  1.072590779760518750  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.628323956879697660 ,  1.000000000000000000 ,  1.674752872690942860 ,  1.685403547401800980 ,  1.000000000000000000 ,  0.402447533804981550 ,  1.060321879719432920  },
/* 6 Poles*/  { 1.000000000000000000 ,  3.092185215951660290 ,  2.993270327526697640 ,  1.000000000000000000 ,  1.172083806038357820 ,  1.549879316777233120 ,  1.000000000000000000 ,  0.289440670348211748 ,  1.045652894471133410  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.127246276318933930 ,  1.000000000000000000 ,  2.303688309906433140 ,  2.719579196147474230 ,  1.000000000000000000 ,  0.840570712084023342 ,  1.433945600980837790 ,  1.000000000000000000 ,  0.217530102384922347 ,  1.039767567163921490  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.971761525934524250 ,  4.833060640383693320 ,  1.000000000000000000 ,  1.619785300917988560 ,  2.325006069336097880 ,  1.000000000000000000 ,  0.624207555526590907 ,  1.340920627645270760 ,  1.000000000000000000 ,  0.168701651437893146 ,  1.032040231423779540  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.646949783449009000 ,  1.000000000000000000 ,  2.916973738752002280 ,  4.108302457275102350 ,  1.000000000000000000 ,  1.161471296222060800 ,  2.006643494142355430 ,  1.000000000000000000 ,  0.479283481400222977 ,  1.268639307303104590 ,  1.000000000000000000 ,  0.134231029991589257 ,  1.023053626424456740  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.859526622741579870 ,  7.165362119981861790 ,  1.000000000000000000 ,  2.035743404988936240 ,  3.327409538611025840 ,  1.000000000000000000 ,  0.865489436142155122 ,  1.782546525608699860 ,  1.000000000000000000 ,  0.379488148126256419 ,  1.217350271253635930 ,  1.000000000000000000 ,  0.109336884133385076 ,  1.017884161545649760  } },
/* Inv Cheby Stop Band Attenuation = 30.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.410972400422926490 ,  1.027927478389459860  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.133550601313680910 ,  1.000000000000000000 ,  0.932737008823895541 ,  1.057304597219850710  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.167292871758601970 ,  1.508371137220482660 ,  1.000000000000000000 ,  0.630209915182006752 ,  1.058891660646410050  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.467739520916820250 ,  1.000000000000000000 ,  1.694635525351043670 ,  1.537225763736310700 ,  1.000000000000000000 ,  0.442307748151102520 ,  1.050415631156833340  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.874923815386165590 ,  2.480171646692731710 ,  1.000000000000000000 ,  1.243824266268395600 ,  1.465794875878767780 ,  1.000000000000000000 ,  0.323117834857052033 ,  1.040312640342795800  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.879563942778570640 ,  1.000000000000000000 ,  2.291731055532863600 ,  2.390457213329579830 ,  1.000000000000000000 ,  0.918551875350894664 ,  1.384527043854000270 ,  1.000000000000000000 ,  0.245111163765797835 ,  1.035188234193604730  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.638038111294249260 ,  3.885613339592619120 ,  1.000000000000000000 ,  1.698499868165409540 ,  2.139860254340192110 ,  1.000000000000000000 ,  0.693965049305676929 ,  1.308473423836703730 ,  1.000000000000000000 ,  0.191168737345242146 ,  1.026472745297456470  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.317192134829995530 ,  1.000000000000000000 ,  2.877122962954215880 ,  3.547355035692678180 ,  1.000000000000000000 ,  1.261518544818224450 ,  1.907970794809965340 ,  1.000000000000000000 ,  0.539745619595149884 ,  1.250694304534833770 ,  1.000000000000000000 ,  0.153040555487738289 ,  1.021100181558978640  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.436446399348928260 ,  5.721999416844292700 ,  1.000000000000000000 ,  2.129349205534731840 ,  3.044377499013150690 ,  1.000000000000000000 ,  0.961698827576655257 ,  1.732553211634585200 ,  1.000000000000000000 ,  0.431510496009933520 ,  1.210813059629259000 ,  1.000000000000000000 ,  0.125357537473488961 ,  1.020823145434854420  } },
/* Inv Cheby Stop Band Attenuation = 35.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.409718802787646830 ,  1.011643397711031510  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.086567649885314070 ,  1.000000000000000000 ,  0.952652495257204968 ,  1.035121382929001800  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.086968526405168060 ,  1.361581849110582660 ,  1.000000000000000000 ,  0.662601763493351648 ,  1.043653001704330400  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.355901849330085840 ,  1.000000000000000000 ,  1.700598794491091190 ,  1.425090613949647980 ,  1.000000000000000000 ,  0.476289871674176135 ,  1.044930100246553200  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.696483690965094660 ,  2.120324830296912650 ,  1.000000000000000000 ,  1.294785972003911430 ,  1.390789370394140520 ,  1.000000000000000000 ,  0.352604656421565466 ,  1.034761334055797290  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.696991727567524810 ,  1.000000000000000000 ,  2.254779222062578640 ,  2.123459435529438720 ,  1.000000000000000000 ,  0.981276835336150288 ,  1.335401691458845800 ,  1.000000000000000000 ,  0.269890271918104607 ,  1.029120161085311570  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.365151482535161160 ,  3.229636404189208940 ,  1.000000000000000000 ,  1.749309320182638850 ,  1.980355599325398950 ,  1.000000000000000000 ,  0.755538888882019366 ,  1.280090890075002760 ,  1.000000000000000000 ,  0.212241719659178485 ,  1.024042503181572310  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  2.076516632201675700 ,  1.000000000000000000 ,  2.811842845732478670 ,  3.106781040494652310 ,  1.000000000000000000 ,  1.342298346571696220 ,  1.819284029190474610 ,  1.000000000000000000 ,  0.595493465765565944 ,  1.236552086029624850 ,  1.000000000000000000 ,  0.171051565758988000 ,  1.022732936316624210  },
/* 10 Poles*/  { 1.000000000000000000 ,  4.071993179459319380 ,  4.682349230785268550 ,  1.000000000000000000 ,  2.178119145399904260 ,  2.776370592974928280 ,  1.000000000000000000 ,  1.042164059854555400 ,  1.673893323732787100 ,  1.000000000000000000 ,  0.478930554277936515 ,  1.198125929364993510 ,  1.000000000000000000 ,  0.140370396161892258 ,  1.019106969473149600  } },
/* Inv Cheby Stop Band Attenuation = 40.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.395708404927642250 ,  0.983839369487708670  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.054838302877583620 ,  1.000000000000000000 ,  0.965009249909134392 ,  1.017928719435321260  },
/* 4 Poles*/  { 1.000000000000000000 ,  2.026062450648305810 ,  1.259975046632356710 ,  1.000000000000000000 ,  0.687325352210101603 ,  1.031922697477577920  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.272360809560709070 ,  1.000000000000000000 ,  1.695251312617900740 ,  1.333081596263536730 ,  1.000000000000000000 ,  0.503651639006743812 ,  1.036879231252971100  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.555503615454924660 ,  1.866533884157159710 ,  1.000000000000000000 ,  1.332420487278029640 ,  1.329411576538204410 ,  1.000000000000000000 ,  0.378718098676471659 ,  1.032340549405193820  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.563264743202531950 ,  1.000000000000000000 ,  2.213789889849703930 ,  1.920565630484737380 ,  1.000000000000000000 ,  1.034365727824622770 ,  1.296723273632329310 ,  1.000000000000000000 ,  0.292871565607880968 ,  1.028747688276390630  },
/* 8 Poles*/  { 1.000000000000000000 ,  3.139754454613628450 ,  2.754498529424877160 ,  1.000000000000000000 ,  1.778299596664557390 ,  1.840260681489664930 ,  1.000000000000000000 ,  0.808651012204856468 ,  1.252399728663593060 ,  1.000000000000000000 ,  0.231637822124285547 ,  1.021632138139545680  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.888363234545112230 ,  1.000000000000000000 ,  2.727659430434560490 ,  2.740689705791785170 ,  1.000000000000000000 ,  1.402042999054766260 ,  1.728076273151162520 ,  1.000000000000000000 ,  0.644381511841201071 ,  1.216826355981518890 ,  1.000000000000000000 ,  0.187571040107623765 ,  1.019884748471195700  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.768500445937636560 ,  3.928641216446045360 ,  1.000000000000000000 ,  2.198496433073791940 ,  2.540613526400823390 ,  1.000000000000000000 ,  1.110118263303909500 ,  1.616508416385319390 ,  1.000000000000000000 ,  0.522638795320704341 ,  1.185355458605792390 ,  1.000000000000000000 ,  0.154603954669781080 ,  1.017611208889445560  } },
/* Inv Cheby Stop Band Attenuation = 45.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.392235527883425750 ,  0.974640689921976278  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.038504618682388750 ,  1.000000000000000000 ,  0.977675750161109303 ,  1.015320782116081230  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.985245148806951130 ,  1.194334352552153260 ,  1.000000000000000000 ,  0.707887702092227067 ,  1.028139000254708830  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.209886252981902330 ,  1.000000000000000000 ,  1.685972668547989660 ,  1.260687456976980810 ,  1.000000000000000000 ,  0.525900137192509609 ,  1.029521608864140570  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.435287970339286900 ,  1.670581243363062020 ,  1.000000000000000000 ,  1.355444220846493190 ,  1.270157870628265020 ,  1.000000000000000000 ,  0.400202145644374674 ,  1.024576139660947400  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.458498238762286900 ,  1.000000000000000000 ,  2.167456453012620800 ,  1.754351083557774250 ,  1.000000000000000000 ,  1.076096673343032380 ,  1.258629329725887920 ,  1.000000000000000000 ,  0.313104593757442706 ,  1.026111320010152150  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.955719999217246490 ,  2.404830001342174390 ,  1.000000000000000000 ,  1.793423596206492610 ,  1.721200756502050620 ,  1.000000000000000000 ,  0.854712839369947686 ,  1.227654866793805200 ,  1.000000000000000000 ,  0.249538880560936899 ,  1.020698619541100080  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.741511229668577080 ,  1.000000000000000000 ,  2.642189566197130190 ,  2.448355291221231860 ,  1.000000000000000000 ,  1.447540212631112320 ,  1.645405536413746980 ,  1.000000000000000000 ,  0.688170830719297633 ,  1.198457229628014260 ,  1.000000000000000000 ,  0.203006806974038334 ,  1.017973925193036640  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.516819578488231460 ,  3.370209804783791620 ,  1.000000000000000000 ,  2.200312204581864160 ,  2.337384716085001650 ,  1.000000000000000000 ,  1.167341714849339240 ,  1.562571070102808600 ,  1.000000000000000000 ,  0.562888917455886650 ,  1.173553004066693270 ,  1.000000000000000000 ,  0.168094320424890392 ,  1.017061874443527800  } },
/* Inv Cheby Stop Band Attenuation = 50.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.402259309755434340 ,  0.986284491311338329  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.024887752836723510 ,  1.000000000000000000 ,  0.983724339510509527 ,  1.008207027731716110  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.945399468936384490 ,  1.136479963130644370 ,  1.000000000000000000 ,  0.720135909348745273 ,  1.015647825452535400  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.159753890779940950 ,  1.000000000000000000 ,  1.671563822381944450 ,  1.198122326337667910 ,  1.000000000000000000 ,  0.542591191250293536 ,  1.018183880838450910  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.340289018217220020 ,  1.526233669664568860 ,  1.000000000000000000 ,  1.372142924190814290 ,  1.222389643527582060 ,  1.000000000000000000 ,  0.418853434472524877 ,  1.019438811631399490  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.375610014649137680 ,  1.000000000000000000 ,  2.121544295348003930 ,  1.619599568405144300 ,  1.000000000000000000 ,  1.109031529040180650 ,  1.223432102146045920 ,  1.000000000000000000 ,  0.330899351175158007 ,  1.022799188427865320  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.800289418412406310 ,  2.134498071540851690 ,  1.000000000000000000 ,  1.796394870762473950 ,  1.615184772137459040 ,  1.000000000000000000 ,  0.893042293895149997 ,  1.201710471378326650 ,  1.000000000000000000 ,  0.265530167364654901 ,  1.017524629898299170  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.625707005583345350 ,  1.000000000000000000 ,  2.562140434578987320 ,  2.216304332740099530 ,  1.000000000000000000 ,  1.482667937212884810 ,  1.573266195017896110 ,  1.000000000000000000 ,  0.727661796414533102 ,  1.182964880126469390 ,  1.000000000000000000 ,  0.217496219676201141 ,  1.018107799247091490  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.300968213552555320 ,  2.935710438134433890 ,  1.000000000000000000 ,  2.185988713616345170 ,  2.155057354842469410 ,  1.000000000000000000 ,  1.212932353070663490 ,  1.506757562504172920 ,  1.000000000000000000 ,  0.598657938296599856 ,  1.158307271089118020 ,  1.000000000000000000 ,  0.180487233557873550 ,  1.013458217964134270  } },
/* Inv Cheby Stop Band Attenuation = 55.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.391719686558097860 ,  0.970167071103526624  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.014141157446753590 ,  1.000000000000000000 ,  0.986269305481331071 ,  1.000216295015042610  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.922298811548775580 ,  1.102393501641051100 ,  1.000000000000000000 ,  0.731859924224782965 ,  1.013256500832291800  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.128001570927845250 ,  1.000000000000000000 ,  1.668029971654551690 ,  1.162855935946534470 ,  1.000000000000000000 ,  0.559236850041836098 ,  1.020688274190062690  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.268782140277230570 ,  1.422735010471249060 ,  1.000000000000000000 ,  1.386777399702702640 ,  1.187946152283956950 ,  1.000000000000000000 ,  0.435694634195551600 ,  1.019673148222500990  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.310009341179098730 ,  1.000000000000000000 ,  2.079740040061030190 ,  1.511971709992104620 ,  1.000000000000000000 ,  1.135658576817915270 ,  1.193061490008708870 ,  1.000000000000000000 ,  0.346673003365712773 ,  1.020454266187951880  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.676559619063038300 ,  1.933366559120672790 ,  1.000000000000000000 ,  1.796615253772442240 ,  1.530805173247558270 ,  1.000000000000000000 ,  0.927379588226539875 ,  1.182577970360970680 ,  1.000000000000000000 ,  0.280528671025226939 ,  1.018715112716005010  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.530304374301509900 ,  1.000000000000000000 ,  2.485079940165402230 ,  2.023496097981213100 ,  1.000000000000000000 ,  1.507111213234476830 ,  1.505355794228616650 ,  1.000000000000000000 ,  0.761857208607320646 ,  1.165873418924920910 ,  1.000000000000000000 ,  0.230640954220845962 ,  1.016281500558617210  },
/* 10 Poles*/  { 1.000000000000000000 ,  3.122511317104476710 ,  2.604041814276904620 ,  1.000000000000000000 ,  2.166228708022558180 ,  2.002568048315582330 ,  1.000000000000000000 ,  1.251432626929847690 ,  1.457761012685790720 ,  1.000000000000000000 ,  0.631631269733050504 ,  1.145989707005963920 ,  1.000000000000000000 ,  0.192235785900474521 ,  1.012198421595652990  } },
/* Inv Cheby Stop Band Attenuation = 60.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.407196883859266330 ,  0.991092627599182263  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.009755390228078700 ,  1.000000000000000000 ,  0.990792417783627033 ,  1.000457984454127920  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.903296643555809850 ,  1.075557682783145670 ,  1.000000000000000000 ,  0.740062123364798707 ,  1.009650553877427860  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.098904798213610420 ,  1.000000000000000000 ,  1.657273028797658030 ,  1.125554404887879390 ,  1.000000000000000000 ,  0.570330853387790215 ,  1.014085507861500310  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.203531478997941220 ,  1.333686960700858130 ,  1.000000000000000000 ,  1.392727993053127780 ,  1.151488822823731880 ,  1.000000000000000000 ,  0.448502795697459156 ,  1.013088412725620780  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.254650924099884210 ,  1.000000000000000000 ,  2.037973696975427180 ,  1.418997744147588500 ,  1.000000000000000000 ,  1.154613182504184540 ,  1.161716271932709880 ,  1.000000000000000000 ,  0.359766288110953292 ,  1.014244138322303760  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.566498395744171380 ,  1.765764108483153900 ,  1.000000000000000000 ,  1.787892589933718800 ,  1.450977472977310830 ,  1.000000000000000000 ,  0.954091024858005121 ,  1.158821299482309500 ,  1.000000000000000000 ,  0.293276436951687369 ,  1.014394937096681160  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.450948124432342110 ,  1.000000000000000000 ,  2.413171196005414740 ,  1.863048694502804640 ,  1.000000000000000000 ,  1.523443758474531950 ,  1.442760850164854110 ,  1.000000000000000000 ,  0.791349845331711266 ,  1.148207573853879500 ,  1.000000000000000000 ,  0.242516261398069949 ,  1.013193801822450220  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.976146361108285010 ,  2.349500968984371770 ,  1.000000000000000000 ,  2.145397269888646540 ,  1.877448833566353950 ,  1.000000000000000000 ,  1.284892711555139620 ,  1.416847583606618110 ,  1.000000000000000000 ,  0.662434478000394344 ,  1.137725347256816950 ,  1.000000000000000000 ,  0.203484311912251914 ,  1.014237726559899850  } },
/* Inv Cheby Stop Band Attenuation = 65.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.348668786450448300 ,  0.909965458954915984  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.005579584356542930 ,  1.000000000000000000 ,  0.992687697609315189 ,  0.998226482357828737  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.883086265229397820 ,  1.049156086100982320 ,  1.000000000000000000 ,  0.743873298843128472 ,  1.000563241879347310  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.074864846447587400 ,  1.000000000000000000 ,  1.645820451787161300 ,  1.093323477436507260 ,  1.000000000000000000 ,  0.578414641713191013 ,  1.005960151650620920  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.159152668819410260 ,  1.274273189890221760 ,  1.000000000000000000 ,  1.402022964907971620 ,  1.130298360726516680 ,  1.000000000000000000 ,  0.461080458105243118 ,  1.015555021618472110  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.211390800039175990 ,  1.000000000000000000 ,  2.004012027106834020 ,  1.347239521414851150 ,  1.000000000000000000 ,  1.171208353855270670 ,  1.137782061999756070 ,  1.000000000000000000 ,  0.371661244903932209 ,  1.011650914821610180  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.474860591326553120 ,  1.633230319170472900 ,  1.000000000000000000 ,  1.777629175063495600 ,  1.383774809759394930 ,  1.000000000000000000 ,  0.976784936147562233 ,  1.137969487648452520 ,  1.000000000000000000 ,  0.304725822250804190 ,  1.010983725957264490  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.384560250859863340 ,  1.000000000000000000 ,  2.347607527817152650 ,  1.729503880171222360 ,  1.000000000000000000 ,  1.533895563504349190 ,  1.386192958695307900 ,  1.000000000000000000 ,  0.816803238343939642 ,  1.130913296584624920 ,  1.000000000000000000 ,  0.253233797965976060 ,  1.009562713380377290  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.842864290466297290 ,  2.132111010579003980 ,  1.000000000000000000 ,  2.115641652636338410 ,  1.758874434509960500 ,  1.000000000000000000 ,  1.308388434408289000 ,  1.370646009505487320 ,  1.000000000000000000 ,  0.688145771008210061 ,  1.122812637596457330 ,  1.000000000000000000 ,  0.213284381191599259 ,  1.009950814046126410  } },
/* Inv Cheby Stop Band Attenuation = 70.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.400515518735218070 ,  0.981032088694887006  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.991684852612141676 ,  1.000000000000000000 ,  0.983011176310567558 ,  0.974837293495632862  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.870724866040420320 ,  1.032756115027686270 ,  1.000000000000000000 ,  0.747806281605958079 ,  0.996672901491659657  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.060114164721635890 ,  1.000000000000000000 ,  1.642489653829132700 ,  1.076137188427225060 ,  1.000000000000000000 ,  0.587054929116052837 ,  1.006975760482702990  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.117517472317380630 ,  1.220907248748018810 ,  1.000000000000000000 ,  1.405369624755522960 ,  1.106891387321890850 ,  1.000000000000000000 ,  0.470465966010205294 ,  1.012351728768669860  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.174626930715375830 ,  1.000000000000000000 ,  1.972040518894808690 ,  1.285511622254326580 ,  1.000000000000000000 ,  1.183122177168607350 ,  1.114474661402504290 ,  1.000000000000000000 ,  0.381541822922728535 ,  1.007027277009503900  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.399745347626236040 ,  1.529069348132570560 ,  1.000000000000000000 ,  1.767964094374252950 ,  1.328810412048559540 ,  1.000000000000000000 ,  0.996708888715622243 ,  1.121154143315816980 ,  1.000000000000000000 ,  0.315170857293724072 ,  1.009592907424867070  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.332849791856467900 ,  1.000000000000000000 ,  2.295788573876425880 ,  1.628160770422168560 ,  1.000000000000000000 ,  1.544921869264544560 ,  1.344013921372421150 ,  1.000000000000000000 ,  0.841395351623903664 ,  1.121453619280919020 ,  1.000000000000000000 ,  0.263710723431464122 ,  1.012065855105556580  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.734790223298976920 ,  1.964403481730797370 ,  1.000000000000000000 ,  2.090150050541326630 ,  1.664267699998824400 ,  1.000000000000000000 ,  1.329965199881189220 ,  1.334387234890608060 ,  1.000000000000000000 ,  0.712636071477174227 ,  1.113647227010326150 ,  1.000000000000000000 ,  0.222781012563363856 ,  1.010351139921389810  } },
/* Inv Cheby Stop Band Attenuation = 75.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.365404526803318590 ,  0.932330555330486255  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.995553995184330409 ,  1.000000000000000000 ,  0.989616088190835574 ,  0.985216250297075158  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.863070405706885420 ,  1.022364508786347550 ,  1.000000000000000000 ,  0.751399852016224501 ,  0.995458794757917320  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.047845199454371600 ,  1.000000000000000000 ,  1.638485511158496610 ,  1.061089686113112270 ,  1.000000000000000000 ,  0.593577340969724587 ,  1.006380196820724660  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.075940714754564990 ,  1.169865465720984380 ,  1.000000000000000000 ,  1.402445071799597540 ,  1.079606798646178150 ,  1.000000000000000000 ,  0.476562297742949614 ,  1.002278011684131000  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.147592480954504610 ,  1.000000000000000000 ,  1.949851400592354620 ,  1.241793632397044430 ,  1.000000000000000000 ,  1.195945262526209700 ,  1.100625693328663780 ,  1.000000000000000000 ,  0.391167230830512580 ,  1.008670431396401000  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.339860442125835770 ,  1.448695425156810270 ,  1.000000000000000000 ,  1.760677552145249570 ,  1.285862462130378690 ,  1.000000000000000000 ,  1.015091081190802710 ,  1.109499513583389830 ,  1.000000000000000000 ,  0.324938877088481459 ,  1.011409485827122980  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.286427185566514140 ,  1.000000000000000000 ,  2.244747876476637760 ,  1.536515573904969400 ,  1.000000000000000000 ,  1.549312184236841580 ,  1.300888826121381130 ,  1.000000000000000000 ,  0.861082134992404713 ,  1.107719467459884480 ,  1.000000000000000000 ,  0.272627580257334190 ,  1.009845123312926200  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.642102860115593370 ,  1.826905408228961300 ,  1.000000000000000000 ,  2.065081202657790270 ,  1.582860045580560150 ,  1.000000000000000000 ,  1.347585831131355990 ,  1.301540543797931710 ,  1.000000000000000000 ,  0.734637302593179609 ,  1.105127736968764650 ,  1.000000000000000000 ,  0.231543565464794238 ,  1.010849597569745570  } },
/* Inv Cheby Stop Band Attenuation = 80.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.279935998399919890 ,  0.819199999999999817  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.994098741083508597 ,  1.000000000000000000 ,  0.990056635800140139 ,  0.984214055250292930  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.855454827954325920 ,  1.012583591163597420 ,  1.000000000000000000 ,  0.753336405402613707 ,  0.992533423500280065  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.036158816297733100 ,  1.000000000000000000 ,  1.631925277591518730 ,  1.045054538824631510 ,  1.000000000000000000 ,  0.597608187921688683 ,  1.001914140449260190  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.052988999734294940 ,  1.141359809722198420 ,  1.000000000000000000 ,  1.407123734230006250 ,  1.068629152898326270 ,  1.000000000000000000 ,  0.484189139390615308 ,  1.004612424052293340  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.125193770699807420 ,  1.000000000000000000 ,  1.930592510328126950 ,  1.205530370565576440 ,  1.000000000000000000 ,  1.206408696410864680 ,  1.088585207065772440 ,  1.000000000000000000 ,  0.399484850391930935 ,  1.010012534906467700  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.286094245916479030 ,  1.378979688559766710 ,  1.000000000000000000 ,  1.750939426043098160 ,  1.245840423848983080 ,  1.000000000000000000 ,  1.029386757667038130 ,  1.096168421334044350 ,  1.000000000000000000 ,  0.333169799855317639 ,  1.010340077246260520  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.248042930639572830 ,  1.000000000000000000 ,  2.201346057720732220 ,  1.461847376715522890 ,  1.000000000000000000 ,  1.552817588030857450 ,  1.264928576091563620 ,  1.000000000000000000 ,  0.878898772712913168 ,  1.096903400032140930 ,  1.000000000000000000 ,  0.280885288969461022 ,  1.009388361942367360  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.555398937358764220 ,  1.703884104655725150 ,  1.000000000000000000 ,  2.035562263368682960 ,  1.504543593034264550 ,  1.000000000000000000 ,  1.358308057994242150 ,  1.265069961067103010 ,  1.000000000000000000 ,  0.752339814047739086 ,  1.091361299291101710 ,  1.000000000000000000 ,  0.238959748869920496 ,  1.005989807340306410  } },
/* Inv Cheby Stop Band Attenuation = 85.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.279783465319857870 ,  0.818968913059438086  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.973961384746547809 ,  1.000000000000000000 ,  0.971262146381364988 ,  0.945971825041497016  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.841086772781239670 ,  0.995907739259445202 ,  1.000000000000000000 ,  0.751251124721916019 ,  0.981082818376105004  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.022824641178561760 ,  1.000000000000000000 ,  1.620062920875770640 ,  1.024107211129537730 ,  1.000000000000000000 ,  0.598389954733010598 ,  0.990314451754755409  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.027135366871950950 ,  1.110620613452322880 ,  1.000000000000000000 ,  1.405880075175099050 ,  1.052179972360050230 ,  1.000000000000000000 ,  0.488863898732728508 ,  0.999582153477477364  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.101599805361377360 ,  1.000000000000000000 ,  1.905375702486350240 ,  1.164835755027771040 ,  1.000000000000000000 ,  1.209524026882669910 ,  1.068511007414799870 ,  1.000000000000000000 ,  0.404827841487071349 ,  1.002059139915743020  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.237051677058274010 ,  1.317373387651893160 ,  1.000000000000000000 ,  1.739064857976790220 ,  1.208025790302193810 ,  1.000000000000000000 ,  1.039932712884227510 ,  1.081117954860539410 ,  1.000000000000000000 ,  0.339924724651042454 ,  1.006361091623163340  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.212725980497607820 ,  1.000000000000000000 ,  2.158401605915357010 ,  1.392769106589447860 ,  1.000000000000000000 ,  1.551381069479979890 ,  1.227996721019471770 ,  1.000000000000000000 ,  0.892519497841240184 ,  1.082381583132777880 ,  1.000000000000000000 ,  0.287701474611344876 ,  1.004626301226958280  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.488502331686853970 ,  1.611832593742495100 ,  1.000000000000000000 ,  2.014525593287609160 ,  1.446417653826672870 ,  1.000000000000000000 ,  1.371068734172347090 ,  1.240440775482927860 ,  1.000000000000000000 ,  0.770550230332909369 ,  1.085815403799648800 ,  1.000000000000000000 ,  0.246521465313924409 ,  1.008147570406413560  } },
/* Inv Cheby Stop Band Attenuation = 90.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.151656824264714500 ,  0.663177691957743898  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.989138147606042795 ,  1.000000000000000000 ,  0.987269971264332247 ,  0.976546390563472477  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.838595937325808280 ,  0.992431194271848338 ,  1.000000000000000000 ,  0.753054142437604046 ,  0.981332083192196669  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.023337706875407480 ,  1.000000000000000000 ,  1.628108898739918600 ,  1.029709659107469920 ,  1.000000000000000000 ,  0.605500509955869237 ,  1.002584832953064440  },
/* 6 Poles*/  { 1.000000000000000000 ,  2.007623092684474480 ,  1.087624709578899780 ,  1.000000000000000000 ,  1.405831180318708910 ,  1.040372419765752230 ,  1.000000000000000000 ,  0.493145042152112689 ,  0.997054968518867546  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.085922288368826430 ,  1.000000000000000000 ,  1.890780520283765980 ,  1.139462628808682430 ,  1.000000000000000000 ,  1.216301385769848590 ,  1.059206405817545660 ,  1.000000000000000000 ,  0.410884848281527437 ,  1.002577615438074110  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.200511621237441150 ,  1.272222349591585420 ,  1.000000000000000000 ,  1.732275676879714290 ,  1.181364508575828640 ,  1.000000000000000000 ,  1.051290926199755840 ,  1.072993870383885270 ,  1.000000000000000000 ,  0.346676960769541687 ,  1.007633385961230090  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.183846187631571570 ,  1.000000000000000000 ,  2.123027682552836030 ,  1.337319338596297810 ,  1.000000000000000000 ,  1.550895627541668540 ,  1.198378170203968860 ,  1.000000000000000000 ,  0.905316312132397272 ,  1.071755264718612600 ,  1.000000000000000000 ,  0.294128236387998454 ,  1.002609402531731140  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.424940198419081040 ,  1.527353104342780820 ,  1.000000000000000000 ,  1.990341080620015470 ,  1.389648519160114890 ,  1.000000000000000000 ,  1.378454321778118890 ,  1.212734473966798450 ,  1.000000000000000000 ,  0.785075395875319049 ,  1.075778717192790390 ,  1.000000000000000000 ,  0.252869417929180107 ,  1.005592935354897580  } },
/* Inv Cheby Stop Band Attenuation = 95.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.079533450088618010 ,  0.582706597081554256  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.964688373070781591 ,  1.000000000000000000 ,  0.963446810718849189 ,  0.929425936372599959  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.847286422856282420 ,  1.001244770208056690 ,  1.000000000000000000 ,  0.758744846852155708 ,  0.992835876611352086  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.009851228383136680 ,  1.000000000000000000 ,  1.612303603218359970 ,  1.006274766511281890 ,  1.000000000000000000 ,  0.602907614364265321 ,  0.985135131805327191  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.993666351909618270 ,  1.071183901272108940 ,  1.000000000000000000 ,  1.407118121472918930 ,  1.032763522187668980 ,  1.000000000000000000 ,  0.497207531119020885 ,  0.997003777268263724  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.072777770782792130 ,  1.000000000000000000 ,  1.878214767557132350 ,  1.118189053569766940 ,  1.000000000000000000 ,  1.221844265239842240 ,  1.051153814545247700 ,  1.000000000000000000 ,  0.416068260305577253 ,  1.002936606566013470  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.167033820482256790 ,  1.231811873619775400 ,  1.000000000000000000 ,  1.724154175496862780 ,  1.156065264879039930 ,  1.000000000000000000 ,  1.059873370812771220 ,  1.063573843466513620 ,  1.000000000000000000 ,  0.352250406302070163 ,  1.006626624962224440  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.161003964796546220 ,  1.000000000000000000 ,  2.095454673874121100 ,  1.294482435322635850 ,  1.000000000000000000 ,  1.552369716654465440 ,  1.176372606090426930 ,  1.000000000000000000 ,  0.917990863183294592 ,  1.065791031802808540 ,  1.000000000000000000 ,  0.300384615588190518 ,  1.004179065819795320  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.371547284281769310 ,  1.458257189298254320 ,  1.000000000000000000 ,  1.969710901130339530 ,  1.342591426553384480 ,  1.000000000000000000 ,  1.385374031185459880 ,  1.189883125426754340 ,  1.000000000000000000 ,  0.798627794075278841 ,  1.068365661563456910 ,  1.000000000000000000 ,  0.258850501225117380 ,  1.004936994446782310  } },
/* Inv Cheby Stop Band Attenuation = 100.00 db */
/* 2 Poles*/  {{ 1.000000000000000000 ,  1.079385377703441010 ,  0.582542222222222072  },
/* 3 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.973070843980577083 ,  1.000000000000000000 ,  0.972217510647201078 ,  0.946036513618167652  },
/* 4 Poles*/  { 1.000000000000000000 ,  1.827649354496274860 ,  0.979639104949388884 ,  1.000000000000000000 ,  0.752264343364171251 ,  0.973462885039439119  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.009271065516306630 ,  1.000000000000000000 ,  1.615852057484562290 ,  1.007910055730136370 ,  1.000000000000000000 ,  0.606868632417707832 ,  0.991037649090376172  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.984405896876885620 ,  1.060152446655144630 ,  1.000000000000000000 ,  1.409673369406136520 ,  1.028762398723269690 ,  1.000000000000000000 ,  0.501138069899939032 ,  0.999177750240415219  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.061568888742655760 ,  1.000000000000000000 ,  1.867132542234361430 ,  1.099976863023891750 ,  1.000000000000000000 ,  1.226201221463709250 ,  1.043879999451095220 ,  1.000000000000000000 ,  0.420431590960498369 ,  1.002865413318516200  },
/* 8 Poles*/  { 1.000000000000000000 ,  2.135514424800608600 ,  1.194615439591574680 ,  1.000000000000000000 ,  1.714482599377891600 ,  1.131322054340908600 ,  1.000000000000000000 ,  1.065727614645015200 ,  1.052462920428092640 ,  1.000000000000000000 ,  0.356654592923025982 ,  1.003024748814739020  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  1.139388086898976530 ,  1.000000000000000000 ,  2.067678722977177590 ,  1.253542090457376630 ,  1.000000000000000000 ,  1.550516466868464780 ,  1.153092360867266520 ,  1.000000000000000000 ,  0.927509757967726167 ,  1.056793568710984180 ,  1.000000000000000000 ,  0.305469733629089024 ,  1.002165931371796550  },
/* 10 Poles*/  { 1.000000000000000000 ,  2.327768166705771250 ,  1.402805227239012800 ,  1.000000000000000000 ,  1.953188972946947600 ,  1.304791015324962890 ,  1.000000000000000000 ,  1.392620612859483350 ,  1.172263910098618740 ,  1.000000000000000000 ,  0.811675876117835982 ,  1.064175978592468260 ,  1.000000000000000000 ,  0.264605761204222634 ,  1.006802840737495820  } } } ;

 const double Elliptic_00Denominator[13][7][24] = {
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.707060687857839820 ,  0.959234259115402721 ,  1.000000000000000000 ,  0.561806424647928959 ,  0.992452196011106347  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.964076647678812404 ,  1.000000000000000000 ,  1.242692049088568900 ,  0.962032219338782357 ,  1.000000000000000000 ,  0.350709197513432436 ,  0.995137516529268629  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.718678815500733310 ,  0.935791248228665440 ,  1.000000000000000000 ,  0.821853600114722305 ,  0.974965676686633587 ,  1.000000000000000000 ,  0.214842884696682307 ,  0.995663832718992614  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.954250494044957098 ,  1.000000000000000000 ,  1.274525110048725150 ,  0.943247052993431256 ,  1.000000000000000000 ,  0.523083359021151106 ,  0.980613867185063159 ,  1.000000000000000000 ,  0.133049249765097938 ,  0.995738386677721077  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.698387117214160470 ,  0.906170352042001803 ,  1.000000000000000000 ,  0.857464538518738384 ,  0.955531016603217154 ,  1.000000000000000000 ,  0.328839513471406830 ,  0.985461062830528478 ,  1.000000000000000000 ,  0.082815402772769720 ,  0.996310868004765204  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.737088928742672289 ,  1.000000000000000000 ,  1.175070490474753540 ,  0.648559442238737183 ,  1.000000000000000000 ,  0.666083412658274310 ,  0.824830635721104377 ,  1.000000000000000000 ,  0.305034354680035480 ,  0.940962424068639502 ,  1.000000000000000000 ,  0.085821579480675730 ,  0.990948369346403180  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.374268489004199180 ,  0.560321207697289436 ,  1.000000000000000000 ,  0.907073175231186135 ,  0.724625254497860638 ,  1.000000000000000000 ,  0.469773692260677056 ,  0.875999298528157055 ,  1.000000000000000000 ,  0.208057706331800957 ,  0.960316882739669087 ,  1.000000000000000000 ,  0.057913648272849186 ,  0.994742229663233002  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.667499182355920210 ,  0.900053051325416020 ,  1.000000000000000000 ,  0.583609686888070689 ,  0.984806106801004888  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.919434341390698462 ,  1.000000000000000000 ,  1.243151421823821990 ,  0.912751865182009681 ,  1.000000000000000000 ,  0.370448921083464222 ,  0.987641041479921489  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.640188127762885140 ,  0.835322134127945870 ,  1.000000000000000000 ,  0.852963317280339806 ,  0.932670933637518074 ,  1.000000000000000000 ,  0.235548613852095884 ,  0.990407914557745128  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.893329667386041470 ,  1.000000000000000000 ,  1.261546955754899720 ,  0.866515457223748342 ,  1.000000000000000000 ,  0.559850846368539434 ,  0.954068202917242258 ,  1.000000000000000000 ,  0.148659589821609039 ,  0.993004302943759742  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.615773990827764410 ,  0.805666666763839800 ,  1.000000000000000000 ,  0.880951444085603330 ,  0.902806713309711073 ,  1.000000000000000000 ,  0.360321197515761915 ,  0.968657901444649361 ,  1.000000000000000000 ,  0.093954320036198982 ,  0.994286366710384506  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.737088928742672289 ,  1.000000000000000000 ,  1.175070490474753540 ,  0.648559442238737183 ,  1.000000000000000000 ,  0.666083412658274310 ,  0.824830635721104377 ,  1.000000000000000000 ,  0.305034354680035480 ,  0.940962424068639502 ,  1.000000000000000000 ,  0.085821579480675730 ,  0.990948369346403180  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.374268489004199180 ,  0.560321207697289436 ,  1.000000000000000000 ,  0.907073175231186135 ,  0.724625254497860638 ,  1.000000000000000000 ,  0.469773692260677056 ,  0.875999298528157055 ,  1.000000000000000000 ,  0.208057706331800957 ,  0.960316882739669087 ,  1.000000000000000000 ,  0.057913648272849186 ,  0.994742229663233002  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.639094053049792830 ,  0.860634830340528678 ,  1.000000000000000000 ,  0.597221941734558004 ,  0.976890039445536207  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.879849604548582298 ,  1.000000000000000000 ,  1.242503505309495050 ,  0.868556856723774584 ,  1.000000000000000000 ,  0.390513918729270160 ,  0.982787728040841802  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.580975537717826730 ,  0.765343947596073382 ,  1.000000000000000000 ,  0.874696180548776514 ,  0.899493542136439173 ,  1.000000000000000000 ,  0.252293882049151819 ,  0.986374884554430009  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.839573799190307835 ,  1.000000000000000000 ,  1.244192234779418320 ,  0.796969041914324494 ,  1.000000000000000000 ,  0.594324672004254317 ,  0.927163701894886172 ,  1.000000000000000000 ,  0.164645310017159235 ,  0.990568961608351950  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.537206906788033620 ,  0.717976946871615285 ,  1.000000000000000000 ,  0.899783384742825043 ,  0.850972210726700262 ,  1.000000000000000000 ,  0.392335913025504746 ,  0.951267211436151783 ,  1.000000000000000000 ,  0.105948613538066519 ,  0.993213198947514475  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.737088928742672289 ,  1.000000000000000000 ,  1.175070490474753540 ,  0.648559442238737183 ,  1.000000000000000000 ,  0.666083412658274310 ,  0.824830635721104377 ,  1.000000000000000000 ,  0.305034354680035480 ,  0.940962424068639502 ,  1.000000000000000000 ,  0.085821579480675730 ,  0.990948369346403180  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.374268489004199180 ,  0.560321207697289436 ,  1.000000000000000000 ,  0.907073175231186135 ,  0.724625254497860638 ,  1.000000000000000000 ,  0.469773692260677056 ,  0.875999298528157055 ,  1.000000000000000000 ,  0.208057706331800957 ,  0.960316882739669087 ,  1.000000000000000000 ,  0.057913648272849186 ,  0.994742229663233002  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.616008538760881570 ,  0.829420782421787517 ,  1.000000000000000000 ,  0.609403460125652385 ,  0.971528562712753452  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.853880895457447053 ,  1.000000000000000000 ,  1.240695553292829660 ,  0.838859423747188249 ,  1.000000000000000000 ,  0.404671479811012091 ,  0.979500743891920544  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.530876708157407950 ,  0.709616090213418982 ,  1.000000000000000000 ,  0.892199001743472109 ,  0.870796909848568501 ,  1.000000000000000000 ,  0.267515891568260833 ,  0.983412453683462195  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.800770792024901867 ,  1.000000000000000000 ,  1.227246691402373410 ,  0.745620593079254879 ,  1.000000000000000000 ,  0.619359169277302502 ,  0.904377909205605013 ,  1.000000000000000000 ,  0.177341185003667939 ,  0.987245348179167204  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.471283884952325760 ,  0.650008740469965485 ,  1.000000000000000000 ,  0.911227520250149081 ,  0.805539593490173345 ,  1.000000000000000000 ,  0.419506060189933860 ,  0.933550475911484323 ,  1.000000000000000000 ,  0.116768706805736630 ,  0.990528682496003121  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.737088928742672289 ,  1.000000000000000000 ,  1.175070490474753540 ,  0.648559442238737183 ,  1.000000000000000000 ,  0.666083412658274310 ,  0.824830635721104377 ,  1.000000000000000000 ,  0.305034354680035480 ,  0.940962424068639502 ,  1.000000000000000000 ,  0.085821579480675730 ,  0.990948369346403180  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.374268489004199180 ,  0.560321207697289436 ,  1.000000000000000000 ,  0.907073175231186135 ,  0.724625254497860638 ,  1.000000000000000000 ,  0.469773692260677056 ,  0.875999298528157055 ,  1.000000000000000000 ,  0.208057706331800957 ,  0.960316882739669087 ,  1.000000000000000000 ,  0.057913648272849186 ,  0.994742229663233002  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.601873336206149470 ,  0.810888278312676070 ,  1.000000000000000000 ,  0.616717577371037162 ,  0.968019600550812820  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.831058847289218750 ,  1.000000000000000000 ,  1.236945072071853110 ,  0.811640321430432299 ,  1.000000000000000000 ,  0.416873861150304781 ,  0.974608410217480126  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.488613884859251170 ,  0.664888438515459646 ,  1.000000000000000000 ,  0.906537942349721293 ,  0.846290286000776715 ,  1.000000000000000000 ,  0.281352294961037863 ,  0.981629286436882298  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.769108455512890643 ,  1.000000000000000000 ,  1.211541431475298140 ,  0.703691999977322347 ,  1.000000000000000000 ,  0.641083524370993141 ,  0.885096665147763750 ,  1.000000000000000000 ,  0.189057846252698308 ,  0.985727227547319784  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.408919413899928360 ,  0.589841636431608252 ,  1.000000000000000000 ,  0.919449864082825807 ,  0.761760284089086914 ,  1.000000000000000000 ,  0.446463494540647210 ,  0.915559365474031828 ,  1.000000000000000000 ,  0.128099081015067090 ,  0.988411817088188482  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.737088928742672289 ,  1.000000000000000000 ,  1.175070490474753540 ,  0.648559442238737183 ,  1.000000000000000000 ,  0.666083412658274310 ,  0.824830635721104377 ,  1.000000000000000000 ,  0.305034354680035480 ,  0.940962424068639502 ,  1.000000000000000000 ,  0.085821579480675730 ,  0.990948369346403180  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.374268489004199180 ,  0.560321207697289436 ,  1.000000000000000000 ,  0.907073175231186135 ,  0.724625254497860638 ,  1.000000000000000000 ,  0.469773692260677056 ,  0.875999298528157055 ,  1.000000000000000000 ,  0.208057706331800957 ,  0.960316882739669087 ,  1.000000000000000000 ,  0.057913648272849186 ,  0.994742229663233002  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.587593720412185270 ,  0.792429038845978040 ,  1.000000000000000000 ,  0.624530675868209095 ,  0.964864760901492424  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.814600061280034038 ,  1.000000000000000000 ,  1.234213664858750060 ,  0.792119844361780401 ,  1.000000000000000000 ,  0.426529063587641033 ,  0.971926583907641306  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.453600051244702040 ,  0.629745989537211059 ,  1.000000000000000000 ,  0.915138783486196372 ,  0.824084518173063829 ,  1.000000000000000000 ,  0.291861976547720281 ,  0.976608021659283843  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.741891878890438283 ,  1.000000000000000000 ,  1.195608420955721620 ,  0.667232051049318264 ,  1.000000000000000000 ,  0.659174204836218802 ,  0.866342865866070566 ,  1.000000000000000000 ,  0.199604157723705461 ,  0.982886584516798667  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.363108961457741410 ,  0.548096954222009436 ,  1.000000000000000000 ,  0.923455644532507614 ,  0.729026337896591214 ,  1.000000000000000000 ,  0.466720231176799449 ,  0.901086294609883653 ,  1.000000000000000000 ,  0.137062053463794270 ,  0.986554507077509690  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.705992577691195389 ,  1.000000000000000000 ,  1.151979908434753510 ,  0.606234018695715249 ,  1.000000000000000000 ,  0.683109841971115017 ,  0.796855332676399519 ,  1.000000000000000000 ,  0.325106096447665083 ,  0.930597654034112431 ,  1.000000000000000000 ,  0.093459912762737879 ,  0.990656115763414502  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.317716437821348400 ,  0.511107455743768258 ,  1.000000000000000000 ,  0.905984336466773565 ,  0.682090521221909385 ,  1.000000000000000000 ,  0.491993384040613213 ,  0.850673083673380570 ,  1.000000000000000000 ,  0.226102310302801063 ,  0.950398779728635068 ,  1.000000000000000000 ,  0.064246509614767688 ,  0.992705766126282252  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.579338404746682740 ,  0.782033174423562238 ,  1.000000000000000000 ,  0.628714562597944004 ,  0.962660673434746483  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.799905299102613321 ,  1.000000000000000000 ,  1.230522642624500620 ,  0.774088022224679762 ,  1.000000000000000000 ,  0.434757013363693212 ,  0.968112372448507452  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.426134741020554040 ,  0.602753676645336634 ,  1.000000000000000000 ,  0.924010879907153138 ,  0.807907115156963229 ,  1.000000000000000000 ,  0.301961149353478231 ,  0.976245752715985016  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.719387595463235829 ,  1.000000000000000000 ,  1.181737252453235240 ,  0.637342540128619350 ,  1.000000000000000000 ,  0.675262193820737378 ,  0.850954073718784620 ,  1.000000000000000000 ,  0.209393090754609351 ,  0.982067271430888233  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.325101998617806980 ,  0.514838629145958682 ,  1.000000000000000000 ,  0.926602909203647185 ,  0.702093232529081068 ,  1.000000000000000000 ,  0.485102601409141909 ,  0.889823069123155230 ,  1.000000000000000000 ,  0.145463379218147670 ,  0.987113640699587291  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.676072608493139104 ,  1.000000000000000000 ,  1.127730976323121360 ,  0.565819811865158062 ,  1.000000000000000000 ,  0.699162137348129820 ,  0.768500526040669874 ,  1.000000000000000000 ,  0.346083326972394745 ,  0.919994982749598234 ,  1.000000000000000000 ,  0.101722151606294955 ,  0.991079876462400922  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.262849980922886140 ,  0.466011775501454284 ,  1.000000000000000000 ,  0.902588524141172299 ,  0.640698061846801226 ,  1.000000000000000000 ,  0.513814881498309561 ,  0.824668612425070391 ,  1.000000000000000000 ,  0.245171568854103544 ,  0.940328026457099342 ,  1.000000000000000000 ,  0.071138599521523044 ,  0.991374088489791916  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.573214671420369410 ,  0.774134908740200189 ,  1.000000000000000000 ,  0.632760862046552797 ,  0.962000491704974481  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.791087215779164299 ,  1.000000000000000000 ,  1.228268352350690540 ,  0.763300470784144758 ,  1.000000000000000000 ,  0.439966336990932572 ,  0.966064522408474735  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.403377884934494450 ,  0.581220520557628961 ,  1.000000000000000000 ,  0.929262893451239136 ,  0.793342590509168000 ,  1.000000000000000000 ,  0.309503362004819038 ,  0.973452352669769594  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.699692549255187424 ,  1.000000000000000000 ,  1.168158511616657510 ,  0.611007444019958656 ,  1.000000000000000000 ,  0.688707391093718813 ,  0.836067097406850590 ,  1.000000000000000000 ,  0.218170938464348790 ,  0.980145892996011181  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.285542679668352010 ,  0.481767255105776615 ,  1.000000000000000000 ,  0.927127116696930709 ,  0.673158136311103550 ,  1.000000000000000000 ,  0.502948672027038191 ,  0.875359863759803902 ,  1.000000000000000000 ,  0.154087732058048543 ,  0.984795660071180801  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.649767367083402769 ,  1.000000000000000000 ,  1.103404581802548410 ,  0.530127257942318630 ,  1.000000000000000000 ,  0.710186059366194167 ,  0.740263046669327074 ,  1.000000000000000000 ,  0.363755458781414909 ,  0.906082205420694509 ,  1.000000000000000000 ,  0.109020277819864036 ,  0.986960376693393093  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.225320956476001920 ,  0.436600503557331876 ,  1.000000000000000000 ,  0.899058367809983383 ,  0.612445176343145992 ,  1.000000000000000000 ,  0.529056607251905175 ,  0.806254823881635385 ,  1.000000000000000000 ,  0.259342476781115050 ,  0.933511676726150430 ,  1.000000000000000000 ,  0.076391026932554257 ,  0.991314879918573477  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.568120342504817580 ,  0.767982391841911527 ,  1.000000000000000000 ,  0.634831004930734744 ,  0.960083763544511415  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.781989004978930202 ,  1.000000000000000000 ,  1.226098329611510220 ,  0.752297922337263336 ,  1.000000000000000000 ,  0.445743623524378907 ,  0.964480618578559490  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.386364042230872280 ,  0.565443686636630249 ,  1.000000000000000000 ,  0.933153426589199531 ,  0.782457738230375677 ,  1.000000000000000000 ,  0.315379262437352370 ,  0.971578499814664265  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.685003713347018040 ,  1.000000000000000000 ,  1.157585597346559640 ,  0.591475656742034506 ,  1.000000000000000000 ,  0.699054846292816112 ,  0.824818484162769816 ,  1.000000000000000000 ,  0.225195654743707313 ,  0.979165037266123917  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.256064799055552910 ,  0.457985494268582372 ,  1.000000000000000000 ,  0.926916553627981310 ,  0.651601410998540076 ,  1.000000000000000000 ,  0.516741290406593334 ,  0.864424168374970825 ,  1.000000000000000000 ,  0.160991802583988847 ,  0.983554075458977106  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.631917405654019770 ,  1.000000000000000000 ,  1.086873721203165390 ,  0.506488803114154229 ,  1.000000000000000000 ,  0.719038294574136483 ,  0.721854300973572505 ,  1.000000000000000000 ,  0.377889577150787581 ,  0.898802935611646481 ,  1.000000000000000000 ,  0.114948647395425374 ,  0.987695037720106694  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.186704465089923670 ,  0.407520050290374358 ,  1.000000000000000000 ,  0.894364229691424262 ,  0.583466704243515655 ,  1.000000000000000000 ,  0.544984324023730315 ,  0.786754314256297604 ,  1.000000000000000000 ,  0.274985372759130819 ,  0.926568058055459276 ,  1.000000000000000000 ,  0.082320177190224886 ,  0.992087799887664823  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.568120342504817580 ,  0.767982391841911527 ,  1.000000000000000000 ,  0.634831004930734744 ,  0.960083763544511415  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.775689068188907815 ,  1.000000000000000000 ,  1.224722019737058960 ,  0.744771592909019509 ,  1.000000000000000000 ,  0.450030431435774536 ,  0.963771449247337597  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.369020623350317930 ,  0.549620790631470624 ,  1.000000000000000000 ,  0.937270898906264249 ,  0.771464538832320357 ,  1.000000000000000000 ,  0.321713968465377975 ,  0.970138141518834374  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.671814320592255987 ,  1.000000000000000000 ,  1.147368369102463200 ,  0.573865551924717221 ,  1.000000000000000000 ,  0.707853087909313783 ,  0.813931510915012035 ,  1.000000000000000000 ,  0.231531068241684540 ,  0.977463871482887936  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.229580688806678920 ,  0.437281565589668630 ,  1.000000000000000000 ,  0.925712366537628895 ,  0.632016570946966727 ,  1.000000000000000000 ,  0.528795764729572904 ,  0.853691445344036182 ,  1.000000000000000000 ,  0.167274482033352573 ,  0.981604868738974345  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.612849519567511991 ,  1.000000000000000000 ,  1.067648227044968310 ,  0.481210152035661032 ,  1.000000000000000000 ,  0.726515432883775625 ,  0.700408444328936120 ,  1.000000000000000000 ,  0.392273413578417995 ,  0.888223492413965920 ,  1.000000000000000000 ,  0.121215739790853608 ,  0.985576071878620508  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.151120072940404530 ,  0.381894811592676298 ,  1.000000000000000000 ,  0.887322732232945000 ,  0.556249035089023569 ,  1.000000000000000000 ,  0.557284091782770496 ,  0.765976645416815427 ,  1.000000000000000000 ,  0.288455639264962482 ,  0.916208052058657518 ,  1.000000000000000000 ,  0.087600244377935788 ,  0.988598589683414963  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.568120342504817580 ,  0.767982391841911527 ,  1.000000000000000000 ,  0.634831004930734744 ,  0.960083763544511415  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.770847217689077269 ,  1.000000000000000000 ,  1.222719434173585640 ,  0.738498679695584359 ,  1.000000000000000000 ,  0.452531002128616633 ,  0.961659889479204444  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.358231122416941660 ,  0.539959093268301826 ,  1.000000000000000000 ,  0.939441887082774318 ,  0.764427979198986085 ,  1.000000000000000000 ,  0.325528199304206345 ,  0.968806156890425707  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.659936018391097279 ,  1.000000000000000000 ,  1.137542197324859390 ,  0.557949179774389958 ,  1.000000000000000000 ,  0.715255735515955671 ,  0.803411634317191536 ,  1.000000000000000000 ,  0.237202052502879263 ,  0.975090994548458712  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.205666402087884890 ,  0.419113778823206107 ,  1.000000000000000000 ,  0.923750029586327459 ,  0.614163014650401684 ,  1.000000000000000000 ,  0.539301066005906771 ,  0.843184895032933257 ,  1.000000000000000000 ,  0.172972482802768751 ,  0.979014681248474239  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.599078659522306212 ,  1.000000000000000000 ,  1.053476117073946170 ,  0.463220527530034509 ,  1.000000000000000000 ,  0.732120513954288787 ,  0.684933899416085734 ,  1.000000000000000000 ,  0.403523073645395303 ,  0.881020838559565811 ,  1.000000000000000000 ,  0.126211429983645651 ,  0.985141055756889927  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.121750615483520400 ,  0.361414852504894246 ,  1.000000000000000000 ,  0.881497422349589721 ,  0.534184820429662866 ,  1.000000000000000000 ,  0.568469639429357132 ,  0.749377223904587830 ,  1.000000000000000000 ,  0.301004489502375072 ,  0.909178429120786946 ,  1.000000000000000000 ,  0.092592519147911931 ,  0.988135273841895145  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.568120342504817580 ,  0.767982391841911527 ,  1.000000000000000000 ,  0.634831004930734744 ,  0.960083763544511415  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.766028914123395910 ,  1.000000000000000000 ,  1.220705269248459900 ,  0.732263236057083766 ,  1.000000000000000000 ,  0.455065767488325501 ,  0.959588334288802969  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.347518053196631320 ,  0.530473641872668322 ,  1.000000000000000000 ,  0.941553212069938472 ,  0.757428935136336134 ,  1.000000000000000000 ,  0.329384643446102410 ,  0.967518814904568592  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.651322596741974680 ,  1.000000000000000000 ,  1.130498797691805280 ,  0.546561339354849074 ,  1.000000000000000000 ,  0.721179674892682643 ,  0.796147139568926798 ,  1.000000000000000000 ,  0.241728771389749297 ,  0.974278182494885181  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.188198833463878670 ,  0.406098880276641705 ,  1.000000000000000000 ,  0.922471039556082450 ,  0.601305177141589042 ,  1.000000000000000000 ,  0.547638479797267541 ,  0.835984101194135243 ,  1.000000000000000000 ,  0.177544814599158052 ,  0.978155814722118877  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.585012951960531713 ,  1.000000000000000000 ,  1.038637537365519890 ,  0.445046703672678734 ,  1.000000000000000000 ,  0.737778570187024751 ,  0.668964713688722457 ,  1.000000000000000000 ,  0.415604835273416517 ,  0.873705355752242618 ,  1.000000000000000000 ,  0.131684936073836634 ,  0.985256968347506978  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.095677998651553240 ,  0.343797317241810951 ,  1.000000000000000000 ,  0.875468575869442711 ,  0.514610933858554054 ,  1.000000000000000000 ,  0.577928158530295222 ,  0.733948259368565337 ,  1.000000000000000000 ,  0.312309147495030581 ,  0.902149300976180180 ,  1.000000000000000000 ,  0.097189036206850862 ,  0.987164778864964898  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.568120342504817580 ,  0.767982391841911527 ,  1.000000000000000000 ,  0.634831004930734744 ,  0.960083763544511415  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.763180458125725436 ,  1.000000000000000000 ,  1.219872216959116520 ,  0.728773371704210082 ,  1.000000000000000000 ,  0.456941983821854192 ,  0.959031936819493280  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.337590050092601900 ,  0.521816101821789058 ,  1.000000000000000000 ,  0.943098644397866504 ,  0.750738307734103771 ,  1.000000000000000000 ,  0.332766099135981486 ,  0.965808046025208089  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.643936737448320473 ,  1.000000000000000000 ,  1.124011314003179770 ,  0.536702708496752834 ,  1.000000000000000000 ,  0.725644762055276837 ,  0.789281405094316102 ,  1.000000000000000000 ,  0.245399133803155811 ,  0.972601376687856400  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.174099543426090580 ,  0.395685922252264510 ,  1.000000000000000000 ,  0.922390884838075520 ,  0.591372329530246876 ,  1.000000000000000000 ,  0.555993004231788324 ,  0.831702625103592053 ,  1.000000000000000000 ,  0.182045731279404460 ,  0.980159031532137681  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.574015630116334608 ,  1.000000000000000000 ,  1.026393998431143210 ,  0.430859490705604031 ,  1.000000000000000000 ,  0.741221328234191512 ,  0.655741782314467336 ,  1.000000000000000000 ,  0.424587270730054034 ,  0.866644862017087569 ,  1.000000000000000000 ,  0.135887463448203488 ,  0.983866276077398516  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.072336712369548420 ,  0.328467195983117966 ,  1.000000000000000000 ,  0.869370014315813511 ,  0.497114618594278856 ,  1.000000000000000000 ,  0.585948814588642675 ,  0.719564508826211258 ,  1.000000000000000000 ,  0.322512386216733726 ,  0.895146745619103790 ,  1.000000000000000000 ,  0.101424107914020650 ,  0.985744889353863285  } } } ;

 const double Elliptic_02Denominator[13][7][24] = {
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.395312609278576140 ,  0.708854734701544142 ,  1.000000000000000000 ,  0.447639184732821882 ,  0.958376905979590132  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.761997739734376234 ,  1.000000000000000000 ,  0.954008299942295746 ,  0.778676562616257395 ,  1.000000000000000000 ,  0.256328209783466043 ,  0.975520267069527325  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.291054339440149020 ,  0.585272478114654104 ,  1.000000000000000000 ,  0.602064960557073814 ,  0.845572055043871007 ,  1.000000000000000000 ,  0.151003342141841995 ,  0.983734408299898999  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.710314624298944874 ,  1.000000000000000000 ,  0.932664118932095665 ,  0.691635433828893254 ,  1.000000000000000000 ,  0.360622084494575945 ,  0.905816522826579429 ,  1.000000000000000000 ,  0.086275031069467129 ,  0.989786970027435764  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.278516265300555070 ,  0.577622561756343633 ,  1.000000000000000000 ,  0.591157777866426870 ,  0.815701645634526407 ,  1.000000000000000000 ,  0.200307921611016460 ,  0.948399868172212668 ,  1.000000000000000000 ,  0.045853507776964939 ,  0.991918013403954246  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.364543929907204190 ,  0.665934638926704014 ,  1.000000000000000000 ,  0.466192365667595920 ,  0.948298436981499515  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.723824550521118604 ,  1.000000000000000000 ,  0.958812281517037790 ,  0.735381635935112299 ,  1.000000000000000000 ,  0.275712140585602394 ,  0.967491553282725292  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.239889830782336720 ,  0.529496217708815942 ,  1.000000000000000000 ,  0.626890845739481506 ,  0.812222454448309317 ,  1.000000000000000000 ,  0.166761525266333921 ,  0.979016725276668276  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.663725280028633957 ,  1.000000000000000000 ,  0.925189517374781834 ,  0.632562601172968475 ,  1.000000000000000000 ,  0.392248739041702266 ,  0.878963318341713262 ,  1.000000000000000000 ,  0.099332849175092303 ,  0.986321485941227927  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.199745134078761040 ,  0.495489130156138768 ,  1.000000000000000000 ,  0.620113756076869360 ,  0.757717104303798905 ,  1.000000000000000000 ,  0.232752906575811741 ,  0.928075602566196811 ,  1.000000000000000000 ,  0.056661586539072967 ,  0.990291120604660180  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.343297561286685090 ,  0.637038923783784350 ,  1.000000000000000000 ,  0.481389801390741723 ,  0.943880912625384982  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.694663617018995594 ,  1.000000000000000000 ,  0.962387279770458570 ,  0.702280899806166481 ,  1.000000000000000000 ,  0.293087777267661331 ,  0.964000679323855247  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.202458162414797640 ,  0.491566547026934808 ,  1.000000000000000000 ,  0.643658217301500635 ,  0.786822523071664448 ,  1.000000000000000000 ,  0.178837944190323256 ,  0.974827877087586003  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.626227211836801856 ,  1.000000000000000000 ,  0.914566452631001781 ,  0.583714177159000980 ,  1.000000000000000000 ,  0.418762012586364862 ,  0.853386253116400262 ,  1.000000000000000000 ,  0.111353606154923707 ,  0.981952489183791744  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.150219874174551290 ,  0.448709307581811989 ,  1.000000000000000000 ,  0.635660784625875386 ,  0.719924558721700802 ,  1.000000000000000000 ,  0.254734941928345504 ,  0.913726319816042909 ,  1.000000000000000000 ,  0.064502330634977470 ,  0.989639781760233816  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.324856249065863970 ,  0.613810213907749813 ,  1.000000000000000000 ,  0.492256922978714540 ,  0.937345124470334579  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.671967666501227412 ,  1.000000000000000000 ,  0.962229338901160247 ,  0.674889049445753342 ,  1.000000000000000000 ,  0.306352968493615430 ,  0.957948838239512712  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.166556514647986150 ,  0.457194651591402601 ,  1.000000000000000000 ,  0.658939618523755377 ,  0.761921558835424340 ,  1.000000000000000000 ,  0.191066204196644468 ,  0.970741108824958898  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.596796185305228599 ,  1.000000000000000000 ,  0.904171517860709595 ,  0.545279554445608272 ,  1.000000000000000000 ,  0.441389508310492451 ,  0.832427132942173520 ,  1.000000000000000000 ,  0.122374347919184959 ,  0.980184352374659840  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.093264425269795390 ,  0.399146431223935338 ,  1.000000000000000000 ,  0.649739983702618984 ,  0.674776204042759176 ,  1.000000000000000000 ,  0.280647573349720514 ,  0.893864006550518120 ,  1.000000000000000000 ,  0.074355676615984995 ,  0.986966886605081251  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.312476853355223790 ,  0.598215400481807102 ,  1.000000000000000000 ,  0.501076527418756523 ,  0.934569165346574637  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.653566158165713840 ,  1.000000000000000000 ,  0.961819980308707190 ,  0.652653739766697716 ,  1.000000000000000000 ,  0.318150009519115540 ,  0.953859424089352426  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.137149463479864450 ,  0.430268617056732416 ,  1.000000000000000000 ,  0.671865920309000386 ,  0.741758828800637371 ,  1.000000000000000000 ,  0.202159302637550525 ,  0.968993512845366944  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.572262748399810839 ,  1.000000000000000000 ,  0.893200772468009774 ,  0.512903660422013741 ,  1.000000000000000000 ,  0.460373752144594528 ,  0.812775081164354596 ,  1.000000000000000000 ,  0.132357641297341760 ,  0.977577003079061146  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.042234882764993210 ,  0.358120913810809460 ,  1.000000000000000000 ,  0.659523559381606228 ,  0.633435970249112579 ,  1.000000000000000000 ,  0.304982811934011500 ,  0.874010142901775811 ,  1.000000000000000000 ,  0.084236187784388211 ,  0.984252521820301762  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.301159738409781720 ,  0.584566013162456710 ,  1.000000000000000000 ,  0.508268102600222482 ,  0.931020791136183190  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.639886857267507048 ,  1.000000000000000000 ,  0.961199930036109818 ,  0.636039418595074735 ,  1.000000000000000000 ,  0.327488474550492403 ,  0.951090685832012950  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.107313147564552350 ,  0.404420451607182263 ,  1.000000000000000000 ,  0.682281965560111892 ,  0.719702380287936649 ,  1.000000000000000000 ,  0.212759825451025064 ,  0.963788345678918690  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.553189409706760893 ,  1.000000000000000000 ,  0.884026221635521803 ,  0.487963905388002495 ,  1.000000000000000000 ,  0.476418891827478763 ,  0.797654877121962480 ,  1.000000000000000000 ,  0.141212849627568443 ,  0.977443793222819268  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.003592448320268500 ,  0.328941585852844332 ,  1.000000000000000000 ,  0.665886556966612031 ,  0.602083139556950142 ,  1.000000000000000000 ,  0.324952434977647198 ,  0.858923154956138024 ,  1.000000000000000000 ,  0.092785189679582575 ,  0.983954500917513686  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.293860081861715420 ,  0.575972165242416900 ,  1.000000000000000000 ,  0.512724362758448571 ,  0.928507276597621556  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.628902119690819883 ,  1.000000000000000000 ,  0.959406787636892799 ,  0.622080615531158343 ,  1.000000000000000000 ,  0.334422130460320655 ,  0.946927617868986604  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.086714867254327200 ,  0.386989729306478847 ,  1.000000000000000000 ,  0.691171013846200943 ,  0.705482427802368139 ,  1.000000000000000000 ,  0.221606698807570612 ,  0.963563730525880513  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.536361633904987589 ,  1.000000000000000000 ,  0.873668659097736389 ,  0.465432263961414849 ,  1.000000000000000000 ,  0.488675592738262776 ,  0.781030455533240020 ,  1.000000000000000000 ,  0.148656376968373499 ,  0.972932618856760301  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.972766788352735001 ,  0.306866475907918601 ,  1.000000000000000000 ,  0.669296859949782941 ,  0.576626810597542283 ,  1.000000000000000000 ,  0.340852268052864893 ,  0.845256409264396713 ,  1.000000000000000000 ,  0.099956131366795556 ,  0.982533981956344227  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.287611598558543240 ,  0.568990135284856646 ,  1.000000000000000000 ,  0.515498820981016737 ,  0.925218117018142272  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.620128643358952791 ,  1.000000000000000000 ,  0.958618878646359751 ,  0.611321179981360818 ,  1.000000000000000000 ,  0.340869762818609978 ,  0.945224287438486854  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.068272743794661310 ,  0.372143752069260536 ,  1.000000000000000000 ,  0.696076634885901102 ,  0.691038972574241961 ,  1.000000000000000000 ,  0.228070802170111275 ,  0.958707929682475868  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.522682428573470914 ,  1.000000000000000000 ,  0.865242951031136465 ,  0.447434394064297580 ,  1.000000000000000000 ,  0.499785542571442476 ,  0.768217949818359003 ,  1.000000000000000000 ,  0.155554805257240136 ,  0.971337405357249772  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.946309178813765306 ,  0.288722035502911811 ,  1.000000000000000000 ,  0.671079155677824502 ,  0.554547377189300916 ,  1.000000000000000000 ,  0.354467829612319041 ,  0.832422306025306980 ,  1.000000000000000000 ,  0.106377973361302530 ,  0.980520328236718863  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.283557321275842880 ,  0.564194974719725129 ,  1.000000000000000000 ,  0.518439835651462899 ,  0.924307070281178644  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.614797190894214696 ,  1.000000000000000000 ,  0.958480733058198808 ,  0.604976208446825314 ,  1.000000000000000000 ,  0.345271570646831227 ,  0.945046109949916446  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.055172267892965140 ,  0.361548855426719995 ,  1.000000000000000000 ,  0.702175580857809067 ,  0.682254622990592030 ,  1.000000000000000000 ,  0.234536742245112645 ,  0.959911578856562753  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.510746961986765835 ,  1.000000000000000000 ,  0.857212448923497017 ,  0.431708024169507487 ,  1.000000000000000000 ,  0.509208741096094930 ,  0.756262007412904169 ,  1.000000000000000000 ,  0.161729722494390715 ,  0.969220778367985925  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.923280338442226456 ,  0.273512057865708202 ,  1.000000000000000000 ,  0.671719459088439219 ,  0.535177055795935885 ,  1.000000000000000000 ,  0.366203775365729844 ,  0.820336402094317907 ,  1.000000000000000000 ,  0.112146370974695653 ,  0.978009623226209768  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.283557321275842880 ,  0.564194974719725129 ,  1.000000000000000000 ,  0.518439835651462899 ,  0.924307070281178644  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.608400878171293735 ,  1.000000000000000000 ,  0.956796338373933408 ,  0.596617333171033715 ,  1.000000000000000000 ,  0.349332394546246428 ,  0.941980591289405478  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.042948151848002600 ,  0.352098953868294262 ,  1.000000000000000000 ,  0.705289500949429882 ,  0.672649749364840899 ,  1.000000000000000000 ,  0.239115456706553292 ,  0.956869221277898641  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.501142692317239846 ,  1.000000000000000000 ,  0.851108652694568923 ,  0.419357116625408199 ,  1.000000000000000000 ,  0.518109786738675027 ,  0.747735995480023230 ,  1.000000000000000000 ,  0.167539834545725935 ,  0.970080333755665602  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.905114563904505731 ,  0.261805267373589390 ,  1.000000000000000000 ,  0.672713395766268651 ,  0.520275905099096936 ,  1.000000000000000000 ,  0.376743103829630666 ,  0.812035001876165174 ,  1.000000000000000000 ,  0.117398547730116112 ,  0.978530401484955514  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.283557321275842880 ,  0.564194974719725129 ,  1.000000000000000000 ,  0.518439835651462899 ,  0.924307070281178644  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.604803734505838020 ,  1.000000000000000000 ,  0.956056436286515043 ,  0.592035739542442374 ,  1.000000000000000000 ,  0.351883237378915636 ,  0.940750274411696230  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.033681401630974110 ,  0.344926798357308884 ,  1.000000000000000000 ,  0.708812645176623346 ,  0.666012633191991887 ,  1.000000000000000000 ,  0.243469296703762578 ,  0.956681388070329430  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.492212735055728545 ,  1.000000000000000000 ,  0.844110093435760711 ,  0.407523103503491413 ,  1.000000000000000000 ,  0.524434247764173689 ,  0.737488577671280066 ,  1.000000000000000000 ,  0.172176413534916167 ,  0.966878543506104693  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.887426532510684263 ,  0.250806971314808780 ,  1.000000000000000000 ,  0.671906853613062793 ,  0.505182233897053856 ,  1.000000000000000000 ,  0.385330465051617721 ,  0.801311712239664020 ,  1.000000000000000000 ,  0.121950953644969495 ,  0.975113833175333844  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.283557321275842880 ,  0.564194974719725129 ,  1.000000000000000000 ,  0.518439835651462899 ,  0.924307070281178644  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.601275302460628103 ,  1.000000000000000000 ,  0.955280834254339073 ,  0.587526729983011253 ,  1.000000000000000000 ,  0.354394239215183982 ,  0.939500619079442489  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.023107316280515100 ,  0.337043016586830946 ,  1.000000000000000000 ,  0.710821146497542356 ,  0.657376032354646811 ,  1.000000000000000000 ,  0.247219571901016483 ,  0.953134506810169202  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.485571619768276730 ,  1.000000000000000000 ,  0.839260221149702224 ,  0.398945271239171895 ,  1.000000000000000000 ,  0.530144662367904251 ,  0.730772333533291185 ,  1.000000000000000000 ,  0.176236821608429545 ,  0.966494918704587924  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.873155211989415148 ,  0.242079959486885682 ,  1.000000000000000000 ,  0.671898774694090983 ,  0.493387488452043998 ,  1.000000000000000000 ,  0.393520986289711383 ,  0.794010728530453513 ,  1.000000000000000000 ,  0.126283322812796667 ,  0.974930979597433134  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.283557321275842880 ,  0.564194974719725129 ,  1.000000000000000000 ,  0.518439835651462899 ,  0.924307070281178644  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.598092565149025090 ,  1.000000000000000000 ,  0.954394295990106278 ,  0.583376098896593698 ,  1.000000000000000000 ,  0.356527039346153152 ,  0.938042160389740420  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.016887412121360020 ,  0.332421207990028833 ,  1.000000000000000000 ,  0.712392859281891222 ,  0.652515912228829431 ,  1.000000000000000000 ,  0.249747367024340028 ,  0.951786203431395861  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.479974483030645105 ,  1.000000000000000000 ,  0.834924424041445623 ,  0.391688567389636200 ,  1.000000000000000000 ,  0.534697851447028416 ,  0.724745294394366524 ,  1.000000000000000000 ,  0.179613097517012238 ,  0.965634004777403909  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.861009096151453934 ,  0.234811490207336271 ,  1.000000000000000000 ,  0.671522878691387048 ,  0.483285393627894744 ,  1.000000000000000000 ,  0.400318043090102493 ,  0.787357804527666128 ,  1.000000000000000000 ,  0.129984018063608653 ,  0.974276448369379211  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_04Denominator[13][7][24] = {
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.296481568568618850 ,  0.639703329591692182 ,  1.000000000000000000 ,  0.411773161594760295 ,  0.948604730964612952  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.704337939401190827 ,  1.000000000000000000 ,  0.871640300007750057 ,  0.735171289138899064 ,  1.000000000000000000 ,  0.229725604629959418 ,  0.970145020662521840  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.183828587366956950 ,  0.515701652814805955 ,  1.000000000000000000 ,  0.540690258847635330 ,  0.821116521552568646 ,  1.000000000000000000 ,  0.132621975255081709 ,  0.980457994208407735  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.652280188369736891 ,  1.000000000000000000 ,  0.846626407398611836 ,  0.645752982256151187 ,  1.000000000000000000 ,  0.318426598101804958 ,  0.894660426277523846 ,  1.000000000000000000 ,  0.074324855572303047 ,  0.990062812058431274  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.160413984091042570 ,  0.496978733754984692 ,  1.000000000000000000 ,  0.532337772572724521 ,  0.782802347015738365 ,  1.000000000000000000 ,  0.176561619788853374 ,  0.941852817153108091 ,  1.000000000000000000 ,  0.039628722922913688 ,  0.993208571211523283  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.267591476436900510 ,  0.599838537716394393 ,  1.000000000000000000 ,  0.430613929202911472 ,  0.938614039688099200  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.670165837965009392 ,  1.000000000000000000 ,  0.878779224648886803 ,  0.696571829177701218 ,  1.000000000000000000 ,  0.248586666746584944 ,  0.965297683221558289  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.134947096883145210 ,  0.464002480805492668 ,  1.000000000000000000 ,  0.566677806309902943 ,  0.787673294141226443 ,  1.000000000000000000 ,  0.148551482794973100 ,  0.977067485057747209  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.608533783532571704 ,  1.000000000000000000 ,  0.839733940879485496 ,  0.588751042037275907 ,  1.000000000000000000 ,  0.347685448274287212 ,  0.865647733765592120 ,  1.000000000000000000 ,  0.086249817971631976 ,  0.983969974612424636  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.097266882810458370 ,  0.434000198390883440 ,  1.000000000000000000 ,  0.556675967155025764 ,  0.733168125137962257 ,  1.000000000000000000 ,  0.203071168997990881 ,  0.924085879640265007 ,  1.000000000000000000 ,  0.048334964325202172 ,  0.992173097409868809  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.246247165693445510 ,  0.571774424264946735 ,  1.000000000000000000 ,  0.445451535711092084 ,  0.932153330343045261  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.639403331755638349 ,  1.000000000000000000 ,  0.881063229507790369 ,  0.659288361340053086 ,  1.000000000000000000 ,  0.265790619082928115 ,  0.956466256631442358  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.093549735544419920 ,  0.423810920215314302 ,  1.000000000000000000 ,  0.586434515714828852 ,  0.757699023298128438 ,  1.000000000000000000 ,  0.162570932613147340 ,  0.972397141769138384  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.573903308540018697 ,  1.000000000000000000 ,  0.831312603409813167 ,  0.543153129364125853 ,  1.000000000000000000 ,  0.373361650488880148 ,  0.841150546326488779 ,  1.000000000000000000 ,  0.097635910466529002 ,  0.981021694781572529  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.033290834397349480 ,  0.376936942324034185 ,  1.000000000000000000 ,  0.575973290884662537 ,  0.680106831204066564 ,  1.000000000000000000 ,  0.231028337345236146 ,  0.901067715910579436 ,  1.000000000000000000 ,  0.058293970520730994 ,  0.988228576259677638  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.232416157430380910 ,  0.554007510686163496 ,  1.000000000000000000 ,  0.456088601015980477 ,  0.929028444918019614  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.620236380351028904 ,  1.000000000000000000 ,  0.883366792352130004 ,  0.636668750362253122 ,  1.000000000000000000 ,  0.278733260759294277 ,  0.954384699341564668  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.058469462978701920 ,  0.392065699276542556 ,  1.000000000000000000 ,  0.601213512764244062 ,  0.731014162647044774 ,  1.000000000000000000 ,  0.174668478014527756 ,  0.966555288383256106  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.547119062072396689 ,  1.000000000000000000 ,  0.823030503922576706 ,  0.507833347838056559 ,  1.000000000000000000 ,  0.395173764360530921 ,  0.821566930819649821 ,  1.000000000000000000 ,  0.108025222585099404 ,  0.980845374466810527  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.987172144072995361 ,  0.339262882541985500 ,  1.000000000000000000 ,  0.587896129942171886 ,  0.641242700395501242 ,  1.000000000000000000 ,  0.253027074007881603 ,  0.883606607485893925 ,  1.000000000000000000 ,  0.066681512771515503 ,  0.986771708910645695  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.217969316908034560 ,  0.536968965183692415 ,  1.000000000000000000 ,  0.464209452916250709 ,  0.922334242487084066  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.602924332534602869 ,  1.000000000000000000 ,  0.882551078727854543 ,  0.614701999043249958 ,  1.000000000000000000 ,  0.289427909573323705 ,  0.948149771719063228  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.030547817654517310 ,  0.367990861920803269 ,  1.000000000000000000 ,  0.613084390538430002 ,  0.709869094227905895 ,  1.000000000000000000 ,  0.185213981422741120 ,  0.963062907543272062  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523598150415167662 ,  1.000000000000000000 ,  0.812554200292258888 ,  0.476059602963595452 ,  1.000000000000000000 ,  0.413206729956900443 ,  0.800238634975896756 ,  1.000000000000000000 ,  0.117446316606291137 ,  0.976500706648067762  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.948865850124403165 ,  0.310076321264860011 ,  1.000000000000000000 ,  0.595679186085109724 ,  0.608261437135676708 ,  1.000000000000000000 ,  0.271851507420453553 ,  0.867200833624070078 ,  1.000000000000000000 ,  0.074298247439710224 ,  0.984731827072741317  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.209033434830368630 ,  0.526161491586363717 ,  1.000000000000000000 ,  0.471029541749550751 ,  0.920153645751966787  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.589976500041524798 ,  1.000000000000000000 ,  0.883495903352491174 ,  0.599206501416329806 ,  1.000000000000000000 ,  0.299475414323477962 ,  0.947456037369769466  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.011102831121621910 ,  0.351832359130833050 ,  1.000000000000000000 ,  0.621357002501126576 ,  0.695158626450975925 ,  1.000000000000000000 ,  0.193071082278759770 ,  0.961209305372836220  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.505078168520064930 ,  1.000000000000000000 ,  0.803651636834056271 ,  0.451308046238617422 ,  1.000000000000000000 ,  0.428664297725257382 ,  0.783585355278855533 ,  1.000000000000000000 ,  0.125940692482274574 ,  0.975004796255477757  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.915425112940545072 ,  0.286043254751217724 ,  1.000000000000000000 ,  0.600894421462101369 ,  0.579077626259055123 ,  1.000000000000000000 ,  0.288683995815176897 ,  0.851476093520070032 ,  1.000000000000000000 ,  0.081478532692730379 ,  0.982369919248319179  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.203855947027092420 ,  0.519839402425310904 ,  1.000000000000000000 ,  0.475577139764712331 ,  0.919526857728516056  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.579705744136105983 ,  1.000000000000000000 ,  0.881506202373149916 ,  0.585520824021036668 ,  1.000000000000000000 ,  0.305669812853883616 ,  0.941899302827074236  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.993744880628603555 ,  0.337887081116976795 ,  1.000000000000000000 ,  0.628109235747479455 ,  0.681666583782359670 ,  1.000000000000000000 ,  0.200093301540284652 ,  0.958866343246789876  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.489510482803402813 ,  1.000000000000000000 ,  0.795038084285508484 ,  0.430437547868151471 ,  1.000000000000000000 ,  0.441524218568173255 ,  0.768345006604101877 ,  1.000000000000000000 ,  0.133469702711901506 ,  0.972879894316415750  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.886587786463499783 ,  0.266349666041751754 ,  1.000000000000000000 ,  0.604083363005749274 ,  0.553642702810581588 ,  1.000000000000000000 ,  0.303355495444612322 ,  0.836660811651465441 ,  1.000000000000000000 ,  0.088051618191429926 ,  0.979593947224969397  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.197325056696900920 ,  0.512673782588727978 ,  1.000000000000000000 ,  0.478710471875218313 ,  0.915848385290644496  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.572433009689313654 ,  1.000000000000000000 ,  0.881998106567785212 ,  0.576862266802721457 ,  1.000000000000000000 ,  0.311992112596839177 ,  0.942186915151053683  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.978184606688831892 ,  0.325758432047438484 ,  1.000000000000000000 ,  0.633589440713728358 ,  0.669263932564812758 ,  1.000000000000000000 ,  0.206347825115349937 ,  0.956090359515196941  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476476950343563688 ,  1.000000000000000000 ,  0.786947561114643035 ,  0.412925376946983114 ,  1.000000000000000000 ,  0.452002136124965259 ,  0.754531332853649594 ,  1.000000000000000000 ,  0.139985682951157764 ,  0.970124603605003522  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.862966897504290453 ,  0.250803455031647415 ,  1.000000000000000000 ,  0.606960377656732608 ,  0.533209999439824278 ,  1.000000000000000000 ,  0.316820518586034494 ,  0.825738384724299723 ,  1.000000000000000000 ,  0.094261149665992264 ,  0.980109169113642098  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.193457275423395640 ,  0.508291755284644764 ,  1.000000000000000000 ,  0.481244364695227966 ,  0.914408950694363343  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.565144262127947106 ,  1.000000000000000000 ,  0.880652058031204943 ,  0.567271481450013049 ,  1.000000000000000000 ,  0.316954710210545221 ,  0.938925797016301411  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.965882006176971175 ,  0.316251557027598218 ,  1.000000000000000000 ,  0.639108783735794828 ,  0.660149177373610452 ,  1.000000000000000000 ,  0.212261789436061166 ,  0.956268282152840876  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.466012863512004660 ,  1.000000000000000000 ,  0.780690737876248764 ,  0.399173300191937030 ,  1.000000000000000000 ,  0.461663139940449774 ,  0.744450227125507680 ,  1.000000000000000000 ,  0.146049895321804624 ,  0.970455817425420864  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.841106380506390750 ,  0.237047836167330578 ,  1.000000000000000000 ,  0.607588674610112234 ,  0.513622127408383533 ,  1.000000000000000000 ,  0.327864834289910700 ,  0.812634448783642660 ,  1.000000000000000000 ,  0.099661888905898458 ,  0.976557490172303089  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.193457275423395640 ,  0.508291755284644764 ,  1.000000000000000000 ,  0.481244364695227966 ,  0.914408950694363343  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.560754162678700374 ,  1.000000000000000000 ,  0.880156754440730937 ,  0.561675192593327433 ,  1.000000000000000000 ,  0.320351544756667106 ,  0.937752777830254236  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.953249021439475985 ,  0.306889715166378085 ,  1.000000000000000000 ,  0.642588116671983678 ,  0.649574580096338838 ,  1.000000000000000000 ,  0.217147453665365892 ,  0.952733250495351402  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.456483866305058383 ,  1.000000000000000000 ,  0.773648722878257922 ,  0.386318284994814076 ,  1.000000000000000000 ,  0.468671088336823582 ,  0.732861630375808204 ,  1.000000000000000000 ,  0.150936349782068741 ,  0.966713687511183917  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.823484106829761697 ,  0.226244577697980065 ,  1.000000000000000000 ,  0.608552790339908611 ,  0.498226152442821701 ,  1.000000000000000000 ,  0.338001694083928994 ,  0.803332669732564009 ,  1.000000000000000000 ,  0.104693935578703073 ,  0.976277665618435653  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.193457275423395640 ,  0.508291755284644764 ,  1.000000000000000000 ,  0.481244364695227966 ,  0.914408950694363343  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.556868221557490939 ,  1.000000000000000000 ,  0.879496198238162052 ,  0.556622751202588972 ,  1.000000000000000000 ,  0.323231707237302990 ,  0.936328188030187358  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.944640699912762560 ,  0.300541661128384041 ,  1.000000000000000000 ,  0.645696759671450016 ,  0.642796000130568257 ,  1.000000000000000000 ,  0.221077126645193744 ,  0.951795698190415429  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.449180741227671432 ,  1.000000000000000000 ,  0.768579920653730486 ,  0.376708563108036409 ,  1.000000000000000000 ,  0.475066452622250779 ,  0.724932316741737637 ,  1.000000000000000000 ,  0.155317105172723902 ,  0.966011706519204982  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.808006672881965593 ,  0.217021238316576687 ,  1.000000000000000000 ,  0.608943543325049408 ,  0.484650270167237718 ,  1.000000000000000000 ,  0.346875947053847111 ,  0.794683081439466643 ,  1.000000000000000000 ,  0.109243182569394193 ,  0.975678237451369390  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.193457275423395640 ,  0.508291755284644764 ,  1.000000000000000000 ,  0.481244364695227966 ,  0.914408950694363343  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.553596070255832906 ,  1.000000000000000000 ,  0.878656450593704119 ,  0.552238286182097138 ,  1.000000000000000000 ,  0.325443983145723925 ,  0.934582277710764298  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.936406610134488448 ,  0.294559727549221273 ,  1.000000000000000000 ,  0.648540202852416536 ,  0.636249919555091581 ,  1.000000000000000000 ,  0.224850623436142011 ,  0.950770467884574422  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.442833296984733393 ,  1.000000000000000000 ,  0.763920273721721665 ,  0.368349321565978383 ,  1.000000000000000000 ,  0.480450169689452700 ,  0.717693265438118066 ,  1.000000000000000000 ,  0.159150005512754184 ,  0.964978891756109847  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.794961104061513435 ,  0.209440202423965272 ,  1.000000000000000000 ,  0.608860808019123900 ,  0.473145568161389196 ,  1.000000000000000000 ,  0.354216375780979698 ,  0.786895105914667381 ,  1.000000000000000000 ,  0.113124705148081595 ,  0.974650930092371359  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.193457275423395640 ,  0.508291755284644764 ,  1.000000000000000000 ,  0.481244364695227966 ,  0.914408950694363343  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.551531418446253197 ,  1.000000000000000000 ,  0.879175351037155495 ,  0.549992882253328874 ,  1.000000000000000000 ,  0.327875456305488588 ,  0.935763377747619418  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.929788005192039857 ,  0.289844203899395469 ,  1.000000000000000000 ,  0.650389171487003370 ,  0.630759812452745594 ,  1.000000000000000000 ,  0.227656955102401137 ,  0.949217321153792093  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.437353657864472389 ,  1.000000000000000000 ,  0.759678533721562910 ,  0.361121086159512161 ,  1.000000000000000000 ,  0.484896918310658953 ,  0.711119641044374751 ,  1.000000000000000000 ,  0.162442817415214297 ,  0.963626159887801981  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.783272815126442645 ,  0.202792717169210657 ,  1.000000000000000000 ,  0.608496363566954024 ,  0.462810382537691301 ,  1.000000000000000000 ,  0.360711674886298461 ,  0.779583568893504553 ,  1.000000000000000000 ,  0.116652501271782130 ,  0.973400574878600877  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_06Denominator[13][7][24] = {
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.232759593908977940 ,  0.597234617931523437 ,  1.000000000000000000 ,  0.389440235939380042 ,  0.942350577111481402  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.668423133895835742 ,  1.000000000000000000 ,  0.821363594711274625 ,  0.710466014936570578 ,  1.000000000000000000 ,  0.213855314755611065 ,  0.967893139872096242  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.118336236531335850 ,  0.475913923380371362 ,  1.000000000000000000 ,  0.504909210500577021 ,  0.808393122620111537 ,  1.000000000000000000 ,  0.122226449589116090 ,  0.980292962950300550  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.616328310482362585 ,  1.000000000000000000 ,  0.794075113552460077 ,  0.619079024096230723 ,  1.000000000000000000 ,  0.293396822744960206 ,  0.887076889880725639 ,  1.000000000000000000 ,  0.067400100773450905 ,  0.988270907853518343  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.103842230945494720 ,  0.467229843015921464 ,  1.000000000000000000 ,  0.490658967513782907 ,  0.775617737162728749 ,  1.000000000000000000 ,  0.156695340036005493 ,  0.941039092951484246 ,  1.000000000000000000 ,  0.034210691182522249 ,  0.992595738012278295  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.206635111709692950 ,  0.561285826371344609 ,  1.000000000000000000 ,  0.407526254796289178 ,  0.933192411027386814  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.635278624109124834 ,  1.000000000000000000 ,  0.828053056485137273 ,  0.671701558435702228 ,  1.000000000000000000 ,  0.231892874770425694 ,  0.961142551374845833  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.071454264149551690 ,  0.427488803896660718 ,  1.000000000000000000 ,  0.529562397967635934 ,  0.774011400915746606 ,  1.000000000000000000 ,  0.137289323619299242 ,  0.975102120657906579  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.576230880080620955 ,  1.000000000000000000 ,  0.789135259425825741 ,  0.566396070699321519 ,  1.000000000000000000 ,  0.321193888840646813 ,  0.860476168240571604 ,  1.000000000000000000 ,  0.078527360105691654 ,  0.984273379709017626  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.027570715137464450 ,  0.392500712800993190 ,  1.000000000000000000 ,  0.521346263809273736 ,  0.713208043283683701 ,  1.000000000000000000 ,  0.188988666421376600 ,  0.918850357633279824 ,  1.000000000000000000 ,  0.044669922311513351 ,  0.991965168293111987  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.187758414292470870 ,  0.536304819043769587 ,  1.000000000000000000 ,  0.421971110572821606 ,  0.928056097133661440  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.607658845023888228 ,  1.000000000000000000 ,  0.832915586091419247 ,  0.638936065083573146 ,  1.000000000000000000 ,  0.249116969041176306 ,  0.957084753145847622  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.031894312435191810 ,  0.390011782262425111 ,  1.000000000000000000 ,  0.548194398238132874 ,  0.743434055949819372 ,  1.000000000000000000 ,  0.150484041714824879 ,  0.968836510663550587  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.543766234840246443 ,  1.000000000000000000 ,  0.782617536970345640 ,  0.523356916459409294 ,  1.000000000000000000 ,  0.346448586701626760 ,  0.837913932961833252 ,  1.000000000000000000 ,  0.089505512800523560 ,  0.983697219508120169  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.974381455122512174 ,  0.346338762864187466 ,  1.000000000000000000 ,  0.538368298787826771 ,  0.667393498221262704 ,  1.000000000000000000 ,  0.212978305026255532 ,  0.899429115046785799 ,  1.000000000000000000 ,  0.053155633383254713 ,  0.989918137956969368  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.171585267679647660 ,  0.516455121214169144 ,  1.000000000000000000 ,  0.432504315949313711 ,  0.921437594349590827  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.586653127453943934 ,  1.000000000000000000 ,  0.834082809673916237 ,  0.612604291884695895 ,  1.000000000000000000 ,  0.262286940737389174 ,  0.951040205299911845  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.999731338168516004 ,  0.361332344495813140 ,  1.000000000000000000 ,  0.563435679965332503 ,  0.718629589600064733 ,  1.000000000000000000 ,  0.162376156228889679 ,  0.965219574452159890  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.516433944664946853 ,  1.000000000000000000 ,  0.773128920102285622 ,  0.486030416453642644 ,  1.000000000000000000 ,  0.367100979014199469 ,  0.813935747573373058 ,  1.000000000000000000 ,  0.099408960226834930 ,  0.978839366858266780  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.931617006594502861 ,  0.312265221090912204 ,  1.000000000000000000 ,  0.549379664138923918 ,  0.629477329572707633 ,  1.000000000000000000 ,  0.233113097792861573 ,  0.881362628067528853 ,  1.000000000000000000 ,  0.060780907338759231 ,  0.987270963914004818  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.159414458826696580 ,  0.501948887743461025 ,  1.000000000000000000 ,  0.440599776353535921 ,  0.916588466069025554  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.571153018250733946 ,  1.000000000000000000 ,  0.834448006296484524 ,  0.593004103012058526 ,  1.000000000000000000 ,  0.272730400693237396 ,  0.946791574523426993  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.974477915815657458 ,  0.339831900228635742 ,  1.000000000000000000 ,  0.575830525749891020 ,  0.699409982787710383 ,  1.000000000000000000 ,  0.172766018907900443 ,  0.964086132087363490  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.494917759387511313 ,  1.000000000000000000 ,  0.764573661562976681 ,  0.456848017827427733 ,  1.000000000000000000 ,  0.384846823526426129 ,  0.794865583578582058 ,  1.000000000000000000 ,  0.108463623141404730 ,  0.977005910292314428  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.893994326159030406 ,  0.284304798502429068 ,  1.000000000000000000 ,  0.557155069085528920 ,  0.595553830565371256 ,  1.000000000000000000 ,  0.251521647681721583 ,  0.863798034754682198 ,  1.000000000000000000 ,  0.068178154299774973 ,  0.984453352909976376  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.150743248996772830 ,  0.491703884803648350 ,  1.000000000000000000 ,  0.446913591451626679 ,  0.913710662012639618  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.557857779713923696 ,  1.000000000000000000 ,  0.833715827509589835 ,  0.575721383470633952 ,  1.000000000000000000 ,  0.281653394242850352 ,  0.941862537600647531  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.951025078911612698 ,  0.320996765684699870 ,  1.000000000000000000 ,  0.584566026858533427 ,  0.679857110762457917 ,  1.000000000000000000 ,  0.181535952968510772 ,  0.958794185555410849  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476882929453123161 ,  1.000000000000000000 ,  0.755946867579032067 ,  0.432281685940946625 ,  1.000000000000000000 ,  0.399803596949791729 ,  0.777335113527988897 ,  1.000000000000000000 ,  0.116641116474792014 ,  0.974641384975938885  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.862245839704979367 ,  0.262092841740859339 ,  1.000000000000000000 ,  0.562106557402043183 ,  0.566525353296561285 ,  1.000000000000000000 ,  0.267359030564998434 ,  0.847396384595477392 ,  1.000000000000000000 ,  0.074901051064182045 ,  0.981217345199586810  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.145174520781353070 ,  0.484961455790843321 ,  1.000000000000000000 ,  0.451974364054242905 ,  0.912967622494002873  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.549917654905857867 ,  1.000000000000000000 ,  0.834498523696296179 ,  0.566094977197408089 ,  1.000000000000000000 ,  0.288406772483560669 ,  0.941934356348952950  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.933098074760472906 ,  0.307021642879611867 ,  1.000000000000000000 ,  0.591701967856310462 ,  0.665233903512998936 ,  1.000000000000000000 ,  0.188947075071384057 ,  0.956087850224319191  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.461495537469192363 ,  1.000000000000000000 ,  0.747487396542436233 ,  0.411291704915696166 ,  1.000000000000000000 ,  0.412500737541233120 ,  0.761160538401171483 ,  1.000000000000000000 ,  0.124040311550480190 ,  0.971829364231426740  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.835805068003327545 ,  0.244400012249874093 ,  1.000000000000000000 ,  0.566324072905072917 ,  0.542741953865617632 ,  1.000000000000000000 ,  0.282082910585096824 ,  0.834888201179424749 ,  1.000000000000000000 ,  0.081382912571974511 ,  0.981431080459603256  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.138852617028587310 ,  0.478200342210710339 ,  1.000000000000000000 ,  0.454793868019325476 ,  0.908833410119849194  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.541304415276475104 ,  1.000000000000000000 ,  0.833650715719750024 ,  0.554801347275418411 ,  1.000000000000000000 ,  0.294635641646631363 ,  0.938782683075134350  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.919447708543349451 ,  0.296554523513975432 ,  1.000000000000000000 ,  0.598104838397285121 ,  0.654685006817979809 ,  1.000000000000000000 ,  0.195484300026650626 ,  0.956163058262566645  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.448953932539501277 ,  1.000000000000000000 ,  0.739694339081833352 ,  0.394143671201577561 ,  1.000000000000000000 ,  0.422508679108275831 ,  0.746824618727472078 ,  1.000000000000000000 ,  0.130253234233118187 ,  0.968368643597450784  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.811386601242982230 ,  0.228873648388793538 ,  1.000000000000000000 ,  0.568041627060799414 ,  0.520041630308997305 ,  1.000000000000000000 ,  0.294493581548801342 ,  0.820149472377659450 ,  1.000000000000000000 ,  0.087178320228239487 ,  0.977634931035190835  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.136478803018754260 ,  0.475011958916746746 ,  1.000000000000000000 ,  0.458424249364113467 ,  0.910140519196405817  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.535929511608652986 ,  1.000000000000000000 ,  0.833469119051693297 ,  0.547959565917493729 ,  1.000000000000000000 ,  0.299023801158854352 ,  0.937775850472982064  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.906262789216129794 ,  0.286887602256546914 ,  1.000000000000000000 ,  0.602049340288966839 ,  0.643214313313096198 ,  1.000000000000000000 ,  0.200642024632385590 ,  0.952319038357807401  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.438896534741249234 ,  1.000000000000000000 ,  0.733660455962231395 ,  0.380693855772353928 ,  1.000000000000000000 ,  0.431706222996709743 ,  0.736309966282199135 ,  1.000000000000000000 ,  0.136020850979164359 ,  0.968087258799768535  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.791832662559874390 ,  0.216836644945088058 ,  1.000000000000000000 ,  0.569723085603873525 ,  0.502252847002637615 ,  1.000000000000000000 ,  0.305646969146982861 ,  0.809503009719373612 ,  1.000000000000000000 ,  0.092511286569608123 ,  0.977140738624001526  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.136478803018754260 ,  0.475011958916746746 ,  1.000000000000000000 ,  0.458424249364113467 ,  0.910140519196405817  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.531144540933883524 ,  1.000000000000000000 ,  0.833077477463570415 ,  0.541768411337611444 ,  1.000000000000000000 ,  0.302854186589417929 ,  0.936520251882955623  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.896403866045261455 ,  0.279701074783275438 ,  1.000000000000000000 ,  0.605953431943766829 ,  0.635205045627787235 ,  1.000000000000000000 ,  0.205269061071933767 ,  0.951421963413530025  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.430355481071124035 ,  1.000000000000000000 ,  0.728125223529973220 ,  0.369269925592252968 ,  1.000000000000000000 ,  0.439381987265304808 ,  0.726865449336948299 ,  1.000000000000000000 ,  0.141053492145384707 ,  0.967394080048361138  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.774872874095997699 ,  0.206734362239677893 ,  1.000000000000000000 ,  0.570633640297323552 ,  0.486757753675709670 ,  1.000000000000000000 ,  0.315335434840820550 ,  0.799687327179689156 ,  1.000000000000000000 ,  0.097311622138941858 ,  0.976328188148895881  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.136478803018754260 ,  0.475011958916746746 ,  1.000000000000000000 ,  0.458424249364113467 ,  0.910140519196405817  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.527444120133184535 ,  1.000000000000000000 ,  0.832375933174563598 ,  0.536793367178259762 ,  1.000000000000000000 ,  0.305527290148634501 ,  0.934758720642942187  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.890134257841319165 ,  0.275260504963202202 ,  1.000000000000000000 ,  0.607672632171729066 ,  0.629689670820563818 ,  1.000000000000000000 ,  0.207782396080462145 ,  0.949480545586515512  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.423194639680453566 ,  1.000000000000000000 ,  0.723152035431894324 ,  0.359683791456904933 ,  1.000000000000000000 ,  0.445625156829640545 ,  0.718487840956949064 ,  1.000000000000000000 ,  0.145326569151829454 ,  0.966286626445108543  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.760365042986979600 ,  0.198338039924292747 ,  1.000000000000000000 ,  0.570957072812239619 ,  0.473447226250609532 ,  1.000000000000000000 ,  0.323556131942973890 ,  0.790763632128722804 ,  1.000000000000000000 ,  0.101521029544530156 ,  0.975173478134974458  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.136478803018754260 ,  0.475011958916746746 ,  1.000000000000000000 ,  0.458424249364113467 ,  0.910140519196405817  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523901082294863318 ,  1.000000000000000000 ,  0.831615749628297762 ,  0.531998891384663386 ,  1.000000000000000000 ,  0.308065949459997213 ,  0.932944326378019384  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.882317877310122456 ,  0.269744363832889855 ,  1.000000000000000000 ,  0.610283930233233907 ,  0.623089179384411107 ,  1.000000000000000000 ,  0.211304994405917490 ,  0.948040557806805073  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.417133325496318041 ,  1.000000000000000000 ,  0.718679520790302728 ,  0.351559903473235902 ,  1.000000000000000000 ,  0.450705737406548790 ,  0.711008488008240902 ,  1.000000000000000000 ,  0.148950060071126489 ,  0.964832357840605614  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.747503235747472128 ,  0.191078626173476873 ,  1.000000000000000000 ,  0.570896829594218391 ,  0.461617219915057275 ,  1.000000000000000000 ,  0.330785264741626228 ,  0.782452902526670546 ,  1.000000000000000000 ,  0.105333272909813980 ,  0.973793238649923620  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.136478803018754260 ,  0.475011958916746746 ,  1.000000000000000000 ,  0.458424249364113467 ,  0.910140519196405817  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.522343609060698877 ,  1.000000000000000000 ,  0.831980536413385097 ,  0.530240250901055976 ,  1.000000000000000000 ,  0.309874777587448802 ,  0.933730397082159325  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.877568797942333778 ,  0.266325095009692647 ,  1.000000000000000000 ,  0.613056169668385609 ,  0.619738257164351825 ,  1.000000000000000000 ,  0.214304743763729572 ,  0.949505236613252923  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.411898829804173583 ,  1.000000000000000000 ,  0.714609677810304045 ,  0.344536090056543898 ,  1.000000000000000000 ,  0.454902756898321381 ,  0.704230767907979338 ,  1.000000000000000000 ,  0.152063673746635625 ,  0.963111616041033347  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.736033076453902480 ,  0.184748744210570776 ,  1.000000000000000000 ,  0.570551753296669739 ,  0.451044763364802481 ,  1.000000000000000000 ,  0.337154140484352050 ,  0.774692816498434023 ,  1.000000000000000000 ,  0.108785749903949028 ,  0.972213299991745439  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_08Denominator[13][7][24] = {
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.187077997278361160 ,  0.569888481025381921 ,  1.000000000000000000 ,  0.370784356147854155 ,  0.937675862126339132  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.643025502314864883 ,  1.000000000000000000 ,  0.784991680831434335 ,  0.694972415497939866 ,  1.000000000000000000 ,  0.202197267684965182 ,  0.967842611514265094  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.070394517996658970 ,  0.448583145536114780 ,  1.000000000000000000 ,  0.477207114314757430 ,  0.798081762522298344 ,  1.000000000000000000 ,  0.114041598853045989 ,  0.976648442560229868  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.591072334627808948 ,  1.000000000000000000 ,  0.756377793448342328 ,  0.602102549441811186 ,  1.000000000000000000 ,  0.275221128701770401 ,  0.882766696891958080 ,  1.000000000000000000 ,  0.062391643922816077 ,  0.987312389770784615  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.056929594779329350 ,  0.440823033473138448 ,  1.000000000000000000 ,  0.463308021273127313 ,  0.766712488034578810 ,  1.000000000000000000 ,  0.145097937314018127 ,  0.938979596658065074 ,  1.000000000000000000 ,  0.031197033260259684 ,  0.991735426635744100  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.160239747385202410 ,  0.532997066472455816 ,  1.000000000000000000 ,  0.389898086151501899 ,  0.927820775896983418  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.608297812695214435 ,  1.000000000000000000 ,  0.792516398898978292 ,  0.653623103320056575 ,  1.000000000000000000 ,  0.221398794649538655 ,  0.960516448002571188  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.026089953972819660 ,  0.403038934487689404 ,  1.000000000000000000 ,  0.502484050402114968 ,  0.765453156534682599 ,  1.000000000000000000 ,  0.129020833518917299 ,  0.974009335393697162  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.552251662003504173 ,  1.000000000000000000 ,  0.753125065890765333 ,  0.550925167630634149 ,  1.000000000000000000 ,  0.303365612432643927 ,  0.858064255178310087 ,  1.000000000000000000 ,  0.073477028582620049 ,  0.986248867650965089  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.980569519333144290 ,  0.367151973555528754 ,  1.000000000000000000 ,  0.494349460456092737 ,  0.701786155429813263 ,  1.000000000000000000 ,  0.177259205960676097 ,  0.915185732416206377 ,  1.000000000000000000 ,  0.041521868599312513 ,  0.990379664872733101  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.141799462460338030 ,  0.508972927698864819 ,  1.000000000000000000 ,  0.403623515210094375 ,  0.921641511004523628  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.583475061842834597 ,  1.000000000000000000 ,  0.796128693967859458 ,  0.622971616191184241 ,  1.000000000000000000 ,  0.236378854457937632 ,  0.954466697999066804  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.989112635128613249 ,  0.368196391127889933 ,  1.000000000000000000 ,  0.521698558648760136 ,  0.736757608497959238 ,  1.000000000000000000 ,  0.142135304740070606 ,  0.970310368250549726  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.520021006497378147 ,  1.000000000000000000 ,  0.745456797637911728 ,  0.506768330797845556 ,  1.000000000000000000 ,  0.326896768297144813 ,  0.831648271645926806 ,  1.000000000000000000 ,  0.083764305181224436 ,  0.981013426010176270  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.931404058898620546 ,  0.325263385099041624 ,  1.000000000000000000 ,  0.510022440269084498 ,  0.657710225004305538 ,  1.000000000000000000 ,  0.199189515737837775 ,  0.895634339065512264 ,  1.000000000000000000 ,  0.049232549442596474 ,  0.987372008711735472  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.126981395763821720 ,  0.490248055225822432 ,  1.000000000000000000 ,  0.415505829016695472 ,  0.917563622989780670  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.563957242491113120 ,  1.000000000000000000 ,  0.798578260844383436 ,  0.598718003573577850 ,  1.000000000000000000 ,  0.249517891732543656 ,  0.950751280496042073  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.957639344620702104 ,  0.340553041709586168 ,  1.000000000000000000 ,  0.536541586243619917 ,  0.711279509141429123 ,  1.000000000000000000 ,  0.153681347313641137 ,  0.965792150447450815  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.494710487943400057 ,  1.000000000000000000 ,  0.737753333151730084 ,  0.472117818748045037 ,  1.000000000000000000 ,  0.347230127875830075 ,  0.810231848700247004 ,  1.000000000000000000 ,  0.093334178091981801 ,  0.978957661973939186  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.890322285138620217 ,  0.293075787140986477 ,  1.000000000000000000 ,  0.520694505511300476 ,  0.619941621321222991 ,  1.000000000000000000 ,  0.218379079372934215 ,  0.877057820436844215 ,  1.000000000000000000 ,  0.056448634432705584 ,  0.984045094113031538  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.115145647002887100 ,  0.476392323954077379 ,  1.000000000000000000 ,  0.423080494192932643 ,  0.912028190228903024  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.547667727265663040 ,  1.000000000000000000 ,  0.799257418390266206 ,  0.577792635172505009 ,  1.000000000000000000 ,  0.260691604383559594 ,  0.946175002670863963  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.930886258657089294 ,  0.318399686398493031 ,  1.000000000000000000 ,  0.547824821294874975 ,  0.688786538720706387 ,  1.000000000000000000 ,  0.163649995005051341 ,  0.960514916418083731  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.473588909948743420 ,  1.000000000000000000 ,  0.729407371174817021 ,  0.442987343997736649 ,  1.000000000000000000 ,  0.364583058625603029 ,  0.790387034222674312 ,  1.000000000000000000 ,  0.102148262962575065 ,  0.976458083604940352  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.854821442055336611 ,  0.266988108294917814 ,  1.000000000000000000 ,  0.529510544960592910 ,  0.587535181265211648 ,  1.000000000000000000 ,  0.236936799446302526 ,  0.861947480085834772 ,  1.000000000000000000 ,  0.063801334419881770 ,  0.984433824180722339  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.106704501004983190 ,  0.466589739009074245 ,  1.000000000000000000 ,  0.429018321966223581 ,  0.908658990687002288  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.536740660830734240 ,  1.000000000000000000 ,  0.799474862646478268 ,  0.563697906837821838 ,  1.000000000000000000 ,  0.268671403678808818 ,  0.943348911098884857  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.910167780991028907 ,  0.301881466038042667 ,  1.000000000000000000 ,  0.556987084801848553 ,  0.671673820290751622 ,  1.000000000000000000 ,  0.172199386426350354 ,  0.957907740045124134  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455974349348954255 ,  1.000000000000000000 ,  0.721004611383016747 ,  0.418604305346335637 ,  1.000000000000000000 ,  0.379133627298352371 ,  0.772249226394670751 ,  1.000000000000000000 ,  0.110077757368367848 ,  0.973479317566417923  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.823671045138032576 ,  0.245582210440484089 ,  1.000000000000000000 ,  0.534440887395599029 ,  0.558022742867131827 ,  1.000000000000000000 ,  0.252436589099972575 ,  0.844834984850774573 ,  1.000000000000000000 ,  0.070351839840887109 ,  0.980686215028485520  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.100772001887082130 ,  0.459447159471164102 ,  1.000000000000000000 ,  0.434567319238713778 ,  0.907829078598929251  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.527218159866731262 ,  1.000000000000000000 ,  0.799057267037995511 ,  0.551147735866662658 ,  1.000000000000000000 ,  0.275587479019005710 ,  0.940084872778745084  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.892431949027457594 ,  0.288290118280134455 ,  1.000000000000000000 ,  0.564051167428782363 ,  0.656575225304876442 ,  1.000000000000000000 ,  0.179511731213877757 ,  0.954717146765060476  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.441291519775478147 ,  1.000000000000000000 ,  0.712910687118601172 ,  0.398246738245012444 ,  1.000000000000000000 ,  0.391133141920310068 ,  0.755838957443719206 ,  1.000000000000000000 ,  0.117058505142601896 ,  0.970016471540305125  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.798483162449779904 ,  0.229044043457936497 ,  1.000000000000000000 ,  0.538445455809704709 ,  0.534523366715598081 ,  1.000000000000000000 ,  0.266358920388982678 ,  0.832004364922361295 ,  1.000000000000000000 ,  0.076461191039869716 ,  0.980325672167282502  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.096385801621855060 ,  0.454466754317897792 ,  1.000000000000000000 ,  0.437825254811873232 ,  0.906241740864520584  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.518904916389660076 ,  1.000000000000000000 ,  0.798147662535310554 ,  0.539962103883378508 ,  1.000000000000000000 ,  0.281511356675832047 ,  0.936429716352670249  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.878934103737248340 ,  0.278128024493774162 ,  1.000000000000000000 ,  0.570344975573381263 ,  0.645655854118258210 ,  1.000000000000000000 ,  0.185940361272233151 ,  0.954395706821982826  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.429616395667056006 ,  1.000000000000000000 ,  0.706522674940440854 ,  0.382371652260385608 ,  1.000000000000000000 ,  0.401853838447325529 ,  0.743630869499621650 ,  1.000000000000000000 ,  0.123453215517629514 ,  0.969735119610434904  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.776100623101210529 ,  0.214978315799457587 ,  1.000000000000000000 ,  0.541186330733139598 ,  0.513519371684543424 ,  1.000000000000000000 ,  0.278972966519014542 ,  0.819822796651168417 ,  1.000000000000000000 ,  0.082240440728584496 ,  0.979763899899741553  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.092379546749927770 ,  0.450033960398063371 ,  1.000000000000000000 ,  0.440530202942739146 ,  0.904477108896272419  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.513709761271053722 ,  1.000000000000000000 ,  0.797918269158784721 ,  0.533176113237992522 ,  1.000000000000000000 ,  0.285695817995470613 ,  0.935101761937211728  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.867515237285055085 ,  0.269758490565846054 ,  1.000000000000000000 ,  0.575261290022303418 ,  0.636184218033222004 ,  1.000000000000000000 ,  0.191354652821959209 ,  0.953594471340606709  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.419842448128275036 ,  1.000000000000000000 ,  0.700649037971323052 ,  0.369080552843097043 ,  1.000000000000000000 ,  0.410734721558451976 ,  0.732767095460839735 ,  1.000000000000000000 ,  0.129016304997581793 ,  0.969021305777644559  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.755592664083893917 ,  0.202676790407467444 ,  1.000000000000000000 ,  0.541815590601573138 ,  0.493654585315835226 ,  1.000000000000000000 ,  0.289265910561129103 ,  0.805670255966989046 ,  1.000000000000000000 ,  0.087244507574809338 ,  0.975193721459649332  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.092379546749927770 ,  0.450033960398063371 ,  1.000000000000000000 ,  0.440530202942739146 ,  0.904477108896272419  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.509074487815490362 ,  1.000000000000000000 ,  0.797495208461511451 ,  0.527027683084034027 ,  1.000000000000000000 ,  0.289357011602710046 ,  0.933563555230950803  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.856455623773247243 ,  0.262000089136167735 ,  1.000000000000000000 ,  0.577936004188652097 ,  0.625822621852622829 ,  1.000000000000000000 ,  0.195430163542470031 ,  0.948939711774064776  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.411551068621438865 ,  1.000000000000000000 ,  0.695267015203410055 ,  0.357808877741311870 ,  1.000000000000000000 ,  0.418137058740990497 ,  0.723034675927986203 ,  1.000000000000000000 ,  0.133865705012738812 ,  0.967945757973238230  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.739112686260502749 ,  0.193060697542282000 ,  1.000000000000000000 ,  0.542702920584612225 ,  0.478079350564267092 ,  1.000000000000000000 ,  0.298639301766223553 ,  0.795499165662306606 ,  1.000000000000000000 ,  0.091878717162821186 ,  0.974021089296214004  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.092379546749927770 ,  0.450033960398063371 ,  1.000000000000000000 ,  0.440530202942739146 ,  0.904477108896272419  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.505022771439593621 ,  1.000000000000000000 ,  0.796893115590220580 ,  0.521551919253940732 ,  1.000000000000000000 ,  0.292437039260591503 ,  0.931796432421662324  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.852109310954626609 ,  0.258854407537328912 ,  1.000000000000000000 ,  0.580489355502658455 ,  0.622621457926338273 ,  1.000000000000000000 ,  0.198070975787859427 ,  0.950124263035134198  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.404445075290944311 ,  1.000000000000000000 ,  0.690342214429374268 ,  0.348152000168685749 ,  1.000000000000000000 ,  0.424328714622487113 ,  0.714266807620172139 ,  1.000000000000000000 ,  0.138095156369622274 ,  0.966563539417152673  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.724697633408031283 ,  0.184893080985820801 ,  1.000000000000000000 ,  0.543049209191049931 ,  0.464418351662252427 ,  1.000000000000000000 ,  0.306810855581227671 ,  0.786111287859009078 ,  1.000000000000000000 ,  0.096053763932560543 ,  0.972607732906220379  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.092379546749927770 ,  0.450033960398063371 ,  1.000000000000000000 ,  0.440530202942739146 ,  0.904477108896272419  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.502888082194182795 ,  1.000000000000000000 ,  0.797384030444351466 ,  0.519076249410792889 ,  1.000000000000000000 ,  0.294866265029897046 ,  0.932759145399935763  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.844523116187821166 ,  0.253595241910306080 ,  1.000000000000000000 ,  0.582984730381892913 ,  0.615918663352007512 ,  1.000000000000000000 ,  0.201455997608650045 ,  0.948407249342896663  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.398445324427057657 ,  1.000000000000000000 ,  0.685922142551818936 ,  0.339992780392520899 ,  1.000000000000000000 ,  0.429359416685468676 ,  0.706468732843958724 ,  1.000000000000000000 ,  0.141677768728395825 ,  0.964868995817036224  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.712282272862354571 ,  0.178039076415594522 ,  1.000000000000000000 ,  0.542980362239471326 ,  0.452615903285782439 ,  1.000000000000000000 ,  0.313757519026641674 ,  0.777567657920879007 ,  1.000000000000000000 ,  0.099714263359345343 ,  0.970931906642564346  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.092379546749927770 ,  0.450033960398063371 ,  1.000000000000000000 ,  0.440530202942739146 ,  0.904477108896272419  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.500148173667253393 ,  1.000000000000000000 ,  0.796442707388195670 ,  0.515121307282785379 ,  1.000000000000000000 ,  0.296525222471457406 ,  0.930431418849733682  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.838407933785207615 ,  0.249437866408614178 ,  1.000000000000000000 ,  0.584603899998331622 ,  0.610306325682938855 ,  1.000000000000000000 ,  0.203980282230375842 ,  0.946313425616034709  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.393399761513027579 ,  1.000000000000000000 ,  0.681986119423005910 ,  0.333119104541330369 ,  1.000000000000000000 ,  0.433371147026287373 ,  0.699552048339149857 ,  1.000000000000000000 ,  0.144658623719979118 ,  0.962890145820142207  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.702794628866188487 ,  0.172856251636789859 ,  1.000000000000000000 ,  0.543634074759640207 ,  0.443975513248946907 ,  1.000000000000000000 ,  0.320284221507426192 ,  0.772622325995583781 ,  1.000000000000000000 ,  0.103121557183706872 ,  0.972653641516593726  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_10Denominator[13][7][24] = {
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.149265179843571570 ,  0.546409659113331392 ,  1.000000000000000000 ,  0.358284995201357648 ,  0.935685322744248538  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.620610759285462277 ,  1.000000000000000000 ,  0.755106606152781246 ,  0.679602969566377002 ,  1.000000000000000000 ,  0.193259928007768339 ,  0.964311811737693536  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.033181900271285870 ,  0.428089688720419415 ,  1.000000000000000000 ,  0.457152129480598579 ,  0.792338245738223868 ,  1.000000000000000000 ,  0.108324376247448881 ,  0.977261604729928734  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.571766124096233175 ,  1.000000000000000000 ,  0.727636111142083708 ,  0.590752309907487283 ,  1.000000000000000000 ,  0.261480467966253960 ,  0.881636937960993183 ,  1.000000000000000000 ,  0.058638655593822676 ,  0.988783415315388381  },
/* 8 Poles*/  { 1.000000000000000000 ,  1.009124893619022690 ,  0.409552763332746816 ,  1.000000000000000000 ,  0.447984080422172193 ,  0.751711388869641173 ,  1.000000000000000000 ,  0.141249342098040687 ,  0.936121990078428556 ,  1.000000000000000000 ,  0.030432701543293712 ,  0.993097527211618702  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.123003661696859940 ,  0.510818351871259013 ,  1.000000000000000000 ,  0.376534149062512424 ,  0.924596701564124768  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.589900337095187410 ,  1.000000000000000000 ,  0.762679919323380973 ,  0.642869896043942846 ,  1.000000000000000000 ,  0.210649936175100305 ,  0.958614547679065510  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.990280344655138367 ,  0.384479833802783277 ,  1.000000000000000000 ,  0.481475117057779511 ,  0.759309676379819720 ,  1.000000000000000000 ,  0.122688799836362134 ,  0.973615338681720233  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.533019409168995151 ,  1.000000000000000000 ,  0.723240002528107051 ,  0.538107651837174683 ,  1.000000000000000000 ,  0.288098133681206847 ,  0.853075889491912931 ,  1.000000000000000000 ,  0.069126950909792456 ,  0.983199773880866434  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.944856201846735244 ,  0.349034249655894802 ,  1.000000000000000000 ,  0.473404039770467300 ,  0.694645668762641333 ,  1.000000000000000000 ,  0.168088693502280451 ,  0.913729496401909036 ,  1.000000000000000000 ,  0.039063845152432858 ,  0.990277732193362237  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.104973006543120030 ,  0.487640527615501640 ,  1.000000000000000000 ,  0.389638741195460159 ,  0.917579749734415362  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.563537352791640966 ,  1.000000000000000000 ,  0.767072481122979544 ,  0.609985290659347701 ,  1.000000000000000000 ,  0.226791246529467222 ,  0.952346169553663580  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.953900964314454813 ,  0.350590519345481244 ,  1.000000000000000000 ,  0.500349600227070201 ,  0.729942345069785170 ,  1.000000000000000000 ,  0.135518646550848249 ,  0.969164630706597308  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.502010577022747317 ,  1.000000000000000000 ,  0.717166437262295564 ,  0.495609413011483979 ,  1.000000000000000000 ,  0.311982205871308260 ,  0.828924601009577655 ,  1.000000000000000000 ,  0.079393477167391549 ,  0.981099967372560444  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.897797833980885640 ,  0.309447515489487679 ,  1.000000000000000000 ,  0.488431790366655516 ,  0.651048267503187006 ,  1.000000000000000000 ,  0.188938494718905392 ,  0.893835945956941602 ,  1.000000000000000000 ,  0.046355904226722970 ,  0.986665046606134410  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.090481893119365610 ,  0.469562331415847800 ,  1.000000000000000000 ,  0.401002607392901667 ,  0.912825305788786956  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.544516915240579991 ,  1.000000000000000000 ,  0.769349801033043512 ,  0.585808336453159773 ,  1.000000000000000000 ,  0.239493697200803379 ,  0.947870354068250709  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.923060692921934711 ,  0.323843969076547500 ,  1.000000000000000000 ,  0.514851183727964123 ,  0.704005220869813431 ,  1.000000000000000000 ,  0.146771711906009544 ,  0.963969373301931443  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.477050956860627207 ,  1.000000000000000000 ,  0.709624386685007380 ,  0.460911127044424440 ,  1.000000000000000000 ,  0.331981067878424851 ,  0.806770113001688105 ,  1.000000000000000000 ,  0.088767901914394917 ,  0.978490237142842112  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.858811484387442925 ,  0.279095824928765446 ,  1.000000000000000000 ,  0.500042552352559122 ,  0.614892292452142053 ,  1.000000000000000000 ,  0.208272009081393483 ,  0.877877223596580492 ,  1.000000000000000000 ,  0.053545350751887280 ,  0.986674682019499039  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.080640825977560840 ,  0.457638534997791668 ,  1.000000000000000000 ,  0.408914272114415445 ,  0.909759672611325287  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.528650028000622774 ,  1.000000000000000000 ,  0.769922896362449816 ,  0.564986060963137304 ,  1.000000000000000000 ,  0.250283046980843693 ,  0.942638849526015732  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.898554046246286653 ,  0.303589923697214770 ,  1.000000000000000000 ,  0.526792518634745299 ,  0.683688198346666054 ,  1.000000000000000000 ,  0.156746572287437658 ,  0.961597013160372716  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.456334920580120207 ,  1.000000000000000000 ,  0.701455816745203076 ,  0.431913492962984147 ,  1.000000000000000000 ,  0.348956857278758858 ,  0.786353114499910633 ,  1.000000000000000000 ,  0.097367913351391652 ,  0.975469079507649162  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.823235273765357434 ,  0.253445333930564731 ,  1.000000000000000000 ,  0.507515186963549469 ,  0.580586294020168259 ,  1.000000000000000000 ,  0.225514989362293583 ,  0.859173661078354911 ,  1.000000000000000000 ,  0.060408362028383913 ,  0.982769642424583090  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.071766904232928310 ,  0.447322327336477754 ,  1.000000000000000000 ,  0.415472404441946885 ,  0.906281960324831215  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.517077286296556471 ,  1.000000000000000000 ,  0.770361833869368406 ,  0.549896480920729291 ,  1.000000000000000000 ,  0.258911456635150750 ,  0.939710050114890394  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.878291164307915873 ,  0.287632255223372324 ,  1.000000000000000000 ,  0.535694440152625462 ,  0.666281489604366839 ,  1.000000000000000000 ,  0.165052706175044944 ,  0.958472416759962087  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.439470021185507986 ,  1.000000000000000000 ,  0.693382020097948692 ,  0.408215083433941250 ,  1.000000000000000000 ,  0.362783481167988753 ,  0.768069820664396219 ,  1.000000000000000000 ,  0.104892777534733528 ,  0.971925049423278953  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.794897081611025791 ,  0.234107132157422515 ,  1.000000000000000000 ,  0.513216352576025381 ,  0.553541908366192326 ,  1.000000000000000000 ,  0.240719676451160602 ,  0.845053626324803853 ,  1.000000000000000000 ,  0.066744975296060854 ,  0.982260578470848356  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.066494498950377960 ,  0.441132259819586980 ,  1.000000000000000000 ,  0.420003238249978317 ,  0.904923091107081912  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.508819043032388896 ,  1.000000000000000000 ,  0.771188466066358269 ,  0.539446386827633906 ,  1.000000000000000000 ,  0.265942974005962673 ,  0.939266690172405383  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.860669946562394372 ,  0.274295565629541327 ,  1.000000000000000000 ,  0.542726871169273539 ,  0.650738837869041853 ,  1.000000000000000000 ,  0.172311953887907404 ,  0.954899190687204302  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.425891197616079964 ,  1.000000000000000000 ,  0.686756809043052696 ,  0.389463846407274994 ,  1.000000000000000000 ,  0.375193980693086571 ,  0.754085579537117412 ,  1.000000000000000000 ,  0.111896858647431319 ,  0.971656739790959234  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.767957811527908984 ,  0.216762053089171514 ,  1.000000000000000000 ,  0.516352445033045893 ,  0.527038207427060201 ,  1.000000000000000000 ,  0.254312495969351515 ,  0.828272210316016388 ,  1.000000000000000000 ,  0.072762153716269568 ,  0.977922328392311524  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.061733941477458030 ,  0.435698319050682570 ,  1.000000000000000000 ,  0.423800702384419248 ,  0.903342472275914798  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.501479146342787341 ,  1.000000000000000000 ,  0.770036455351483573 ,  0.529199107961878856 ,  1.000000000000000000 ,  0.270864744446534522 ,  0.934936896148428009  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.847523959423407969 ,  0.264532636755042405 ,  1.000000000000000000 ,  0.548797370582981059 ,  0.639672018270225129 ,  1.000000000000000000 ,  0.178527843630993610 ,  0.954206438395030942  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.414458947303142033 ,  1.000000000000000000 ,  0.680501641647638045 ,  0.373676515679515819 ,  1.000000000000000000 ,  0.385643895428188388 ,  0.741523331470096525 ,  1.000000000000000000 ,  0.118119526283577816 ,  0.971000978947211157  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.746637788857782647 ,  0.203574816065926395 ,  1.000000000000000000 ,  0.518940605593584681 ,  0.506434623674696938 ,  1.000000000000000000 ,  0.266252738578903192 ,  0.815979811973944091 ,  1.000000000000000000 ,  0.078222685132902550 ,  0.976945816933899924  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.057822115492550900 ,  0.431426640719875798 ,  1.000000000000000000 ,  0.426364449294555303 ,  0.901397668677407782  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.495804688034240326 ,  1.000000000000000000 ,  0.769933714688776805 ,  0.521721474619297521 ,  1.000000000000000000 ,  0.275562275993205374 ,  0.933609833875162720  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.834896458649651008 ,  0.255570137709516643 ,  1.000000000000000000 ,  0.552539821445936496 ,  0.627815155066630881 ,  1.000000000000000000 ,  0.183430896400633342 ,  0.949656907154520224  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.403947570612095908 ,  1.000000000000000000 ,  0.673398652786301288 ,  0.358875093933717637 ,  1.000000000000000000 ,  0.393776844861270292 ,  0.727470978850238259 ,  1.000000000000000000 ,  0.123432531900463507 ,  0.966428349471065040  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.727970395076196319 ,  0.192466192246935508 ,  1.000000000000000000 ,  0.520564605185588669 ,  0.488323189879734332 ,  1.000000000000000000 ,  0.276817254828420678 ,  0.804539607873145157 ,  1.000000000000000000 ,  0.083249253548585478 ,  0.975746591689476195  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.057822115492550900 ,  0.431426640719875798 ,  1.000000000000000000 ,  0.426364449294555303 ,  0.901397668677407782  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491332124995598862 ,  1.000000000000000000 ,  0.769475885900045675 ,  0.515652712405269775 ,  1.000000000000000000 ,  0.279052061345176072 ,  0.931837533998717604  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.825440901115985004 ,  0.248904531705363086 ,  1.000000000000000000 ,  0.556223991965897446 ,  0.619486670257195615 ,  1.000000000000000000 ,  0.187819215474333945 ,  0.948196240568990434  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.395893654796016736 ,  1.000000000000000000 ,  0.668160808374505089 ,  0.347784906805640448 ,  1.000000000000000000 ,  0.400932981372810804 ,  0.717567082054351490 ,  1.000000000000000000 ,  0.128120145102710636 ,  0.965065736586242950  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.711854912618502422 ,  0.183197209362478713 ,  1.000000000000000000 ,  0.521437093584594114 ,  0.472635557071607981 ,  1.000000000000000000 ,  0.285952869620289818 ,  0.794062250797496594 ,  1.000000000000000000 ,  0.087757449725341608 ,  0.974292780242213108  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.057822115492550900 ,  0.431426640719875798 ,  1.000000000000000000 ,  0.426364449294555303 ,  0.901397668677407782  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.487416134469115248 ,  1.000000000000000000 ,  0.768852759089304061 ,  0.510242822817490227 ,  1.000000000000000000 ,  0.281990974214082257 ,  0.929868376415648790  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.820859162406543441 ,  0.245637522956207249 ,  1.000000000000000000 ,  0.558887222855442456 ,  0.615966361928525941 ,  1.000000000000000000 ,  0.190583573009167201 ,  0.949334334755185694  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.388994403888666185 ,  1.000000000000000000 ,  0.663371397832140608 ,  0.338291044814619912 ,  1.000000000000000000 ,  0.406915330724877011 ,  0.708659366565851379 ,  1.000000000000000000 ,  0.132206551139263329 ,  0.963432236502794082  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.697780805648335556 ,  0.175340010595217810 ,  1.000000000000000000 ,  0.521776943100170310 ,  0.458902821984951137 ,  1.000000000000000000 ,  0.293904920790354407 ,  0.784415000226854398 ,  1.000000000000000000 ,  0.091813951217314116 ,  0.972623366868036410  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.057822115492550900 ,  0.431426640719875798 ,  1.000000000000000000 ,  0.426364449294555303 ,  0.901397668677407782  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.485354029908077966 ,  1.000000000000000000 ,  0.769308410176779534 ,  0.507793537613877910 ,  1.000000000000000000 ,  0.284319562343178678 ,  0.930713870639024776  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.813526349696050866 ,  0.240631587673617031 ,  1.000000000000000000 ,  0.561255435436793570 ,  0.609239255554354986 ,  1.000000000000000000 ,  0.193827045459990549 ,  0.947396025946151021  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.383877457436122838 ,  1.000000000000000000 ,  0.660291146539529694 ,  0.331493622234394270 ,  1.000000000000000000 ,  0.412534751953071988 ,  0.703337013862632254 ,  1.000000000000000000 ,  0.135917760087666289 ,  0.965075126043985154  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.685673284055788534 ,  0.168756362139550198 ,  1.000000000000000000 ,  0.521710344207871435 ,  0.447056663976264756 ,  1.000000000000000000 ,  0.300657621152399579 ,  0.775654831094450881 ,  1.000000000000000000 ,  0.095367370291590495 ,  0.970720327510558034  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.057822115492550900 ,  0.431426640719875798 ,  1.000000000000000000 ,  0.426364449294555303 ,  0.901397668677407782  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.482695633432385973 ,  1.000000000000000000 ,  0.768371751613928122 ,  0.503878348856713676 ,  1.000000000000000000 ,  0.285905909877682607 ,  0.928257770885009825  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.807610617311374290 ,  0.236671130786658135 ,  1.000000000000000000 ,  0.562788751027261336 ,  0.603609116393683753 ,  1.000000000000000000 ,  0.196246483513799558 ,  0.945128918024729536  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.378849369453006957 ,  1.000000000000000000 ,  0.656381454548528032 ,  0.324555574114446743 ,  1.000000000000000000 ,  0.416549599183118291 ,  0.696171120195866022 ,  1.000000000000000000 ,  0.138893502286951354 ,  0.962942561828675547  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.676157185853079601 ,  0.163639182560576441 ,  1.000000000000000000 ,  0.522353002113339238 ,  0.438131170338927012 ,  1.000000000000000000 ,  0.307171219610876178 ,  0.770383225901388968 ,  1.000000000000000000 ,  0.098765690934743966 ,  0.972295959821819822  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_12Denominator[13][7][24] = {
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.116056670164754070 ,  0.526275711981621597 ,  1.000000000000000000 ,  0.346534750086042842 ,  0.930686760148138559  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.604057485955211249 ,  1.000000000000000000 ,  0.731042830341485539 ,  0.670626698735764526 ,  1.000000000000000000 ,  0.185510190028822436 ,  0.964390478493313275  },
/* 6 Poles*/  { 1.000000000000000000 ,  1.002248297223490070 ,  0.411745022791759285 ,  1.000000000000000000 ,  0.439985478974250721 ,  0.787258151441238452 ,  1.000000000000000000 ,  0.103390829184430544 ,  0.976514732131156693  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.555467829566664184 ,  1.000000000000000000 ,  0.703160673598874131 ,  0.580992954120714833 ,  1.000000000000000000 ,  0.249788224807495851 ,  0.879207785714162249 ,  1.000000000000000000 ,  0.055469545500073457 ,  0.987894172318598218  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.985948121577706393 ,  0.400993997974687488 ,  1.000000000000000000 ,  0.427923495990199576 ,  0.753418672867692485 ,  1.000000000000000000 ,  0.131507830632023009 ,  0.938246601959021431 ,  1.000000000000000000 ,  0.027818314548725003 ,  0.993952147574695188  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.092099652942415620 ,  0.493332423595069070 ,  1.000000000000000000 ,  0.364785518797772046 ,  0.921787345671609692  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.571543445959433960 ,  1.000000000000000000 ,  0.739516554033397000 ,  0.631253042066667880 ,  1.000000000000000000 ,  0.204193772085322939 ,  0.958505544667055065  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.959815063403641533 ,  0.368964836930719742 ,  1.000000000000000000 ,  0.464050794811989020 ,  0.753459510225241047 ,  1.000000000000000000 ,  0.117530652562646193 ,  0.972206782679365777  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.517799783675495617 ,  1.000000000000000000 ,  0.700319091799726512 ,  0.529786665552002711 ,  1.000000000000000000 ,  0.276821690163328860 ,  0.852952039438065079 ,  1.000000000000000000 ,  0.065969526040233567 ,  0.985566541585968348  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.917529106139109984 ,  0.336434877607818983 ,  1.000000000000000000 ,  0.455739846684286853 ,  0.691261425906237292 ,  1.000000000000000000 ,  0.159920317635674797 ,  0.913987378024566310 ,  1.000000000000000000 ,  0.036836601668111153 ,  0.990969683105717869  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.073461294519048040 ,  0.469380726249901570 ,  1.000000000000000000 ,  0.378775087408189382 ,  0.914510717064492606  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.547657773370776257 ,  1.000000000000000000 ,  0.742855280102215798 ,  0.600490930696979786 ,  1.000000000000000000 ,  0.218410949457967379 ,  0.950856387860962937  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.924010866199846959 ,  0.335938459313535387 ,  1.000000000000000000 ,  0.482604797889276738 ,  0.723583376142112433 ,  1.000000000000000000 ,  0.130105464537248444 ,  0.967148938846756456  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.487779979141956865 ,  1.000000000000000000 ,  0.694473788541858150 ,  0.487974761895926146 ,  1.000000000000000000 ,  0.299813078312838654 ,  0.828462248287362923 ,  1.000000000000000000 ,  0.075808711089061787 ,  0.982835158417146748  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.870488859250044822 ,  0.297144371781143501 ,  1.000000000000000000 ,  0.470953402829211942 ,  0.646499905301551547 ,  1.000000000000000000 ,  0.180700794796965963 ,  0.893319599003525089 ,  1.000000000000000000 ,  0.044058351662446017 ,  0.987012495363344011  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.061855917498186530 ,  0.454581900742674516 ,  1.000000000000000000 ,  0.389133003404924682 ,  0.911855330805670494  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.529869746238171846 ,  1.000000000000000000 ,  0.746393732092995421 ,  0.578288274697857485 ,  1.000000000000000000 ,  0.231238033626150696 ,  0.949155108541530401  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.895888718245177573 ,  0.311544392202121989 ,  1.000000000000000000 ,  0.497392726748899006 ,  0.700241067941111850 ,  1.000000000000000000 ,  0.141128032794554326 ,  0.964860331867728238  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.463037432502780566 ,  1.000000000000000000 ,  0.687072284656795818 ,  0.453067276866662205 ,  1.000000000000000000 ,  0.319591054363988103 ,  0.805612953780061480 ,  1.000000000000000000 ,  0.085039145646578815 ,  0.979768509161147771  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.833026880669306857 ,  0.268234237088939509 ,  1.000000000000000000 ,  0.482151689683884832 ,  0.610771473978095725 ,  1.000000000000000000 ,  0.199163743537500715 ,  0.877180047075509117 ,  1.000000000000000000 ,  0.050891380114173823 ,  0.986563703629682487  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.049751394818091650 ,  0.440525890280858978 ,  1.000000000000000000 ,  0.397134194862474310 ,  0.905709362307317800  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.514230433822418065 ,  1.000000000000000000 ,  0.746929926697925794 ,  0.557355940338565503 ,  1.000000000000000000 ,  0.241792016659219094 ,  0.943377729838953849  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.871761712000277522 ,  0.291776476124087913 ,  1.000000000000000000 ,  0.509114660404435426 ,  0.679553107627260511 ,  1.000000000000000000 ,  0.150894537048114313 ,  0.962008323094939555  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.443028894611255553 ,  1.000000000000000000 ,  0.679188735453885140 ,  0.424634936233827365 ,  1.000000000000000000 ,  0.335884209871525119 ,  0.785007130470651626 ,  1.000000000000000000 ,  0.093268458331481466 ,  0.976224855163827399  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.797739933190042216 ,  0.243020733788860854 ,  1.000000000000000000 ,  0.489671256187791615 ,  0.575852148826355159 ,  1.000000000000000000 ,  0.216233663997528552 ,  0.857892718321555114 ,  1.000000000000000000 ,  0.057654010243349360 ,  0.982328382501767128  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.041093943717550600 ,  0.430582583569568633 ,  1.000000000000000000 ,  0.403387307934002959 ,  0.901896430589090081  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.502823867985624062 ,  1.000000000000000000 ,  0.747337232195372958 ,  0.542189786600570045 ,  1.000000000000000000 ,  0.250230589633036127 ,  0.940038309283623752  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.851510082822926950 ,  0.275972543811031423 ,  1.000000000000000000 ,  0.518044780160238916 ,  0.661619720060393712 ,  1.000000000000000000 ,  0.159193891866812570 ,  0.958545703164518481  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.426030355493563717 ,  1.000000000000000000 ,  0.671129241891350148 ,  0.400435033170792931 ,  1.000000000000000000 ,  0.349857999274327314 ,  0.765960568348659110 ,  1.000000000000000000 ,  0.100841685214021926 ,  0.972387802252434486  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.769760301764573107 ,  0.224126727874098791 ,  1.000000000000000000 ,  0.495337129320661707 ,  0.548440319171353274 ,  1.000000000000000000 ,  0.231202424456108752 ,  0.843314302398144533 ,  1.000000000000000000 ,  0.063872663882657374 ,  0.981516767277699098  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.035948024648329470 ,  0.424611197110683847 ,  1.000000000000000000 ,  0.407722405422262846 ,  0.900327893544756730  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.493809902945173962 ,  1.000000000000000000 ,  0.746816011895998555 ,  0.529811207301768672 ,  1.000000000000000000 ,  0.256659882785448268 ,  0.936001722445944151  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.834263064420999956 ,  0.263054555250268218 ,  1.000000000000000000 ,  0.524886483412249860 ,  0.645906326008045761 ,  1.000000000000000000 ,  0.166262179560706935 ,  0.954599258051626931  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.412710035528470909 ,  1.000000000000000000 ,  0.664627143811500676 ,  0.381784399281737630 ,  1.000000000000000000 ,  0.361991323262637110 ,  0.751649993861934207 ,  1.000000000000000000 ,  0.107680465080909754 ,  0.971780188736243722  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.745319726687708739 ,  0.208453990770484932 ,  1.000000000000000000 ,  0.499278536529647321 ,  0.524308013894389635 ,  1.000000000000000000 ,  0.244625230560410523 ,  0.829577540621767584 ,  1.000000000000000000 ,  0.069727507831468596 ,  0.980511352978648332  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.031293269114661460 ,  0.419357709084072272 ,  1.000000000000000000 ,  0.411363480368015344 ,  0.898568668627256484  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.486776439815041373 ,  1.000000000000000000 ,  0.747158783217632405 ,  0.520594878534234473 ,  1.000000000000000000 ,  0.262632879599148161 ,  0.934939514945276495  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.821397745699123050 ,  0.253601830550444662 ,  1.000000000000000000 ,  0.530787190659146257 ,  0.634709153595307662 ,  1.000000000000000000 ,  0.172310303258881803 ,  0.953611366979573272  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.401514554457489903 ,  1.000000000000000000 ,  0.658496315389729747 ,  0.366114488055157006 ,  1.000000000000000000 ,  0.372189023850144018 ,  0.738827020414212998 ,  1.000000000000000000 ,  0.113747407086849828 ,  0.970819568831174817  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.724268513485444743 ,  0.195550519431790443 ,  1.000000000000000000 ,  0.501860867464462568 ,  0.503403450252794205 ,  1.000000000000000000 ,  0.256379963208843265 ,  0.816887593432903625 ,  1.000000000000000000 ,  0.075087678372582942 ,  0.979263948317495747  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.027459576896494480 ,  0.415216438924211395 ,  1.000000000000000000 ,  0.413827074993076194 ,  0.896493135647200567  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.481259370712654233 ,  1.000000000000000000 ,  0.747014691001533171 ,  0.513172948261442441 ,  1.000000000000000000 ,  0.267161408393016553 ,  0.933374918035007273  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.808831704603932944 ,  0.244774735667611515 ,  1.000000000000000000 ,  0.534550183095971909 ,  0.622584457186845630 ,  1.000000000000000000 ,  0.177207305933332199 ,  0.948843273497551043  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.391438492617723344 ,  1.000000000000000000 ,  0.651658519518195867 ,  0.351729114448260660 ,  1.000000000000000000 ,  0.379904705059842529 ,  0.724767830654094891 ,  1.000000000000000000 ,  0.118792001321090066 ,  0.965906504622010886  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.705883993189782788 ,  0.184716089856708843 ,  1.000000000000000000 ,  0.503476181300467673 ,  0.485080722761682070 ,  1.000000000000000000 ,  0.266755208306527269 ,  0.805110471731686150 ,  1.000000000000000000 ,  0.080012684347437593 ,  0.977811078445648518  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.027459576896494480 ,  0.415216438924211395 ,  1.000000000000000000 ,  0.413827074993076194 ,  0.896493135647200567  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476904882407274300 ,  1.000000000000000000 ,  0.746535886799814641 ,  0.507145641570626693 ,  1.000000000000000000 ,  0.270527854689492420 ,  0.931416789771072895  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.799605291915795324 ,  0.238345146063759394 ,  1.000000000000000000 ,  0.538113051541951215 ,  0.614192877313251451 ,  1.000000000000000000 ,  0.181464360213229953 ,  0.947164126851730237  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.383366002763820768 ,  1.000000000000000000 ,  0.646427864465367152 ,  0.340477396463137350 ,  1.000000000000000000 ,  0.387087395178681903 ,  0.714492095023016138 ,  1.000000000000000000 ,  0.123485700531427350 ,  0.964364837044231149  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.690043513719811541 ,  0.175697945705447472 ,  1.000000000000000000 ,  0.504343256434351295 ,  0.469246040287635724 ,  1.000000000000000000 ,  0.275710774410121706 ,  0.794350950433176717 ,  1.000000000000000000 ,  0.084423491806317252 ,  0.976125888957346644  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.027459576896494480 ,  0.415216438924211395 ,  1.000000000000000000 ,  0.413827074993076194 ,  0.896493135647200567  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.473086925870001840 ,  1.000000000000000000 ,  0.745901607823825419 ,  0.501768430743515736 ,  1.000000000000000000 ,  0.273366447279629776 ,  0.929286509331769928  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.794097432593628860 ,  0.234467565950027684 ,  1.000000000000000000 ,  0.541285388126745759 ,  0.609806754249629934 ,  1.000000000000000000 ,  0.184771699586837007 ,  0.948420900896121632  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.377324971484320204 ,  1.000000000000000000 ,  0.642935976444771962 ,  0.332315528162527740 ,  1.000000000000000000 ,  0.393638475055113735 ,  0.708066360998230948 ,  1.000000000000000000 ,  0.127697608233569015 ,  0.966100430296954138  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.676229469752607892 ,  0.168067338145063166 ,  1.000000000000000000 ,  0.504682568417005539 ,  0.455408710944041117 ,  1.000000000000000000 ,  0.283495430583721897 ,  0.784463794810254855 ,  1.000000000000000000 ,  0.088388082458602649 ,  0.974245199796087591  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.027459576896494480 ,  0.415216438924211395 ,  1.000000000000000000 ,  0.413827074993076194 ,  0.896493135647200567  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.470700525265387415 ,  1.000000000000000000 ,  0.746410599936711994 ,  0.498872450991645899 ,  1.000000000000000000 ,  0.276045954087015766 ,  0.930183034270088194  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.787817824437037073 ,  0.230254264991798629 ,  1.000000000000000000 ,  0.543070890121097549 ,  0.603735473564485203 ,  1.000000000000000000 ,  0.187390224313115000 ,  0.946114833672276001  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.371625301114947959 ,  1.000000000000000000 ,  0.638727436695565642 ,  0.324356996552122800 ,  1.000000000000000000 ,  0.398374483169276039 ,  0.700008482907932050 ,  1.000000000000000000 ,  0.131069841009448185 ,  0.964001265163780929  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.664070107999147896 ,  0.161527626670965263 ,  1.000000000000000000 ,  0.504642524619125843 ,  0.443210948065727972 ,  1.000000000000000000 ,  0.290295004209740692 ,  0.775336206863886823 ,  1.000000000000000000 ,  0.091959132908386879 ,  0.972197179755124874  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.027459576896494480 ,  0.415216438924211395 ,  1.000000000000000000 ,  0.413827074993076194 ,  0.896493135647200567  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.468145639248134460 ,  1.000000000000000000 ,  0.745469646176330603 ,  0.495029544485708850 ,  1.000000000000000000 ,  0.277535356681840040 ,  0.927606045143921420  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.782054756354790048 ,  0.226442240842163051 ,  1.000000000000000000 ,  0.544545192285494695 ,  0.598082467609367230 ,  1.000000000000000000 ,  0.189732481163941508 ,  0.943714612760274130  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.366714940054165328 ,  1.000000000000000000 ,  0.634907585862729107 ,  0.317499035694904175 ,  1.000000000000000000 ,  0.402281380267901667 ,  0.692747435242273113 ,  1.000000000000000000 ,  0.133964661453853273 ,  0.961714941342950214  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.653797119456143161 ,  0.156132553195222651 ,  1.000000000000000000 ,  0.504296702335847669 ,  0.432871772946576638 ,  1.000000000000000000 ,  0.295904257215732769 ,  0.767177496643534185 ,  1.000000000000000000 ,  0.094995094744624864 ,  0.969925673200189409  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_14Denominator[13][7][24] = {
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.089947093418082160 ,  0.512034809166126981 ,  1.000000000000000000 ,  0.336573221675784018 ,  0.929979613643926450  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.590421842218165405 ,  1.000000000000000000 ,  0.709664291884252618 ,  0.663565615379648754 ,  1.000000000000000000 ,  0.178262720356686943 ,  0.963456580698597520  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.976671675584444343 ,  0.398935050154935800 ,  1.000000000000000000 ,  0.425622708817957995 ,  0.784413575047658629 ,  1.000000000000000000 ,  0.099253505864221936 ,  0.977340348232027201  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.541573879730495422 ,  1.000000000000000000 ,  0.682626360647451413 ,  0.573254043891777365 ,  1.000000000000000000 ,  0.240182650290691718 ,  0.877917503619856654 ,  1.000000000000000000 ,  0.052897938167860273 ,  0.987983014234898094  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.951747760669769960 ,  0.379753508284951524 ,  1.000000000000000000 ,  0.416583062768182244 ,  0.741788499458381434 ,  1.000000000000000000 ,  0.128594692172231129 ,  0.934122235249896127 ,  1.000000000000000000 ,  0.027238765403994985 ,  0.992427454416190180  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.066264654921194270 ,  0.479607159103382086 ,  1.000000000000000000 ,  0.354424959138141926 ,  0.920323993485548386  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.557994137704623694 ,  1.000000000000000000 ,  0.718219252786242990 ,  0.623689569166142488 ,  1.000000000000000000 ,  0.196746527928187143 ,  0.956886355136858824  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.934583510639115600 ,  0.356731172659434603 ,  1.000000000000000000 ,  0.449512178620858127 ,  0.749858100672346262 ,  1.000000000000000000 ,  0.113217698850273080 ,  0.972460778244603308  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504964462987046336 ,  1.000000000000000000 ,  0.679986812139896979 ,  0.522800687367772188 ,  1.000000000000000000 ,  0.266294264626980304 ,  0.851426412006579314 ,  1.000000000000000000 ,  0.062975699359640411 ,  0.985099481936110299  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.892167835456491587 ,  0.324440354919175444 ,  1.000000000000000000 ,  0.440347551749228960 ,  0.685939361352782106 ,  1.000000000000000000 ,  0.153133855305478683 ,  0.911291198281402592 ,  1.000000000000000000 ,  0.035029308871914806 ,  0.988547829036097725  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.047841479566832050 ,  0.456038675236055568 ,  1.000000000000000000 ,  0.368109151845733618 ,  0.912477496225398976  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.533603702496558197 ,  1.000000000000000000 ,  0.723560504151175121 ,  0.592945546912438060 ,  1.000000000000000000 ,  0.212493533733559808 ,  0.952568399603335947  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.899814779510821672 ,  0.324878003628306045 ,  1.000000000000000000 ,  0.467426809677163602 ,  0.719907257860034733 ,  1.000000000000000000 ,  0.125327011029178509 ,  0.966775245079934842  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.475017143901933336 ,  1.000000000000000000 ,  0.674300747693483249 ,  0.480565265661674623 ,  1.000000000000000000 ,  0.289171131170690210 ,  0.826222511567561102 ,  1.000000000000000000 ,  0.072709318972513801 ,  0.981984892843708823  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.848568108156874446 ,  0.288032347188651006 ,  1.000000000000000000 ,  0.455917716072837043 ,  0.644338052448791387 ,  1.000000000000000000 ,  0.173305611912057289 ,  0.894013554791662557 ,  1.000000000000000000 ,  0.041965476668140829 ,  0.987969920407904767  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.033874928444891370 ,  0.438925925972645681 ,  1.000000000000000000 ,  0.378707010841186020 ,  0.906713232878224362  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.515322957133088511 ,  1.000000000000000000 ,  0.725598419158692942 ,  0.568776141776652699 ,  1.000000000000000000 ,  0.224559589510669372 ,  0.946927706976854244  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.871535937678145189 ,  0.300513129186706918 ,  1.000000000000000000 ,  0.482348850642974003 ,  0.695784924085650402 ,  1.000000000000000000 ,  0.136398996715526355 ,  0.964140730181544558  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.451034140998746835 ,  1.000000000000000000 ,  0.667171336872999010 ,  0.446286327028165897 ,  1.000000000000000000 ,  0.308235541495417042 ,  0.803253744172222173 ,  1.000000000000000000 ,  0.081568839858612269 ,  0.978448766525715996  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.811136547970950583 ,  0.259301350827920218 ,  1.000000000000000000 ,  0.467216901473881507 ,  0.607765125993536714 ,  1.000000000000000000 ,  0.191682417574909475 ,  0.877277923712272423 ,  1.000000000000000000 ,  0.048731466705419564 ,  0.987252263038190914  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.023732106895467900 ,  0.426736857063989561 ,  1.000000000000000000 ,  0.387030068191747012 ,  0.903209036869162207  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.500978543654793218 ,  1.000000000000000000 ,  0.727317578935077424 ,  0.549972966194649859 ,  1.000000000000000000 ,  0.235204588915867041 ,  0.944038118268063386  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.847824396985715767 ,  0.281249980039443292 ,  1.000000000000000000 ,  0.493831529346765330 ,  0.674852398159492273 ,  1.000000000000000000 ,  0.145958744582961597 ,  0.960885286870430400  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.430765114606864674 ,  1.000000000000000000 ,  0.659294485003236730 ,  0.417160382863052015 ,  1.000000000000000000 ,  0.324780945444426150 ,  0.781822031811687856 ,  1.000000000000000000 ,  0.089886560494265577 ,  0.974665830886612738  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.777072322912977453 ,  0.235119588492482573 ,  1.000000000000000000 ,  0.474506303019272324 ,  0.573213995147633382 ,  1.000000000000000000 ,  0.208056866002478996 ,  0.857877397361252236 ,  1.000000000000000000 ,  0.055191471341329644 ,  0.982636717483955313  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.017470292409305530 ,  0.419181388593219351 ,  1.000000000000000000 ,  0.392921185753856816 ,  0.901898311049984525  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.489843912473506948 ,  1.000000000000000000 ,  0.727643106167812137 ,  0.534880651358580539 ,  1.000000000000000000 ,  0.243391864978409644 ,  0.940318893026773739  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.827949765766600843 ,  0.265877477329979295 ,  1.000000000000000000 ,  0.502562634774999073 ,  0.656748971905667278 ,  1.000000000000000000 ,  0.154071084197211439 ,  0.957075157400286480  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.415164611869221378 ,  1.000000000000000000 ,  0.652741519625042677 ,  0.395030315908183716 ,  1.000000000000000000 ,  0.338799229099176480 ,  0.765646608773204229 ,  1.000000000000000000 ,  0.097310034763059397 ,  0.974093469525820765  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.748497031926481093 ,  0.215961554572230097 ,  1.000000000000000000 ,  0.480382072863731913 ,  0.544571579369882985 ,  1.000000000000000000 ,  0.223339662254152721 ,  0.842496993203372235 ,  1.000000000000000000 ,  0.061517118586712889 ,  0.981657601435189431  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.010191266612837200 ,  0.411259655754435904 ,  1.000000000000000000 ,  0.397244874550595650 ,  0.897377943534971023  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.481039760534313687 ,  1.000000000000000000 ,  0.727079514858359532 ,  0.522565529125694317 ,  1.000000000000000000 ,  0.249627628148404918 ,  0.935978278732715996  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.811039187334090195 ,  0.253327076133119056 ,  1.000000000000000000 ,  0.509243363529077508 ,  0.640913996592932134 ,  1.000000000000000000 ,  0.160973817195968011 ,  0.952829344884058127  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.401249366113430539 ,  1.000000000000000000 ,  0.645124951544274849 ,  0.374941874512281836 ,  1.000000000000000000 ,  0.350097164681899664 ,  0.748183905868308630 ,  1.000000000000000000 ,  0.103836126687872343 ,  0.969564455779541090  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.724430371618267621 ,  0.200668817079239448 ,  1.000000000000000000 ,  0.484273647121878925 ,  0.520243491122044821 ,  1.000000000000000000 ,  0.236529367721235728 ,  0.828428772938633151 ,  1.000000000000000000 ,  0.067260081078368575 ,  0.980404279387087518  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.005615682130324600 ,  0.406137540594211421 ,  1.000000000000000000 ,  0.400771817864300495 ,  0.895471094640323484  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.474168432930132389 ,  1.000000000000000000 ,  0.727373649186765725 ,  0.513392775413283653 ,  1.000000000000000000 ,  0.255426162877240126 ,  0.934670727341567642  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.798181565250114300 ,  0.243967614648104208 ,  1.000000000000000000 ,  0.515144254694563597 ,  0.629424517032345032 ,  1.000000000000000000 ,  0.167014128021617025 ,  0.951649286003916850  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.390229935400625560 ,  1.000000000000000000 ,  0.639103179784231923 ,  0.359343038180353069 ,  1.000000000000000000 ,  0.360108563482364363 ,  0.735139168617116923 ,  1.000000000000000000 ,  0.109779524405751158 ,  0.968362862935633340  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.703750654350386640 ,  0.188114984448036593 ,  1.000000000000000000 ,  0.486812394011662375 ,  0.499226881530557998 ,  1.000000000000000000 ,  0.248053434646318405 ,  0.815467803252149292 ,  1.000000000000000000 ,  0.072508346656431036 ,  0.978927195739316636  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.003490609018587780 ,  0.403416513074019711 ,  1.000000000000000000 ,  0.403828692200383921 ,  0.896231845700471541  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.468773219691838772 ,  1.000000000000000000 ,  0.727202684424372414 ,  0.506003433411324810 ,  1.000000000000000000 ,  0.259824933384078860 ,  0.932913564450832844  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.787546654971226445 ,  0.236439489210044373 ,  1.000000000000000000 ,  0.519614906002979660 ,  0.619684021448387967 ,  1.000000000000000000 ,  0.171968354979902593 ,  0.950069177997489800  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.380844439094186971 ,  1.000000000000000000 ,  0.633478162891895069 ,  0.346077161119629773 ,  1.000000000000000000 ,  0.368577868176914347 ,  0.723398229793523972 ,  1.000000000000000000 ,  0.115062039117177425 ,  0.966890459336296026  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.686157633984649906 ,  0.177848140134964638 ,  1.000000000000000000 ,  0.488334616303571001 ,  0.481274849109315950 ,  1.000000000000000000 ,  0.257926981067669092 ,  0.803703273931118956 ,  1.000000000000000000 ,  0.077190798309808587 ,  0.977207066984367323  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.003490609018587780 ,  0.403416513074019711 ,  1.000000000000000000 ,  0.403828692200383921 ,  0.896231845700471541  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464510198777513372 ,  1.000000000000000000 ,  0.726712324720289349 ,  0.499999897037084695 ,  1.000000000000000000 ,  0.263097764952188673 ,  0.930804973128608215  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.778297455678399697 ,  0.230042932442954512 ,  1.000000000000000000 ,  0.523201311338310826 ,  0.611047576998192454 ,  1.000000000000000000 ,  0.176240224073667712 ,  0.948244069075837470  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.373099128327226914 ,  1.000000000000000000 ,  0.628441196739721652 ,  0.335136523722868285 ,  1.000000000000000000 ,  0.375415631021390916 ,  0.713146324821870903 ,  1.000000000000000000 ,  0.119532165078448757 ,  0.965098613070002553  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.670208716859506226 ,  0.168855593341046828 ,  1.000000000000000000 ,  0.489239208723270647 ,  0.464983264299817178 ,  1.000000000000000000 ,  0.266959949256009299 ,  0.792520982361077486 ,  1.000000000000000000 ,  0.081631604135697478 ,  0.975384363173798508  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.003490609018587780 ,  0.403416513074019711 ,  1.000000000000000000 ,  0.403828692200383921 ,  0.896231845700471541  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.460767943841719374 ,  1.000000000000000000 ,  0.726073890751772533 ,  0.494640280481929318 ,  1.000000000000000000 ,  0.265861236473118268 ,  0.928544313518570585  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.770670139715113112 ,  0.224882423732640724 ,  1.000000000000000000 ,  0.525804007139443197 ,  0.603730067713673324 ,  1.000000000000000000 ,  0.179637413604576107 ,  0.946113213641719741  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.366323467617908460 ,  1.000000000000000000 ,  0.623752485791845057 ,  0.325583542395642023 ,  1.000000000000000000 ,  0.381286408799703436 ,  0.703783880833357744 ,  1.000000000000000000 ,  0.123530595745368579 ,  0.963126547013069212  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.657016035076583238 ,  0.161644010085199102 ,  1.000000000000000000 ,  0.489536887186339698 ,  0.451459136985007004 ,  1.000000000000000000 ,  0.274345182687261879 ,  0.782668767661495846 ,  1.000000000000000000 ,  0.085392025282704023 ,  0.973286559700354048  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.003490609018587780 ,  0.403416513074019711 ,  1.000000000000000000 ,  0.403828692200383921 ,  0.896231845700471541  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.458424952892647342 ,  1.000000000000000000 ,  0.726560498652541908 ,  0.491743302720909270 ,  1.000000000000000000 ,  0.268476658758445730 ,  0.929353565327523667  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.766745583418126264 ,  0.222181085605166001 ,  1.000000000000000000 ,  0.528023918403012660 ,  0.600492917580022656 ,  1.000000000000000000 ,  0.182018372823785329 ,  0.946947095708409736  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.361420650847491587 ,  1.000000000000000000 ,  0.620787723759002930 ,  0.318902687977372479 ,  1.000000000000000000 ,  0.386622361051538466 ,  0.698241044108790621 ,  1.000000000000000000 ,  0.127053685379988318 ,  0.964464149941220539  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.645086325826133233 ,  0.155292140824802916 ,  1.000000000000000000 ,  0.489502173572796018 ,  0.439226694006628948 ,  1.000000000000000000 ,  0.281003976152689849 ,  0.773391397931162294 ,  1.000000000000000000 ,  0.088884709008969692 ,  0.971085932620782022  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.003490609018587780 ,  0.403416513074019711 ,  1.000000000000000000 ,  0.403828692200383921 ,  0.896231845700471541  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455913789927448521 ,  1.000000000000000000 ,  0.725629903001629217 ,  0.487908258745208601 ,  1.000000000000000000 ,  0.269932829437094290 ,  0.926694129973343217  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.761095040589309435 ,  0.218475358819391008 ,  1.000000000000000000 ,  0.529457133486230136 ,  0.594796973911004501 ,  1.000000000000000000 ,  0.184304066466191213 ,  0.944430112398584320  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.356494595283427196 ,  1.000000000000000000 ,  0.616970242459882079 ,  0.311945924668869223 ,  1.000000000000000000 ,  0.390564551420258699 ,  0.690754688242695591 ,  1.000000000000000000 ,  0.129966750412405269 ,  0.962075837381178411  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.634765418114758617 ,  0.149927317142557692 ,  1.000000000000000000 ,  0.489181301008787039 ,  0.428623230495571328 ,  1.000000000000000000 ,  0.286664386723425146 ,  0.764958087290726785 ,  1.000000000000000000 ,  0.091942422340604929 ,  0.968719111948324718  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_16Denominator[13][7][24] = {
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.065314561203143830 ,  0.498529986854496121 ,  1.000000000000000000 ,  0.326797852400430355 ,  0.926068165914249564  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.576926987229084975 ,  1.000000000000000000 ,  0.692639100616165071 ,  0.656664880569203913 ,  1.000000000000000000 ,  0.173462767707589699 ,  0.964966859832053769  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.952939989913339391 ,  0.386894459678110092 ,  1.000000000000000000 ,  0.412705538858687659 ,  0.780038604199553509 ,  1.000000000000000000 ,  0.095609453910619813 ,  0.975650906610855628  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.529054855604213170 ,  1.000000000000000000 ,  0.664989713794856718 ,  0.566419930796568605 ,  1.000000000000000000 ,  0.232372494206240093 ,  0.877201306073777443 ,  1.000000000000000000 ,  0.050858288933544368 ,  0.988798411047131176  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.922415202110764554 ,  0.362201279021719669 ,  1.000000000000000000 ,  0.406593616712562500 ,  0.732027911410230936 ,  1.000000000000000000 ,  0.125983427985953272 ,  0.930632323457600830 ,  1.000000000000000000 ,  0.026716131478845502 ,  0.991134090976875548  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.042618012064232100 ,  0.466501910771246642 ,  1.000000000000000000 ,  0.346405121509639813 ,  0.919338791839717673  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.545066865691368307 ,  1.000000000000000000 ,  0.700921449026846832 ,  0.616718244355355161 ,  1.000000000000000000 ,  0.191581296666913786 ,  0.957679476368222371  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.911977558181541803 ,  0.346041326924174275 ,  1.000000000000000000 ,  0.435882219526916026 ,  0.745456341385752452 ,  1.000000000000000000 ,  0.109095289776557580 ,  0.970182991092106595  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.492693986514881777 ,  1.000000000000000000 ,  0.662424996616723938 ,  0.515640174928439166 ,  1.000000000000000000 ,  0.258278273163310090 ,  0.850013859917023162 ,  1.000000000000000000 ,  0.060822549222222203 ,  0.985545818957358488  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.872764964769253360 ,  0.316372650317771620 ,  1.000000000000000000 ,  0.427469625586002600 ,  0.685123038532598483 ,  1.000000000000000000 ,  0.147159032779463850 ,  0.912942793539941677 ,  1.000000000000000000 ,  0.033409928215568846 ,  0.990308056496348454  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.024513387914439240 ,  0.443522750783789332 ,  1.000000000000000000 ,  0.359656677734344843 ,  0.910945579648050452  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.522738669857404914 ,  1.000000000000000000 ,  0.705414559929268048 ,  0.587858596205502404 ,  1.000000000000000000 ,  0.205670287198723295 ,  0.952342199315652671  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.878586144526497082 ,  0.315301727613204408 ,  1.000000000000000000 ,  0.454907106306446041 ,  0.717282696620334725 ,  1.000000000000000000 ,  0.121533565122803777 ,  0.967786270093417556  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.463827563497045070 ,  1.000000000000000000 ,  0.656921477202447202 ,  0.474375247859111504 ,  1.000000000000000000 ,  0.280234184904267269 ,  0.824795065265253924 ,  1.000000000000000000 ,  0.070142645852725921 ,  0.981958069482973372  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.829026786438386676 ,  0.279955056888353981 ,  1.000000000000000000 ,  0.443227014887030035 ,  0.642441411967629605 ,  1.000000000000000000 ,  0.167311893027723674 ,  0.895037459246907918 ,  1.000000000000000000 ,  0.040301207009304250 ,  0.989491166550020562  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.010789875929341000 ,  0.426832394593668174 ,  1.000000000000000000 ,  0.369921890274914178 ,  0.904773008201806950  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.503220679720745578 ,  1.000000000000000000 ,  0.707973985781621851 ,  0.561881290584222315 ,  1.000000000000000000 ,  0.218761375643692080 ,  0.946658005543369319  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.849586959347968018 ,  0.290734970583360230 ,  1.000000000000000000 ,  0.468413538004115670 ,  0.690629364163798720 ,  1.000000000000000000 ,  0.131948121272580793 ,  0.961095061535725659  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.440082171589672222 ,  1.000000000000000000 ,  0.649906616604698173 ,  0.440022729757746145 ,  1.000000000000000000 ,  0.299085017595187963 ,  0.801357632857948143 ,  1.000000000000000000 ,  0.078877475727341190 ,  0.978099047823400625  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.790251492139170564 ,  0.250539846966480906 ,  1.000000000000000000 ,  0.453669032997606747 ,  0.602889680265333072 ,  1.000000000000000000 ,  0.185199000207878744 ,  0.874429141850823433 ,  1.000000000000000000 ,  0.046903460517675993 ,  0.984745684787775466  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.000821206923907520 ,  0.414937453990038563 ,  1.000000000000000000 ,  0.377993624733214573 ,  0.900968675559838950  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.490211845507042165 ,  1.000000000000000000 ,  0.709257654092964818 ,  0.544407014430344005 ,  1.000000000000000000 ,  0.228165831565431132 ,  0.943075004059812372  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.826209509588016200 ,  0.271881999395464813 ,  1.000000000000000000 ,  0.479727196373166465 ,  0.669490016830597834 ,  1.000000000000000000 ,  0.141339669277836671 ,  0.957520032521682496  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.421296307171885931 ,  1.000000000000000000 ,  0.643503419873142302 ,  0.413090194469989014 ,  1.000000000000000000 ,  0.315580092965279446 ,  0.782884588609319976 ,  1.000000000000000000 ,  0.087019065319247219 ,  0.977616575421004996  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.758057127804730335 ,  0.227723460338869477 ,  1.000000000000000000 ,  0.461799176307194581 ,  0.570253957102896614 ,  1.000000000000000000 ,  0.201742517088838724 ,  0.857958079927698014 ,  1.000000000000000000 ,  0.053364008712092168 ,  0.983659483240084676  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.994124683274808074 ,  0.406846586078696115 ,  1.000000000000000000 ,  0.384482139123846045 ,  0.899643639273399631  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.478252810808618756 ,  1.000000000000000000 ,  0.709866393288831277 ,  0.528102188566051178 ,  1.000000000000000000 ,  0.237129407292996974 ,  0.939348991882678397  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.808131164579070749 ,  0.257814821316543374 ,  1.000000000000000000 ,  0.489213877327237978 ,  0.653649858582312504 ,  1.000000000000000000 ,  0.149574127173881771 ,  0.956953193401654301  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.404732583736211515 ,  1.000000000000000000 ,  0.635701771235004154 ,  0.388912346484578175 ,  1.000000000000000000 ,  0.329155309687925224 ,  0.763115830465076694 ,  1.000000000000000000 ,  0.094340516263889562 ,  0.973167484630539037  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.730606853085612018 ,  0.209442333746097109 ,  1.000000000000000000 ,  0.467432843813895438 ,  0.542108973204186717 ,  1.000000000000000000 ,  0.216357566157081682 ,  0.842584741285562711 ,  1.000000000000000000 ,  0.059401019328864220 ,  0.982369235187856682  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.987494471473414848 ,  0.399806751608835331 ,  1.000000000000000000 ,  0.387919030407238186 ,  0.894762595507557434  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.469621941206577520 ,  1.000000000000000000 ,  0.709279283508574276 ,  0.515838655589904382 ,  1.000000000000000000 ,  0.243209106722393403 ,  0.934762985665283708  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.791466384208135199 ,  0.245525668293539945 ,  1.000000000000000000 ,  0.495786508062444287 ,  0.637654161355086346 ,  1.000000000000000000 ,  0.156352905173205603 ,  0.952447474554627971  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.391799542505157694 ,  1.000000000000000000 ,  0.629401729334598037 ,  0.370347951690006449 ,  1.000000000000000000 ,  0.340876184200175958 ,  0.748251162303483541 ,  1.000000000000000000 ,  0.100925452500790464 ,  0.972035182793999031  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.706789455022410196 ,  0.194404481881145674 ,  1.000000000000000000 ,  0.471312126162177436 ,  0.517510596999930850 ,  1.000000000000000000 ,  0.229383108073629521 ,  0.828189354643639519 ,  1.000000000000000000 ,  0.065058701811699676 ,  0.980905772098678019  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.982982134888031767 ,  0.394787238207063351 ,  1.000000000000000000 ,  0.391359533958633987 ,  0.892732341882048530  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.462882944293618648 ,  1.000000000000000000 ,  0.709540036430755583 ,  0.506698096962689748 ,  1.000000000000000000 ,  0.248866312724233707 ,  0.933253430847174714  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.778556041891665607 ,  0.236189981706653174 ,  1.000000000000000000 ,  0.501724861165940394 ,  0.625839613409075790 ,  1.000000000000000000 ,  0.162415355042802551 ,  0.951102229154261236  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.380963816557013113 ,  1.000000000000000000 ,  0.623472774743534686 ,  0.354808668568466823 ,  1.000000000000000000 ,  0.350695562381672932 ,  0.734987055719767191 ,  1.000000000000000000 ,  0.106752791676562542 ,  0.970604132224676341  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.685073278646529560 ,  0.181408357014591681 ,  1.000000000000000000 ,  0.472934917782897146 ,  0.494437840221483904 ,  1.000000000000000000 ,  0.240280580900258806 ,  0.811870785417737939 ,  1.000000000000000000 ,  0.070086682758230740 ,  0.975520960828436090  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.980879750811695095 ,  0.392113239574593930 ,  1.000000000000000000 ,  0.394354464915191272 ,  0.893416584034917083  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.457587477858004799 ,  1.000000000000000000 ,  0.709352647824506022 ,  0.499333127420907019 ,  1.000000000000000000 ,  0.253160898115103394 ,  0.931338985781534956  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.767901411330740635 ,  0.228702591847420672 ,  1.000000000000000000 ,  0.506216310974317185 ,  0.615851587692168856 ,  1.000000000000000000 ,  0.167382816092161324 ,  0.949381823314327966  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.371937002115451276 ,  1.000000000000000000 ,  0.618042639842703245 ,  0.341878903655810640 ,  1.000000000000000000 ,  0.358787909849850983 ,  0.723279264265577648 ,  1.000000000000000000 ,  0.111801743214346355 ,  0.968879258652423081  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.669041149664080348 ,  0.172058018433053489 ,  1.000000000000000000 ,  0.475342801513819468 ,  0.478256906150360206 ,  1.000000000000000000 ,  0.250448690441089583 ,  0.802978785396930794 ,  1.000000000000000000 ,  0.074818551037286490 ,  0.977337311679835397  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.980879750811695095 ,  0.392113239574593930 ,  1.000000000000000000 ,  0.394354464915191272 ,  0.893416584034917083  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.453399559777543593 ,  1.000000000000000000 ,  0.708857841850453974 ,  0.493347630480741040 ,  1.000000000000000000 ,  0.256359499267054647 ,  0.929108768306178501  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.759024380924572695 ,  0.222614859846587604 ,  1.000000000000000000 ,  0.509601307189939012 ,  0.607329479434322916 ,  1.000000000000000000 ,  0.171442405648518675 ,  0.947359982860708216  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.364136913213939317 ,  1.000000000000000000 ,  0.612987043316819191 ,  0.330729281059202185 ,  1.000000000000000000 ,  0.365689295605959364 ,  0.712669196929962445 ,  1.000000000000000000 ,  0.116304660869200743 ,  0.966954195031004771  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.653715162696778540 ,  0.163482272643526300 ,  1.000000000000000000 ,  0.476196400934680020 ,  0.462261548770710295 ,  1.000000000000000000 ,  0.259080932693443622 ,  0.791823437401849128 ,  1.000000000000000000 ,  0.079057737040192255 ,  0.975302039384921993  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.980879750811695095 ,  0.392113239574593930 ,  1.000000000000000000 ,  0.394354464915191272 ,  0.893416584034917083  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.450513968691529876 ,  1.000000000000000000 ,  0.709472031104437684 ,  0.489726995651814989 ,  1.000000000000000000 ,  0.259521976754184092 ,  0.930021032820080751  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.751370098781628970 ,  0.217473388695913372 ,  1.000000000000000000 ,  0.512237369604506720 ,  0.599828525937440404 ,  1.000000000000000000 ,  0.174864381929386248 ,  0.945133223793557242  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.357630890604753837 ,  1.000000000000000000 ,  0.608465612745358375 ,  0.321436061238431992 ,  1.000000000000000000 ,  0.371278126795181629 ,  0.703351159053062114 ,  1.000000000000000000 ,  0.120115804332258866 ,  0.964783674714244421  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.640380559818587325 ,  0.156247009037961598 ,  1.000000000000000000 ,  0.476533071598669922 ,  0.448322711688692421 ,  1.000000000000000000 ,  0.266568192267041315 ,  0.781604995440324912 ,  1.000000000000000000 ,  0.082861477458401917 ,  0.973104369368135047  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.980879750811695095 ,  0.392113239574593930 ,  1.000000000000000000 ,  0.394354464915191272 ,  0.893416584034917083  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.447409846239710918 ,  1.000000000000000000 ,  0.708689816717972265 ,  0.485098475327020384 ,  1.000000000000000000 ,  0.261628802041376063 ,  0.927478323636574342  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.747670427196857501 ,  0.214947249761501286 ,  1.000000000000000000 ,  0.514317926514037738 ,  0.596693153691124412 ,  1.000000000000000000 ,  0.177098767049335526 ,  0.945860414984266318  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.352015708998642152 ,  1.000000000000000000 ,  0.604335409430426407 ,  0.313424003890694192 ,  1.000000000000000000 ,  0.375954293310770404 ,  0.694951729775276639 ,  1.000000000000000000 ,  0.123435068271076934 ,  0.962444785796355284  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.628663282753381347 ,  0.150059662353935835 ,  1.000000000000000000 ,  0.476500451618313392 ,  0.436061921538173969 ,  1.000000000000000000 ,  0.273097489450107256 ,  0.772196399589735849 ,  1.000000000000000000 ,  0.086283253276346048 ,  0.970769138471424986  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.980879750811695095 ,  0.392113239574593930 ,  1.000000000000000000 ,  0.394354464915191272 ,  0.893416584034917083  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.445719537392714304 ,  1.000000000000000000 ,  0.709020260359480847 ,  0.482968655533581814 ,  1.000000000000000000 ,  0.263524783063829537 ,  0.928019591040479930  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.741460832262635705 ,  0.210892612416065245 ,  1.000000000000000000 ,  0.516085225378973056 ,  0.590406272952891165 ,  1.000000000000000000 ,  0.179742114684399268 ,  0.943383302999936779  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.347837880819774903 ,  1.000000000000000000 ,  0.601713253028064332 ,  0.307684634288471093 ,  1.000000000000000000 ,  0.380512812514327392 ,  0.689973089191943889 ,  1.000000000000000000 ,  0.126515972907498775 ,  0.963526665225994838  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.619699023201719101 ,  0.145384746385836566 ,  1.000000000000000000 ,  0.477084936245887270 ,  0.427049078009460792 ,  1.000000000000000000 ,  0.279170755125940639 ,  0.766537804879544704 ,  1.000000000000000000 ,  0.089446111435423734 ,  0.971940751104879208  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_18Denominator[13][7][24] = {
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.043409859023320060 ,  0.486089211020819201 ,  1.000000000000000000 ,  0.319776949485572448 ,  0.924928186665303076  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564632593791683846 ,  1.000000000000000000 ,  0.676569824505432238 ,  0.649684092316948125 ,  1.000000000000000000 ,  0.168793903354478747 ,  0.964223025806542733  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.931815758257071525 ,  0.376331638278568414 ,  1.000000000000000000 ,  0.401671706941365780 ,  0.776537961248720654 ,  1.000000000000000000 ,  0.092567366146780059 ,  0.974833980791605104  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.517622272320534149 ,  1.000000000000000000 ,  0.648257029926500805 ,  0.559459434504745889 ,  1.000000000000000000 ,  0.224727211519618719 ,  0.874095578851965760 ,  1.000000000000000000 ,  0.048850654648900468 ,  0.986240109236283691  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.904299448845878429 ,  0.354356090533907209 ,  1.000000000000000000 ,  0.395634246117677668 ,  0.731837505495228857 ,  1.000000000000000000 ,  0.121372792673905452 ,  0.932859922107137418 ,  1.000000000000000000 ,  0.025542269019585090 ,  0.993531513304615421  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.022081174839774450 ,  0.456358266251381484 ,  1.000000000000000000 ,  0.337452396150047684 ,  0.917223778656838240  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.533228777599840997 ,  1.000000000000000000 ,  0.684674327869023158 ,  0.609679382162835792 ,  1.000000000000000000 ,  0.186607025407361904 ,  0.956351123295269945  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.892945138236026725 ,  0.337369456444881199 ,  1.000000000000000000 ,  0.425412027410062954 ,  0.744256924665722908 ,  1.000000000000000000 ,  0.106079774341204400 ,  0.972580294892975705  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.483254192532204085 ,  1.000000000000000000 ,  0.647115558272107649 ,  0.511596688655460929 ,  1.000000000000000000 ,  0.250203871784011267 ,  0.850202369914360823 ,  1.000000000000000000 ,  0.058522941711277185 ,  0.986337712479519513  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.852813434902815470 ,  0.307250902152699290 ,  1.000000000000000000 ,  0.415922102842985142 ,  0.681098052116029806 ,  1.000000000000000000 ,  0.142274976567722922 ,  0.911034109501467460 ,  1.000000000000000000 ,  0.032139311554670758 ,  0.988672904961098609  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.003234478545208090 ,  0.432285203508474658 ,  1.000000000000000000 ,  0.351778168936861979 ,  0.908721755714818502  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.511258649443607882 ,  1.000000000000000000 ,  0.689041444309367534 ,  0.580844047452429679 ,  1.000000000000000000 ,  0.200428419590172519 ,  0.950564504314421965  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.858892904773904009 ,  0.306476305644447888 ,  1.000000000000000000 ,  0.442973451551562869 ,  0.713428971919287669 ,  1.000000000000000000 ,  0.117868742700156473 ,  0.966066677367889315  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.454365347490653992 ,  1.000000000000000000 ,  0.641753614544099848 ,  0.469821776665095780 ,  1.000000000000000000 ,  0.272125447994199243 ,  0.824355316314714037 ,  1.000000000000000000 ,  0.067776774036672774 ,  0.982459939508447344  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.809369542490762539 ,  0.271303745306077593 ,  1.000000000000000000 ,  0.431628495685752123 ,  0.637858029824721728 ,  1.000000000000000000 ,  0.162252135057250585 ,  0.892687853853679436 ,  1.000000000000000000 ,  0.038949499910300653 ,  0.987638987733268747  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.991398844697886639 ,  0.417362765289999604 ,  1.000000000000000000 ,  0.362383860786476109 ,  0.905266229878154416  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.494225964039939736 ,  1.000000000000000000 ,  0.692305392535660258 ,  0.558472446838600312 ,  1.000000000000000000 ,  0.212526474178993979 ,  0.947548692925941016  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.831787396138930824 ,  0.283389121661803245 ,  1.000000000000000000 ,  0.457175992866854952 ,  0.689067194912035941 ,  1.000000000000000000 ,  0.128370317941452788 ,  0.962620745560178959  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.430743626807533586 ,  1.000000000000000000 ,  0.634851654399506193 ,  0.435251969023754293 ,  1.000000000000000000 ,  0.290850281368173536 ,  0.800436586551891316 ,  1.000000000000000000 ,  0.076419516194627515 ,  0.978319997313297107  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.773738672023675300 ,  0.244271551639968526 ,  1.000000000000000000 ,  0.442445160138814852 ,  0.601529379462263369 ,  1.000000000000000000 ,  0.179622648307873251 ,  0.875591394499857234 ,  1.000000000000000000 ,  0.045304827183304849 ,  0.986369484678802255  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.979906652969693703 ,  0.404308143351751859 ,  1.000000000000000000 ,  0.369638834530026827 ,  0.898155999574639097  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.479290862668277917 ,  1.000000000000000000 ,  0.692732231167847212 ,  0.537514881218015494 ,  1.000000000000000000 ,  0.222457659477004160 ,  0.940587627959159955  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.808677011529107981 ,  0.264831745925689421 ,  1.000000000000000000 ,  0.468343863716935260 ,  0.667652621826798853 ,  1.000000000000000000 ,  0.137623508660334154 ,  0.958730944992503775  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.412121461327101979 ,  1.000000000000000000 ,  0.628533038741057148 ,  0.408243298104964680 ,  1.000000000000000000 ,  0.307166889927768361 ,  0.781597266321273021 ,  1.000000000000000000 ,  0.084452355091568288 ,  0.977582098729833260  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.740310036220448442 ,  0.220832864055480277 ,  1.000000000000000000 ,  0.449716261316282839 ,  0.566234587698097647 ,  1.000000000000000000 ,  0.195659510888913396 ,  0.855422865662354370 ,  1.000000000000000000 ,  0.051595995543459318 ,  0.981309659658700983  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.973306086240779855 ,  0.396379016068680590 ,  1.000000000000000000 ,  0.375975175649877313 ,  0.896650071622315692  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.467541599826148457 ,  1.000000000000000000 ,  0.693298357876554472 ,  0.521273068887416424 ,  1.000000000000000000 ,  0.231228098754152067 ,  0.936584449793814167  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.789039629191860437 ,  0.249830776333501037 ,  1.000000000000000000 ,  0.477004751349679290 ,  0.648940169080110429 ,  1.000000000000000000 ,  0.145625120024687110 ,  0.954429888104872060  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.396083843771943334 ,  1.000000000000000000 ,  0.620975483273484952 ,  0.384554918524026679 ,  1.000000000000000000 ,  0.320237758600534828 ,  0.761867522200976199 ,  1.000000000000000000 ,  0.091488976888551968 ,  0.972826726728649338  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.713912484021419602 ,  0.203365291611358523 ,  1.000000000000000000 ,  0.455140846142086664 ,  0.538649952834992507 ,  1.000000000000000000 ,  0.209649914960038042 ,  0.840146681989056954 ,  1.000000000000000000 ,  0.057360001951341028 ,  0.979763890389417025  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.967933141552431264 ,  0.390193494962982446 ,  1.000000000000000000 ,  0.380609348093101485 ,  0.894769658013293223  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.459880907228256586 ,  1.000000000000000000 ,  0.693939406741136522 ,  0.510884385527197504 ,  1.000000000000000000 ,  0.237600706039122506 ,  0.935135430062165263  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.774108169993100170 ,  0.238738439896035520 ,  1.000000000000000000 ,  0.484322382866159307 ,  0.635230001323662030 ,  1.000000000000000000 ,  0.152540441117548609 ,  0.953209386460480368  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.383264720672170989 ,  1.000000000000000000 ,  0.614757665563806510 ,  0.365950241947874344 ,  1.000000000000000000 ,  0.331831859182155353 ,  0.746739947078320920 ,  1.000000000000000000 ,  0.097984375278257166 ,  0.971490680359318293  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.690295916367250739 ,  0.188546129264540535 ,  1.000000000000000000 ,  0.459030872342106044 ,  0.513818648164702463 ,  1.000000000000000000 ,  0.222540204952555942 ,  0.825486411421120669 ,  1.000000000000000000 ,  0.062942276535047040 ,  0.978131246181289882  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.963529063150031617 ,  0.385331549086701863 ,  1.000000000000000000 ,  0.383896962039674272 ,  0.892604195700684611  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.453242040901412568 ,  1.000000000000000000 ,  0.694176360607662413 ,  0.501740683475384919 ,  1.000000000000000000 ,  0.243147929045872735 ,  0.933447496304089186  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.761648977439265518 ,  0.229783125598174903 ,  1.000000000000000000 ,  0.489995698072120178 ,  0.623527255652242762 ,  1.000000000000000000 ,  0.158348999518108796 ,  0.951622988972296024  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.372317670458662653 ,  1.000000000000000000 ,  0.608797624242164881 ,  0.350088600586122678 ,  1.000000000000000000 ,  0.341764766744514281 ,  0.733020509607363202 ,  1.000000000000000000 ,  0.103864236092346504 ,  0.969924605392542549  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.670108565720909821 ,  0.176460241516887689 ,  1.000000000000000000 ,  0.461552967185013208 ,  0.492488226781937655 ,  1.000000000000000000 ,  0.233750313516645075 ,  0.812047519987874278 ,  1.000000000000000000 ,  0.068025869400290068 ,  0.976303553433062632  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.959884446182828532 ,  0.381477066380283725 ,  1.000000000000000000 ,  0.386127508980957046 ,  0.890222954084705198  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.448021784219250041 ,  1.000000000000000000 ,  0.693978050927684831 ,  0.494371973694382461 ,  1.000000000000000000 ,  0.247362212521985259 ,  0.931392404980859956  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.751142709247327955 ,  0.222440236620810194 ,  1.000000000000000000 ,  0.494410353106913281 ,  0.613445502003982179 ,  1.000000000000000000 ,  0.163230041986364482 ,  0.949740618139648274  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.363431902630021675 ,  1.000000000000000000 ,  0.603453401461314098 ,  0.337225336635364914 ,  1.000000000000000000 ,  0.349715049931875521 ,  0.721177185582213665 ,  1.000000000000000000 ,  0.108820382367761842 ,  0.968034921816661065  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.652576596785180652 ,  0.166382344053350228 ,  1.000000000000000000 ,  0.463118762880354085 ,  0.473910607776350079 ,  1.000000000000000000 ,  0.243592741311545041 ,  0.799652202373341336 ,  1.000000000000000000 ,  0.072677584744687704 ,  0.974313979404066499  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.959884446182828532 ,  0.381477066380283725 ,  1.000000000000000000 ,  0.386127508980957046 ,  0.890222954084705198  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.443427651454724958 ,  1.000000000000000000 ,  0.693580256729125932 ,  0.487793287313452228 ,  1.000000000000000000 ,  0.251005819292424936 ,  0.929186233674353157  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.742209982084488407 ,  0.216346564551582576 ,  1.000000000000000000 ,  0.497840119085596111 ,  0.604693143185684678 ,  1.000000000000000000 ,  0.167325262276827452 ,  0.947615777062142617  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.355758592969304899 ,  1.000000000000000000 ,  0.598480337536911877 ,  0.326142209023041030 ,  1.000000000000000000 ,  0.356490779107245803 ,  0.710455688919235939 ,  1.000000000000000000 ,  0.113238245391976786 ,  0.965962221520038744  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.637533714157776155 ,  0.158037295250189946 ,  1.000000000000000000 ,  0.463956222839272459 ,  0.457934728277860192 ,  1.000000000000000000 ,  0.252054772124400583 ,  0.788388455271618160 ,  1.000000000000000000 ,  0.076830536710722874 ,  0.972145791271153636  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.959884446182828532 ,  0.381477066380283725 ,  1.000000000000000000 ,  0.386127508980957046 ,  0.890222954084705198  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.440634831174951336 ,  1.000000000000000000 ,  0.694159525673729849 ,  0.484230981158004503 ,  1.000000000000000000 ,  0.254058572041359343 ,  0.930002976631086553  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.734683178562253403 ,  0.211321753927139699 ,  1.000000000000000000 ,  0.500419052326641634 ,  0.597146941474837822 ,  1.000000000000000000 ,  0.170678619869322895 ,  0.945269025670791629  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.349361123135357110 ,  1.000000000000000000 ,  0.594034658197899135 ,  0.316910110926725497 ,  1.000000000000000000 ,  0.361975739260407714 ,  0.701049134133390761 ,  1.000000000000000000 ,  0.116976275148743161 ,  0.963665820256376704  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.625638434897694773 ,  0.151575979098080987 ,  1.000000000000000000 ,  0.465164623369136221 ,  0.445710256056837173 ,  1.000000000000000000 ,  0.259879887674599408 ,  0.781033354194225815 ,  1.000000000000000000 ,  0.080707014993541840 ,  0.973506784767872557  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.959884446182828532 ,  0.381477066380283725 ,  1.000000000000000000 ,  0.386127508980957046 ,  0.890222954084705198  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.437617999494572696 ,  1.000000000000000000 ,  0.693370994097403770 ,  0.479659730164761988 ,  1.000000000000000000 ,  0.256083242610156359 ,  0.927365705943015040  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.731187931966396198 ,  0.208949716170907990 ,  1.000000000000000000 ,  0.502375081451815308 ,  0.594109995456167939 ,  1.000000000000000000 ,  0.172780737887791580 ,  0.945906226533175132  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.343715946374449244 ,  1.000000000000000000 ,  0.589897418732555412 ,  0.308780172024797739 ,  1.000000000000000000 ,  0.366699874478604748 ,  0.692423982537044958 ,  1.000000000000000000 ,  0.120322162734938592 ,  0.961246490035794210  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.614402680935421941 ,  0.145684308247933991 ,  1.000000000000000000 ,  0.465109732038543389 ,  0.433716747101757005 ,  1.000000000000000000 ,  0.266099207439998431 ,  0.771689323797212001 ,  1.000000000000000000 ,  0.083966817212169087 ,  0.971017027786662656  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.959884446182828532 ,  0.381477066380283725 ,  1.000000000000000000 ,  0.386127508980957046 ,  0.890222954084705198  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.435981697080448427 ,  1.000000000000000000 ,  0.693682466345242554 ,  0.477564458756748356 ,  1.000000000000000000 ,  0.257912892846489383 ,  0.927849968405108916  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.725060948691595875 ,  0.204972288155722410 ,  1.000000000000000000 ,  0.504115949177511569 ,  0.587771480450246076 ,  1.000000000000000000 ,  0.175382775232448079 ,  0.943335995691678031  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.339731557341620249 ,  1.000000000000000000 ,  0.587392457743878382 ,  0.303250098935754775 ,  1.000000000000000000 ,  0.371034250181821623 ,  0.687532138345129828 ,  1.000000000000000000 ,  0.123251879431594533 ,  0.962205226284702286  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.604422818217887237 ,  0.140574098715286977 ,  1.000000000000000000 ,  0.464805032042504507 ,  0.423057830810614210 ,  1.000000000000000000 ,  0.271557258558141057 ,  0.763035069588238857 ,  1.000000000000000000 ,  0.086909711575183124 ,  0.968426717773743673  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double Elliptic_20Denominator[13][7][24] = {
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.022838412010578010 ,  0.474474009729460910 ,  1.000000000000000000 ,  0.312973174295437084 ,  0.922354933276847033  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.553091584320383522 ,  1.000000000000000000 ,  0.661051787663480384 ,  0.642146500051460678 ,  1.000000000000000000 ,  0.164187683960093533 ,  0.960926019624960603  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.913469927557958639 ,  0.367686007912449764 ,  1.000000000000000000 ,  0.391583029879021216 ,  0.774231789648666790 ,  1.000000000000000000 ,  0.089722889145091536 ,  0.974582473564810647  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.507469099558332970 ,  1.000000000000000000 ,  0.634562830667131816 ,  0.554384989443629905 ,  1.000000000000000000 ,  0.219001204185719667 ,  0.874317109267295112 ,  1.000000000000000000 ,  0.047399654440232038 ,  0.987924197698139772  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.884214188472244644 ,  0.344087619949142220 ,  1.000000000000000000 ,  0.386233072279612455 ,  0.727227441857806744 ,  1.000000000000000000 ,  0.118021982019384158 ,  0.931092610527004316 ,  1.000000000000000000 ,  0.024747711096230151 ,  0.992422752489903415  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  1.001812781576246090 ,  0.445335657753970215 ,  1.000000000000000000 ,  0.330252175551653537 ,  0.914158831486300616  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.525046465818474917 ,  1.000000000000000000 ,  0.669368398882835724 ,  0.606690717359513965 ,  1.000000000000000000 ,  0.180579324271869734 ,  0.955633735194913569  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.875562599748913151 ,  0.329786774020389750 ,  1.000000000000000000 ,  0.414680134396908484 ,  0.741979513891255937 ,  1.000000000000000000 ,  0.102808707306096564 ,  0.971859725738302815  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.473432812299042327 ,  1.000000000000000000 ,  0.632264824309187623 ,  0.505708781215442271 ,  1.000000000000000000 ,  0.243008184890224993 ,  0.847284106646526647 ,  1.000000000000000000 ,  0.056553092214204702 ,  0.983850819797585929  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.837045700314857033 ,  0.300867813107098314 ,  1.000000000000000000 ,  0.406180243197083346 ,  0.680772487357330203 ,  1.000000000000000000 ,  0.137975335283043954 ,  0.913115028886548719 ,  1.000000000000000000 ,  0.031002024377025608 ,  0.991061259281796669  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.985775781121852823 ,  0.424429914964258970 ,  1.000000000000000000 ,  0.343654260323723482 ,  0.908099754046210128  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.501320099502399974 ,  1.000000000000000000 ,  0.674559890349065827 ,  0.575432547771694614 ,  1.000000000000000000 ,  0.195678927688778870 ,  0.949885207426384492  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.841143002456026800 ,  0.298642775814875749 ,  1.000000000000000000 ,  0.432569070445846249 ,  0.710245103249398713 ,  1.000000000000000000 ,  0.114742437439499550 ,  0.965103660775561645  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.444718050362502171 ,  1.000000000000000000 ,  0.627016125995731644 ,  0.463784786175219510 ,  1.000000000000000000 ,  0.264771020678771563 ,  0.821013990946580452 ,  1.000000000000000000 ,  0.065706930782866171 ,  0.979727214818711056  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.793663980989815276 ,  0.265242054117199977 ,  1.000000000000000000 ,  0.420570330538109272 ,  0.635810015726464339 ,  1.000000000000000000 ,  0.156803317396650072 ,  0.891425021716369392 ,  1.000000000000000000 ,  0.037419059527307980 ,  0.985922506942408239  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.971577929691358921 ,  0.407120480326430945 ,  1.000000000000000000 ,  0.354615061811692944 ,  0.901526457942139925  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.484510156545290727 ,  1.000000000000000000 ,  0.677750571619102571 ,  0.553037604900792012 ,  1.000000000000000000 ,  0.207576087752254335 ,  0.946532273604796859  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.814368072857576952 ,  0.275950226226698458 ,  1.000000000000000000 ,  0.446582402907315379 ,  0.685651964438749317 ,  1.000000000000000000 ,  0.125089019372537025 ,  0.961359271361081791  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.422147380334505828 ,  1.000000000000000000 ,  0.621402099720575918 ,  0.430889220627293901 ,  1.000000000000000000 ,  0.283822472240662815 ,  0.799824522682627159 ,  1.000000000000000000 ,  0.074373175841623354 ,  0.979066034356951276  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.756811394798486137 ,  0.237386773985421462 ,  1.000000000000000000 ,  0.431889337000790741 ,  0.597609273573656563 ,  1.000000000000000000 ,  0.174764595856262944 ,  0.873402279998098274 ,  1.000000000000000000 ,  0.043964711459016391 ,  0.984572123201905680  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.961876523259761029 ,  0.395655551428042263 ,  1.000000000000000000 ,  0.362321487203353221 ,  0.897241245880715188  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.470630071757353607 ,  1.000000000000000000 ,  0.679375018953666920 ,  0.534017589063303544 ,  1.000000000000000000 ,  0.217728179767356061 ,  0.942667089290255666  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.791576094251555951 ,  0.257747290158438946 ,  1.000000000000000000 ,  0.457578599414262543 ,  0.664080277193072721 ,  1.000000000000000000 ,  0.134192198242322008 ,  0.957204627844331801  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.403376732727451559 ,  1.000000000000000000 ,  0.614125923829910048 ,  0.402937239195373109 ,  1.000000000000000000 ,  0.298970450909047392 ,  0.778111193252224664 ,  1.000000000000000000 ,  0.081940699008328496 ,  0.974324791329085671  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.726112522633452939 ,  0.215845580494393507 ,  1.000000000000000000 ,  0.439682115812137730 ,  0.565259302602227898 ,  1.000000000000000000 ,  0.190441318849149194 ,  0.856682777590323852 ,  1.000000000000000000 ,  0.050055900224610934 ,  0.983017858231493791  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.592381930125518763 ,  1.000000000000000000 ,  0.931759874903402729 ,  0.504851421860563199 ,  1.000000000000000000 ,  0.509167467182769284 ,  0.759027767525421337 ,  1.000000000000000000 ,  0.222734197508160564 ,  0.921553541891136807 ,  1.000000000000000000 ,  0.060653776601926245 ,  0.989186504968604052  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.103206162147165830 ,  0.388060310740633352 ,  1.000000000000000000 ,  0.705289905786164129 ,  0.625679451469780545 ,  1.000000000000000000 ,  0.346426921257656673 ,  0.837702411277215009 ,  1.000000000000000000 ,  0.145011377560471499 ,  0.950354773368109806 ,  1.000000000000000000 ,  0.038869405550168774 ,  0.994334510761841539  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.955347942405732842 ,  0.387841847755196756 ,  1.000000000000000000 ,  0.368539513280302822 ,  0.895571959807298201  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.459020941760428569 ,  1.000000000000000000 ,  0.679909891768258445 ,  0.517724416633042250 ,  1.000000000000000000 ,  0.226357886156362847 ,  0.938388237665534919  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.773979116381054655 ,  0.244198219593857541 ,  1.000000000000000000 ,  0.466761922396224993 ,  0.647926313487503624 ,  1.000000000000000000 ,  0.142153008869399800 ,  0.956155257465718944  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.387913915675911658 ,  1.000000000000000000 ,  0.607705912464734155 ,  0.380255615052195017 ,  1.000000000000000000 ,  0.312812172633594898 ,  0.760712684272268347 ,  1.000000000000000000 ,  0.089221208886168801 ,  0.973114219187219853  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.699881205282801622 ,  0.198546797526219065 ,  1.000000000000000000 ,  0.445108884456411225 ,  0.537323896477978180 ,  1.000000000000000000 ,  0.204317394711257538 ,  0.841081121741850568 ,  1.000000000000000000 ,  0.055758308797010547 ,  0.981299804537994147  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.564711031167718813 ,  1.000000000000000000 ,  0.912287200750314886 ,  0.468215873996224219 ,  1.000000000000000000 ,  0.525906978393562374 ,  0.729973434792511644 ,  1.000000000000000000 ,  0.241362203258234487 ,  0.909811453743740728 ,  1.000000000000000000 ,  0.067599699678685704 ,  0.988557810748681987  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.050516439871768570 ,  0.348388202300729477 ,  1.000000000000000000 ,  0.706561983381737591 ,  0.582766348028751824 ,  1.000000000000000000 ,  0.368972943989322622 ,  0.809540086198935249 ,  1.000000000000000000 ,  0.162393388516471615 ,  0.939421075503622416 ,  1.000000000000000000 ,  0.044818495673302636 ,  0.992752096103965864  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.950027375636802951 ,  0.381738098069445475 ,  1.000000000000000000 ,  0.373095522673643487 ,  0.893566747954414264  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.451450212992994870 ,  1.000000000000000000 ,  0.680525898725536482 ,  0.507301061642632889 ,  1.000000000000000000 ,  0.232628809357327798 ,  0.936756764601036296  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.757526170297942780 ,  0.232194758730515011 ,  1.000000000000000000 ,  0.473279404439350682 ,  0.631471678972926220 ,  1.000000000000000000 ,  0.148846052520128475 ,  0.951260476326377891  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.375282685020635332 ,  1.000000000000000000 ,  0.601574050288610218 ,  0.361724557826242521 ,  1.000000000000000000 ,  0.324217428979360123 ,  0.745401658883646845 ,  1.000000000000000000 ,  0.095607789698849297 ,  0.971583697706613347  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.676478087868785383 ,  0.183921563537176452 ,  1.000000000000000000 ,  0.448984605535636627 ,  0.512250219078866698 ,  1.000000000000000000 ,  0.217070441145408077 ,  0.826143044892582723 ,  1.000000000000000000 ,  0.061270714641342282 ,  0.979497792676226053  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.542148489579208248 ,  1.000000000000000000 ,  0.894509645971073852 ,  0.438494954255241654 ,  1.000000000000000000 ,  0.538749492129721008 ,  0.704476982286859177 ,  1.000000000000000000 ,  0.257451170135515883 ,  0.898498361655205913 ,  1.000000000000000000 ,  0.073838268416542646 ,  0.987226929897522587  },
/* 10 Poles*/  { 1.000000000000000000 ,  1.008888064255074820 ,  0.318973763508329344 ,  1.000000000000000000 ,  0.705050496125272597 ,  0.548562790321418636 ,  1.000000000000000000 ,  0.386377324534073974 ,  0.785043389344319942 ,  1.000000000000000000 ,  0.177024533945971424 ,  0.928875725872407854 ,  1.000000000000000000 ,  0.050006113556918287 ,  0.990404760409599572  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.945660844588146254 ,  0.376933293036264827 ,  1.000000000000000000 ,  0.376335884550369315 ,  0.891306556609758660  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.444090917110072425 ,  1.000000000000000000 ,  0.679528343355392028 ,  0.496345831557343242 ,  1.000000000000000000 ,  0.237663975213541656 ,  0.931565584588580853  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.745261797335964760 ,  0.223433223908099415 ,  1.000000000000000000 ,  0.478847116012410035 ,  0.619711891888297117 ,  1.000000000000000000 ,  0.154547297693970159 ,  0.949512125027319631  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.364729302115553267 ,  1.000000000000000000 ,  0.595811437511508557 ,  0.346263224296851635 ,  1.000000000000000000 ,  0.333746302111132598 ,  0.731785383924030697 ,  1.000000000000000000 ,  0.101248014066215211 ,  0.969797100578168370  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.656514144553283496 ,  0.172024178305999087 ,  1.000000000000000000 ,  0.451490075361523080 ,  0.490760123805918824 ,  1.000000000000000000 ,  0.228140322685469826 ,  0.812477294203288958 ,  1.000000000000000000 ,  0.066283502641877615 ,  0.977513399753877521  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.523235086903438118 ,  1.000000000000000000 ,  0.878267558754215472 ,  0.413755599607017688 ,  1.000000000000000000 ,  0.548768417045225720 ,  0.681823716511260725 ,  1.000000000000000000 ,  0.271502180401037907 ,  0.887603005036984638 ,  1.000000000000000000 ,  0.079480411580017035 ,  0.985328857927569857  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.975964013720886592 ,  0.296766536723603258 ,  1.000000000000000000 ,  0.703389042775898421 ,  0.521873275151072735 ,  1.000000000000000000 ,  0.401252144569799352 ,  0.765938971699454907 ,  1.000000000000000000 ,  0.190187908200295047 ,  0.922111723248753234 ,  1.000000000000000000 ,  0.054788118585693622 ,  0.991300190201907294  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.942043243689472343 ,  0.373118579958733876 ,  1.000000000000000000 ,  0.378541951353386474 ,  0.888853761728643943  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.438414855835128181 ,  1.000000000000000000 ,  0.679442126907308097 ,  0.488307917110206324 ,  1.000000000000000000 ,  0.242364280533459014 ,  0.929534877941409432  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.734720290835960910 ,  0.216110068271063355 ,  1.000000000000000000 ,  0.483294725572435635 ,  0.609410567755606203 ,  1.000000000000000000 ,  0.159453131972241285 ,  0.947526894667454322  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.355954960569543177 ,  1.000000000000000000 ,  0.590540298712882161 ,  0.333428794758237501 ,  1.000000000000000000 ,  0.341584240835885344 ,  0.719799976822764420 ,  1.000000000000000000 ,  0.106127735049606459 ,  0.967761570912958335  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.639620088438020762 ,  0.162358320541397150 ,  1.000000000000000000 ,  0.452985123272195478 ,  0.472513213981670732 ,  1.000000000000000000 ,  0.237578908304687197 ,  0.800145753899542123 ,  1.000000000000000000 ,  0.070739219799714023 ,  0.975336601591202390  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.504762499066448878 ,  1.000000000000000000 ,  0.861451959689104818 ,  0.389883887205608848 ,  1.000000000000000000 ,  0.558241924956799696 ,  0.658984999399859106 ,  1.000000000000000000 ,  0.286085824597152205 ,  0.876436998943860623 ,  1.000000000000000000 ,  0.085519242671734413 ,  0.983782042293696479  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.946173375243036352 ,  0.277605999759960531 ,  1.000000000000000000 ,  0.699545849015512755 ,  0.497262410741342420 ,  1.000000000000000000 ,  0.412923717096473575 ,  0.745913227762831976 ,  1.000000000000000000 ,  0.201538712213934768 ,  0.912129940288503693 ,  1.000000000000000000 ,  0.059048018711862917 ,  0.988050516477593677  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.942043243689472343 ,  0.373118579958733876 ,  1.000000000000000000 ,  0.378541951353386474 ,  0.888853761728643943  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.435174732778345896 ,  1.000000000000000000 ,  0.680140928700248559 ,  0.484127187243244661 ,  1.000000000000000000 ,  0.245836084951815276 ,  0.930403581524380185  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.725954922072672981 ,  0.210171209256565045 ,  1.000000000000000000 ,  0.486643851123559568 ,  0.600647322456405397 ,  1.000000000000000000 ,  0.163460104259438854 ,  0.945283935586137325  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.348383192947009201 ,  1.000000000000000000 ,  0.585637328268051482 ,  0.322380100841556549 ,  1.000000000000000000 ,  0.348259779686679538 ,  0.708960790355203319 ,  1.000000000000000000 ,  0.110475349111949644 ,  0.965557977171965343  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.624721305549085559 ,  0.154128898007683535 ,  1.000000000000000000 ,  0.453831059266461834 ,  0.456401557516396050 ,  1.000000000000000000 ,  0.245948755114087370 ,  0.788697171920881068 ,  1.000000000000000000 ,  0.074839073123346631 ,  0.973041801571028486  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.491916083373139634 ,  1.000000000000000000 ,  0.849517062447618487 ,  0.373578810647674953 ,  1.000000000000000000 ,  0.565299328886330832 ,  0.643252568320047180 ,  1.000000000000000000 ,  0.297368953056912300 ,  0.869564092693022928 ,  1.000000000000000000 ,  0.090291011955955236 ,  0.984570648071891763  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.917557276051475967 ,  0.259834438193164874 ,  1.000000000000000000 ,  0.696110527683694080 ,  0.474172540593062297 ,  1.000000000000000000 ,  0.425690254332078966 ,  0.727764189935454775 ,  1.000000000000000000 ,  0.214262650123301174 ,  0.905232504687009221 ,  1.000000000000000000 ,  0.063901960428635538 ,  0.988960767685827413  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.942043243689472343 ,  0.373118579958733876 ,  1.000000000000000000 ,  0.378541951353386474 ,  0.888853761728643943  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.431633336401262169 ,  1.000000000000000000 ,  0.679493407659008120 ,  0.478826776263507337 ,  1.000000000000000000 ,  0.248407327560992902 ,  0.927836437263627412  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.719886796470818768 ,  0.206027618661737466 ,  1.000000000000000000 ,  0.490059644849192733 ,  0.595272809461398600 ,  1.000000000000000000 ,  0.167047499475432820 ,  0.946303348722744908  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.341932577458178177 ,  1.000000000000000000 ,  0.581172114634889825 ,  0.312983954103168260 ,  1.000000000000000000 ,  0.353813068809094244 ,  0.699287089483326185 ,  1.000000000000000000 ,  0.114251591980503914 ,  0.963180082903551171  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.611783481609094015 ,  0.147202694529164430 ,  1.000000000000000000 ,  0.454168664904937402 ,  0.442393694052845454 ,  1.000000000000000000 ,  0.253195463567639334 ,  0.778236409639122217 ,  1.000000000000000000 ,  0.078512625712369152 ,  0.970610602889042107  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.476428361677405987 ,  1.000000000000000000 ,  0.833925548748272849 ,  0.353958188503244231 ,  1.000000000000000000 ,  0.572256254053421176 ,  0.622803408334976050 ,  1.000000000000000000 ,  0.310461483617243961 ,  0.858655366828983024 ,  1.000000000000000000 ,  0.096034234958397344 ,  0.982729302606952571  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.891402585843427131 ,  0.244274033119927680 ,  1.000000000000000000 ,  0.690995431237797053 ,  0.452755890689633511 ,  1.000000000000000000 ,  0.435530025223059836 ,  0.708816746552858090 ,  1.000000000000000000 ,  0.225139703210058689 ,  0.895254220428384984 ,  1.000000000000000000 ,  0.068190771058976085 ,  0.985743628853457565  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.942043243689472343 ,  0.373118579958733876 ,  1.000000000000000000 ,  0.378541951353386474 ,  0.888853761728643943  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.429411903770103409 ,  1.000000000000000000 ,  0.679925697752679303 ,  0.475945429298712974 ,  1.000000000000000000 ,  0.250855773479024602 ,  0.928440782109718366  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.714708467481228515 ,  0.202673537744553184 ,  1.000000000000000000 ,  0.491311162046367988 ,  0.589666666481312607 ,  1.000000000000000000 ,  0.169056858735586679 ,  0.943493284647002994  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.337137545863947730 ,  1.000000000000000000 ,  0.578260276879676627 ,  0.306230485146045783 ,  1.000000000000000000 ,  0.358984254251168700 ,  0.693343156916513492 ,  1.000000000000000000 ,  0.117664281754234310 ,  0.964230737997167964  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.601568455679483582 ,  0.141826219627983197 ,  1.000000000000000000 ,  0.455005046796672863 ,  0.431724829225357598 ,  1.000000000000000000 ,  0.259998158708933946 ,  0.771538900017454510 ,  1.000000000000000000 ,  0.081968798329607531 ,  0.971735407212995561  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.464303427575956607 ,  1.000000000000000000 ,  0.820986969427659030 ,  0.338712841400739761 ,  1.000000000000000000 ,  0.576809550711299357 ,  0.605996276662658095 ,  1.000000000000000000 ,  0.320551271796106874 ,  0.848678462109914045 ,  1.000000000000000000 ,  0.100613891275780992 ,  0.979870016403665001  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.870170321354792997 ,  0.232008216405744955 ,  1.000000000000000000 ,  0.687010495705434487 ,  0.435737818894578910 ,  1.000000000000000000 ,  0.444513180325788460 ,  0.694137401628229012 ,  1.000000000000000000 ,  0.235225404218445400 ,  0.888918230595170167 ,  1.000000000000000000 ,  0.072220692089939753 ,  0.985861428764035042  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Poles*/  {{ 1.000000000000000000 ,  0.942043243689472343 ,  0.373118579958733876 ,  1.000000000000000000 ,  0.378541951353386474 ,  0.888853761728643943  },
/* 5 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.427021672587788503 ,  1.000000000000000000 ,  0.679020083599551128 ,  0.472142671531143143 ,  1.000000000000000000 ,  0.252219615965524113 ,  0.925583801650305182  },
/* 6 Poles*/  { 1.000000000000000000 ,  0.710673498084498423 ,  0.199980766229736179 ,  1.000000000000000000 ,  0.493537731740689101 ,  0.586070977378043256 ,  1.000000000000000000 ,  0.171505462950015175 ,  0.944180882468173222  },
/* 7 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.332467372979914244 ,  1.000000000000000000 ,  0.574636830412476485 ,  0.299422607269242635 ,  1.000000000000000000 ,  0.362696198437312822 ,  0.685657202115799280 ,  1.000000000000000000 ,  0.120406304300459510 ,  0.961534224124925840  },
/* 8 Poles*/  { 1.000000000000000000 ,  0.591742870291224965 ,  0.136823628891382998 ,  1.000000000000000000 ,  0.454705185374338017 ,  0.421043295061905243 ,  1.000000000000000000 ,  0.265364827558700944 ,  0.762789817969815265 ,  1.000000000000000000 ,  0.084860842483639509 ,  0.969048345634971331  },
/* 9 Poles*/  { 0.000000000000000000 ,  1.000000000000000000 ,  0.455209532017615381 ,  1.000000000000000000 ,  0.811374319850976966 ,  0.327510910258249632 ,  1.000000000000000000 ,  0.580893426639302879 ,  0.593846846226569136 ,  1.000000000000000000 ,  0.329176743894304513 ,  0.842465795839762444 ,  1.000000000000000000 ,  0.104560078215593627 ,  0.979889565228648784  },
/* 10 Poles*/  { 1.000000000000000000 ,  0.848715784702477771 ,  0.219984312181815050 ,  1.000000000000000000 ,  0.682492321227462395 ,  0.418657804906673092 ,  1.000000000000000000 ,  0.453546470999259710 ,  0.678980863652053457 ,  1.000000000000000000 ,  0.245875792097658058 ,  0.882347092503278030 ,  1.000000000000000000 ,  0.076561654651646133 ,  0.986245640774516041  } } } ;

 const double InvChebyNumerator[19][9][24] = {
/* Inv Cheby Stop Band Attenuation = 10.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.978841760960471600  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.847141410137843390  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.408236086231936080 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.207801403048597070  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.247478842482956370 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.265942053156292600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.165438555425359680 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.174737951191682670 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.232465053643167600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.116560180360053780 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.736204332010945040 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.637420055344743550  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.090064110941623140 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.516725368734431530 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.397206713236855970 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.550435108808525100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.068319922753895980 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.381473813906354930 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.507661765613826250 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.857284253447250680  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.053700792922253490 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.294780345099738380 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.055828931011474570 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.987278624115259800 ,  1.000000000000000000 ,  0.000000000000000000 ,  42.004157959075236300  } },
/* Inv Cheby Stop Band Attenuation = 15.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.500297560007939350  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.370370370370370680  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.638073699311621830 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.547393181410457790  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.375630809617433230 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.601448263286592510  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.250534513612941100 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.333529170945014110 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.417698853448293000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.177346209235009990 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.830724061905315110 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.944323690537913760  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.135010527844255930 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.579264232334893550 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.537283124986314590 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.686417231162568700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.103130518665607520 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.426488350913714950 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.589372495280843460 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.145893813612635980  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.083531091091263220 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.331435611962148660 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.114029504085423120 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.128468617905331150 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.193296815830230200  } },
/* Inv Cheby Stop Band Attenuation = 20.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.931548549863435900  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.152028905782660480  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.948624760072309670 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.357417407568510400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.545793484423141350 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.046939935449453070  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.357155379207831030 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.532486414738519990 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.902735938158915700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.253860518867237590 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.949700609861254600 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.330638115168823350  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.187739016720663400 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.652631143446933180 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.701612520470246360 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.019084545491562500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.144365507189116560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.479810446335386940 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.686163168115415890 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.487767073449322910  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.119200519429395020 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.375265962136863030 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.183622545319455990 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.297295839712595540 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.615203596472987600  } },
/* Inv Cheby Stop Band Attenuation = 25.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  18.575963718820872800  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.273134790617309160  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.369759257795036160 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.811969137264394900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.766921351019724810 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.625860213732555510  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.491670519550750700 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.783495083837213000 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.776290150551876200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.349951119853534330 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.099117471247216930 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.815791616662666640  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.260335427149523730 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.753642465874010180 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.927860692839467930 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.853896530013326300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.197981408691592310 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.549142640144633760 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.812015493213024350 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.932288672267858940  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.156660718285660080 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.421296798906099830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.256709479568194790 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.474598970037508390 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.108496683567615500  } },
/* Inv Cheby Stop Band Attenuation = 30.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  32.505921011842041000  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.968262641440705170  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.943772103731816520 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.157561178478328400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.049989210339876240 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.366941500378299420  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.663292855124592240 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.103746721906939680 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.166680919467381700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.464250746408696680 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.276848605012271600 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.392880983088669920  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.339796892519730780 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.864205889762625910 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.175503946928549760 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.862216887833056900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.260884527781724660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.630484389919405700 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.959667655566750710 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.453809233986426400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.211318217619816280 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.488459561167956970 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.363349304822829570 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.733298764047195740 ,  1.000000000000000000 ,  0.000000000000000000 ,  48.287333646680103500  } },
/* Inv Cheby Stop Band Attenuation = 35.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  56.888888888888921300  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.398820966295016180  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.702748840304743270 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.581201776975611000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.420890061487272590 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.337972548009308760  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.875989639915478070 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.500644325669812760 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.129164964694627600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.601395927365693250 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.490103619368218890 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.085315665267236440  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.440169257420561570 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.003864934250801260 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.488316439736895090 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.399042289349353000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.340740151765321020 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.733747889061229670 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.147112343967483290 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.115880534721730700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.269943500543467390 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.560497908832990440 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.477730496839325890 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.010778502433809400 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.624340186735167900  } },
/* Inv Cheby Stop Band Attenuation = 40.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  98.383936948770937600  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.953261972344769500  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.723118797543047090 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.528353713025225600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.884089548119797900 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.550644563658534200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.144051558106446540 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.000854674851516360 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.862785839850349800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.776652410462498550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.762620112897081980 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.970171161654830530  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.559694304934672760 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.170173199924497090 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.860818652899509650 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.419935310559111500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.428429411209092280 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.847141277223585790 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.352944883901116220 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.842899361503162600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.338887340137703720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.645215628532867850 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.612243767594075280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.337097073874930150 ,  1.000000000000000000 ,  0.000000000000000000 ,  53.372680083671284000  } },
/* Inv Cheby Stop Band Attenuation = 45.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  173.318347107438115000  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.334126826687832300  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.120870531932331100 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.675047835408669500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.470016817339476310 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.084622089743687570  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.459342218239042750 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.589195056293915440 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.254218231131169100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.982335915521253660 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.082449351649846130 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.008650176818992600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.703225173346286200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.369883388647549440 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.308135489432587840 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.047554857513318900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.532282059728121040 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.981435987429892800 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.596717662450049870 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.703926483449185600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.420108572942252230 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.745019725242757280 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.770710916270056060 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.721525861355497790 ,  1.000000000000000000 ,  0.000000000000000000 ,  56.610439336837956100  } },
/* Inv Cheby Stop Band Attenuation = 50.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  311.890541344438077000  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.102365220722020900  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.913648890984993580 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.124125851979314900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.190016837036973030 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.969606638197493700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.849660152343921120 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.317538236959330260 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.690645742193901000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.225920176834940810 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.461212679539736210 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.238486977423244500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.867520269012676430 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.598485116797839560 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.820164457588978380 ,  1.000000000000000000 ,  0.000000000000000000 ,  47.199972432239604800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.656054279825755590 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.141489242369822850 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.887247547153396270 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.730103860408513900  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.508951083280910990 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.854188795787722780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.944047601868788890 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.142026971064184160 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.152009071635653500  } },
/* Inv Cheby Stop Band Attenuation = 55.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  545.565036420395700000  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.393724836873531100  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  10.381314217490421800 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.506733375793174900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.159772732308040230 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.508460566459833200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.340845331516066440 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.234102259348895280 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.531972741942325900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.517322894009332400 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.914331704216750920 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.709755208976274000  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.068134597176921830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.877621763719902860 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.445383043881443900 ,  1.000000000000000000 ,  0.000000000000000000 ,  52.270327445773524700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.795452195610854670 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.321748513276072230 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.214455545595896520 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.885831595284008300  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.614283236705820410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.983620227250701310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.149556499490588470 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.640575326286728420 ,  1.000000000000000000 ,  0.000000000000000000 ,  64.350913011300704600  } },
/* Inv Cheby Stop Band Attenuation = 60.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  991.092627599244452000  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  53.273179901437799800  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  13.649813324899408700 ,  1.000000000000000000 ,  0.000000000000000000 ,  79.556942230642917500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.328484465914279160 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.568187648647967300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.911855262593300520 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.299621296659084990 ,  1.000000000000000000 ,  0.000000000000000000 ,  54.485115109118808600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.851504984342143610 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.433970863056538650 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.397012959446753300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.289569651211138530 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.185728562769232220 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.135489840867162310 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.866908441101713800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.953221147936470500 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.525763875756106720 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.584785781999283220 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.193870907622532200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.739862638896967570 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.137931339853678560 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.394568907090375730 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.234956076860285630 ,  1.000000000000000000 ,  0.000000000000000000 ,  69.356942314376681200  } },
/* Inv Cheby Stop Band Attenuation = 65.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  1.618172839506174300E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  77.862627162694011000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  17.896439590364003900 ,  1.000000000000000000 ,  0.000000000000000000 ,  104.308093944960390000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.780592085720701160 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.369854803014376900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.642823259642098850 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.663626148642379120 ,  1.000000000000000000 ,  0.000000000000000000 ,  64.666185927644761500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.256933829200841400 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.064395742205352490 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.443989684234356500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.548569870992956070 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.546103883732269590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.942669232006415390 ,  1.000000000000000000 ,  0.000000000000000000 ,  64.412916769090998100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.132736991352421450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.757900739984163960 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.006162382087083530 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.682210513827449500  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.870775368557707540 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.298796008862795230 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.649986933613670990 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.854580037141163370 ,  1.000000000000000000 ,  0.000000000000000000 ,  74.575576496356617900  } },
/* Inv Cheby Stop Band Attenuation = 70.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.102295857988166540E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  111.455782312925194000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  23.631948277180413200 ,  1.000000000000000000 ,  0.000000000000000000 ,  137.737088349451000000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.683560245949090370 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.351890192037235000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.500922006040042330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.264860208537122300 ,  1.000000000000000000 ,  0.000000000000000000 ,  76.617959660062439300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.727838635252979760 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.796632999644401710 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.821549124819547700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.854138810186380490 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.971275355134612270 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.894980973274410730 ,  1.000000000000000000 ,  0.000000000000000000 ,  72.135909523382991900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.352003161615722870 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.041439842871477060 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.520844716424227800 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.500114267022006200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.028137035948237800 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.492160952097126180 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.957008310675049770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.599389650366759950 ,  1.000000000000000000 ,  0.000000000000000000 ,  80.848556813135218100  } },
/* Inv Cheby Stop Band Attenuation = 75.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.242880000000002210E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  165.182104599874009000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  31.335659607207095000 ,  1.000000000000000000 ,  0.000000000000000000 ,  182.637608426636632000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.063237236254334300 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.581965517481062700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.494200545037473130 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.118343195526122700 ,  1.000000000000000000 ,  0.000000000000000000 ,  90.452545016580757000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.308775540792020740 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.699965564931359200 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.754651540418525000  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.218148436312818110 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.477761743290795240 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.029424290091212100 ,  1.000000000000000000 ,  0.000000000000000000 ,  81.335940496712737500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.590790243710191290 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.350221972631128860 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.081348384976265290 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.479863045561984300  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.206221990911385600 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.710990529708793510 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.304461975937810440 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.442284801561513100 ,  1.000000000000000000 ,  0.000000000000000000 ,  87.947639046585507600  } },
/* Inv Cheby Stop Band Attenuation = 80.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.192000000000003720E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  242.053554939981581000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  41.525256869961687300 ,  1.000000000000000000 ,  0.000000000000000000 ,  242.026933503172700000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.000221241188882000 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.271089568732598700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.782966547936321260 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.523213296710306200 ,  1.000000000000000000 ,  0.000000000000000000 ,  108.402739822641252000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.994122473346427780 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.765651350759465110 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.214911551845169400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.629868769452365030 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.050633254083965800 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.312558984352042700 ,  1.000000000000000000 ,  0.000000000000000000 ,  91.741818653126571800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.867877169370927510 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.708530681308375150 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.731753075960368090 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.777150226320024700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.394475782713278540 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.942315504647456060 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.671755608208521870 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.333310145823070100 ,  1.000000000000000000 ,  0.000000000000000000 ,  95.452086286595346100  } },
/* Inv Cheby Stop Band Attenuation = 85.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  14.56355555555555980E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  341.333333333333428000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  54.599253299597634700 ,  1.000000000000000000 ,  0.000000000000000000 ,  318.227768922571670000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.548433574464663600 ,  1.000000000000000000 ,  0.000000000000000000 ,  48.560430179678881800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  9.280125920877962290 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.316950720413597500 ,  1.000000000000000000 ,  0.000000000000000000 ,  129.255479838728689000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.750514242625392570 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.941808883169478860 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.033871071300627200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.094045245709478920 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.696492731560721450 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.759174303064929400 ,  1.000000000000000000 ,  0.000000000000000000 ,  103.473480818492448000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.170090018484242660 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.099330411224349820 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.441135715614897170 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.282752764125959300  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.624219186669899620 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.224622631924421780 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.119997784484388250 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.420710264794864800 ,  1.000000000000000000 ,  0.000000000000000000 ,  104.610452963995726000  } },
/* Inv Cheby Stop Band Attenuation = 90.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  20.97152000000001240E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  517.049309664694533000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  72.691313564553070100 ,  1.000000000000000000 ,  0.000000000000000000 ,  423.676023713483005000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.522382485269893900 ,  1.000000000000000000 ,  0.000000000000000000 ,  61.582397659076576700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  11.113685866085251800 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.738420157875168600 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.793675392482498000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.692722793335351030 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.406903730876543100 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.791004160514681600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.653302843648074120 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.474648968369781880 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.502111848733248300 ,  1.000000000000000000 ,  0.000000000000000000 ,  117.608236752990436000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522717034446435140 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.555322083992141560 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.268855265364969400 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.206331786944129900  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.869836865254792090 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.526435959556802670 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.599211554649935250 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.583245024509741600 ,  1.000000000000000000 ,  0.000000000000000000 ,  114.401623131201276000  } },
/* Inv Cheby Stop Band Attenuation = 95.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  32.76800000000002200E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  722.159779614325316000  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  97.933928510917297700 ,  1.000000000000000000 ,  0.000000000000000000 ,  570.800765366538712000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.981927460101037000 ,  1.000000000000000000 ,  0.000000000000000000 ,  75.875672155748247900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  13.363381326064265800 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.936409037395581100 ,  1.000000000000000000 ,  0.000000000000000000 ,  186.127890967769304000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.803316364603793080 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.133830235561664100 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.398299299758079400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.288595310124509120 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.358600830282131080 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.482013590584216400 ,  1.000000000000000000 ,  0.000000000000000000 ,  133.664708750453769000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.937628275392466740 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.091855197579293170 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.242774250570896300 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.646300210949768000  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.151623718274977120 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.872693721958396560 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.148993398517560220 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.916972357794541900 ,  1.000000000000000000 ,  0.000000000000000000 ,  125.634621686915466000  } },
/* Inv Cheby Stop Band Attenuation = 100.00 db */
/* 2 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  58.25422222222226050E3  },
/* 3 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.078781893004115760E3  },
/* 4 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  127.913702544871569000 ,  1.000000000000000000 ,  0.000000000000000000 ,  745.535693539968861000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.588722964399742900 ,  1.000000000000000000 ,  0.000000000000000000 ,  95.790521595440282000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  16.125177478219768500 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.089990817908756100 ,  1.000000000000000000 ,  0.000000000000000000 ,  224.594749058617396000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.109539357595338060 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.164952300359738400 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.993311218478318400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.006657138914364150 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.357718754735786960 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.719867713840155700 ,  1.000000000000000000 ,  0.000000000000000000 ,  151.813105362739577000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.394747582857057470 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.682968720451238910 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.315768009498384600 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.436209542150685100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.477046280148513800 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.272570745679064250 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.783910940572452480 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.457232171147897000 ,  1.000000000000000000 ,  0.000000000000000000 ,  138.607090516965513000  } } } ;

 const double Elliptic_00Numerator[13][7][24] = {
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.910830454559533820 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.983215030828052500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.296741248914774050 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.574094351635229930  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.642871377361085680 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.739393947136502750 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.139960549202360300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359596583061372540 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.849965192756673150 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.009036420182806810  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.211545707006767230 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.463499556452673380 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.624915692405712430 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.848200838682242200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.365371244256305960 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.605290222553738650 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.491587068383541940 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.573920981573921020  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.234824400070324120 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.376417709723818160 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.842582726948407410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.698180624566437480 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.393452714392868300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.130056619002086650 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.116513524961042900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.684467623840570030 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.606957620406516800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.861859930968994050 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.164871400451119140 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.415324215841806700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.474172249666751980 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.041183893396058660 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.671302342055471790  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.276097973060154490 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.563522332102892600 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.874983183488928780 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.984940538485368200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.365371244256305960 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.605290222553738650 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.491587068383541940 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.573920981573921020  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.234824400070324120 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.376417709723818160 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.842582726948407410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.698180624566437480 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.393452714392868300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.545076789610010160 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.383447617708263500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.287255235084686420 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.200712813394950730  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.112210499619666230 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.644637465008714110 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.071357998598060100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.631225612125700500 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.297601153071648030 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.542679879104005280  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.364828948135644150 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.696826732587282780 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.198550496564729780 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.713479006184293500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.365371244256305960 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.605290222553738650 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.491587068383541940 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.573920981573921020  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.234824400070324120 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.376417709723818160 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.842582726948407410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.698180624566437480 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.393452714392868300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.732160338864641600 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.146037223045837600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.950132665808410600 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.946617197318923690  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.432676395635921200 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.253186213425564690 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.676566024268403500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.797099568766146630 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.564962069847452670 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.441128347241435570  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.464922935662419160 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844730189210929570 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.551890090776805840 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.673126369848581900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.365371244256305960 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.605290222553738650 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.491587068383541940 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.573920981573921020  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.234824400070324120 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.376417709723818160 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.842582726948407410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.698180624566437480 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.393452714392868300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  11.042022976087789500 ,  1.000000000000000000 ,  0.000000000000000000 ,  63.618228082225989800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.854624455557278400 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.324395555350850600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.850138860282009470 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.040963206513859870 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.608969472007203200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.000410307810968290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.888883973187595710 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.517801465996878820  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.601031165924027060 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.042385326255387490 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.015715488117872490 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.525005919818845300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.365371244256305960 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.605290222553738650 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.491587068383541940 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.573920981573921020  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.234824400070324120 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.376417709723818160 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.842582726948407410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.698180624566437480 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.393452714392868300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  15.422027688864837100 ,  1.000000000000000000 ,  0.000000000000000000 ,  89.155985106048433400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.958222518825404100 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.220572921254191800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.317668912962722590 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.921097748586039880 ,  1.000000000000000000 ,  0.000000000000000000 ,  42.225877058632910600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.244413273853986810 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.275365550686143660 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.795624417704038580  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.739789725933329260 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.241654716282358350 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.477906376554240180 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.341746114161011900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.453634791262664590 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.724910355566038780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.721305783766970520 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.414265223224147050  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.293269588720381110 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.453502856784386400 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.976610352641805730 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.045128108873536910 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.398858368414167300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  19.866655928235140000 ,  1.000000000000000000 ,  0.000000000000000000 ,  115.066571119989987000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.508484522655351160 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.285752784788140700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.945947267274431790 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.099067181643288650 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.051620995204132200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.549305623913073140 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.755239141875428730 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.372328412893287100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.906174463086362850 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.478185379331199640 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.020372145948029720 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.794654123063516200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.573109757624523210 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.884859425213488700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.024101346522486190 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.512335181081775560  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.372029085802998210 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.555526100779701220 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.150108600805118010 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.487475562824803530 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.206390818951696500  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.657752058880039000 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.651689458850484000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.014840524950820110 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.233330659366512100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.650562762352290490 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.418828544079023150 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.932827509621141400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.925756729944499310 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.345900330250047180 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.307437637192865800  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.127120777660862490 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.791150551046089670 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.735629701500307040 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.657854482459370600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.703207350227802450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.058455021540606560 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.351710170369714970 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.699396967269750600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.445594503213886610 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.649557072752317670 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.307329976869186170 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.883527397237754640 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.597634220957232300  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  34.047503891838566400 ,  1.000000000000000000 ,  0.000000000000000000 ,  197.725776041422563000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.585366507178159500 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.966962531730896300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.424691850719648660 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.867027423832977820 ,  1.000000000000000000 ,  0.000000000000000000 ,  71.764812337358677000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.335441484009817440 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.986895659403459470 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.401570107740450800  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.358852719097915160 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.117706275184637830 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.477651931512880880 ,  1.000000000000000000 ,  0.000000000000000000 ,  49.721860355920775000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.837730715617588380 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.235854325733651390 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.681560598783123690 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.882432060627797600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.546241919978610380 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.777012165750758270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.517892660060777390 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.409372675146215670 ,  1.000000000000000000 ,  0.000000000000000000 ,  42.083163731185663900  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  34.047503891838566400 ,  1.000000000000000000 ,  0.000000000000000000 ,  197.725776041422563000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.670838789970485300 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.047464996078133500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.633305191427575130 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.125995604654136200 ,  1.000000000000000000 ,  0.000000000000000000 ,  88.648406388761685600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.842518880862082040 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.779059776145127000 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.985805096601755100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.633391028850751600 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.503495177665791440 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.351589369757057570 ,  1.000000000000000000 ,  0.000000000000000000 ,  56.853014728293963700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.014042153857925840 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.467843641242534410 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.111930119506876480 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.424590849857432700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.652470633145251440 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.911372613328595090 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.739666518700043070 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.963474527019211990 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.814491134197957900  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  34.047503891838566400 ,  1.000000000000000000 ,  0.000000000000000000 ,  197.725776041422563000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.884674470154031700 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.463761080387783400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.706758990506195950 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.131416828926971300 ,  1.000000000000000000 ,  0.000000000000000000 ,  103.631500268890946000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.480959346265914970 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.775241065330133590 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.231808245486199400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.962169585235044700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.964466048338393560 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.393255907227631600 ,  1.000000000000000000 ,  0.000000000000000000 ,  65.342062847397457400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.190575507211525160 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.698980858052540730 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.538055911363882980 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.945116179971817500  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.777760263351941150 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.068388839166426680 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.995632084109609570 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.596823489280114750 ,  1.000000000000000000 ,  0.000000000000000000 ,  52.196874200589370200  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  34.047503891838566400 ,  1.000000000000000000 ,  0.000000000000000000 ,  197.725776041422563000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.045880413023709800 ,  1.000000000000000000 ,  0.000000000000000000 ,  59.978321820939470400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  9.241965101256562680 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.998456924742090000 ,  1.000000000000000000 ,  0.000000000000000000 ,  125.045668977646329000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.173007166151653460 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.853755686067587230 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.741624306347738100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.301906017171118980 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.439670883932223870 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.464076891778463010 ,  1.000000000000000000 ,  0.000000000000000000 ,  74.054709064206221600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.434353822089851520 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.017145573096777330 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.122291238519467880 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.024324703435429000  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.918793833451666760 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.244481167468854730 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.281316858246693610 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.301297636286355970 ,  1.000000000000000000 ,  0.000000000000000000 ,  58.175133655377507600  } },
/* Elliptic 0.00 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  34.047503891838566400 ,  1.000000000000000000 ,  0.000000000000000000 ,  197.725776041422563000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.602650085047233800 ,  1.000000000000000000 ,  0.000000000000000000 ,  74.527380088019185700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  11.298808246831011800 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.838773361255878300 ,  1.000000000000000000 ,  0.000000000000000000 ,  153.723969112568597000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.955405012332027680 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.072530838153435080 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.706258943087032500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.718699182424855200 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.021425485411787020 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.771603502505714200 ,  1.000000000000000000 ,  0.000000000000000000 ,  84.676784104670233000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.673746951703935170 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.329138448683006240 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.694263202056323080 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.058136331864233200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.078383955109578630 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.443144067344011900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.602352513452213860 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090703066664064020 ,  1.000000000000000000 ,  0.000000000000000000 ,  64.866003480180111800  } } } ;

 const double Elliptic_02Numerator[13][7][24] = {
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.382217388290925890 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.267130386157077700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.984659707252252490 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.534515229623921510  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.517541484924276100 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.395803677997490940 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.873514891509591600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.266112352867860610 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.636643848805156680 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.087902966447345850  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.123155797169774320 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.290335250034885920 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.104446691127333850 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.051572632806200500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.286306702831785390 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.583890179999752200  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.322031502876252150 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.445908842756429280  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.687654600972393170 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.732812077656004760 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.491969178679426000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.365045879145198260 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.807174695147338420 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.691268075284383170  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.185169118451272040 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.391631348174027670 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.369150974596555330 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.341304640015462000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.522451720459408800 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.819153735566352700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.780748619278231360 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.667780806563846420  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.863224227883054200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.074812843763151450 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.121134647739701500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.484872249544900940 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.008093356962592500 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.387206334895178190  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.242567809997022770 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.481278286735844720 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.594149436707434480 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.257110786652671000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  7.159227767392974510 ,  1.000000000000000000 ,  0.000000000000000000 ,  40.386283014696204000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.326810721383846040 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.115288321482843510  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.103431488429690080 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.536800690888984190 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.642794641388242400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.633058946053059350 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.250203799961979280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.207675010625717250  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.331608653435318420 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617053329496883180 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.927572854933359280 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.073657198399899900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  9.367531083869454990 ,  1.000000000000000000 ,  0.000000000000000000 ,  53.274433046370504000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.075168753818618760 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.088886926103413100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.404883156776736360 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.110267822838133080 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.980547195466208200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.811043401047659930 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.537275597501896570 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.170348222332532550  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.449582719969368270 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.792211995351871150 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.346855076507072810 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.578017502113400600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  12.808505455633916600 ,  1.000000000000000000 ,  0.000000000000000000 ,  73.345992997448334400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.003047388395337690 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.528795352553075300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.821427276767678460 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.899241902012409610 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.933561328732452000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022494177099571910 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.874019437549900860 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.286792398813247470  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.586542715205216060 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.991121444253926900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.812468974377534620 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.430015482735743900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  16.775989530520846000 ,  1.000000000000000000 ,  0.000000000000000000 ,  96.480597756302088900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.118025770898717570 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.457183366429918100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.317805391438668480 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.832977377457971180 ,  1.000000000000000000 ,  0.000000000000000000 ,  40.941689570114370200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.258811524132915420 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.249348216462291110 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.529463939456741530  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.734730336123687880 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204224260052173530 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.306531088212102180 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.501537324458524800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  21.616176959340894100 ,  1.000000000000000000 ,  0.000000000000000000 ,  124.700748477223897000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.690572486395493180 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.581082748432159200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.861027300541408640 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.854112501654722860 ,  1.000000000000000000 ,  0.000000000000000000 ,  48.605818921268422600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.550799854831506060 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.709556057794290760 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.042087004355270400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.905502602007490330 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.447934529486133660 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.867242628246846610 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.107160039808405800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  29.025219197242979200 ,  1.000000000000000000 ,  0.000000000000000000 ,  167.889276716077518000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.300333385235230570 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.799419520198259900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.587259901432248380 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.213816578492012960 ,  1.000000000000000000 ,  0.000000000000000000 ,  58.777278322327596500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.905164131810150870 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.266152719771691080 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.866088039196874600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.103836657684814430 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.729332233041727120 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.510836615240523710 ,  1.000000000000000000 ,  0.000000000000000000 ,  41.379792555963547300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  29.025219197242979200 ,  1.000000000000000000 ,  0.000000000000000000 ,  167.889276716077518000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.910758277912945500 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.639311717068345300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.379901628831855300 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.697812344029792090 ,  1.000000000000000000 ,  0.000000000000000000 ,  69.881705065001654000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.353193722348097870 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.967001616021562430 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.153655434089484900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.337084668575565070 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.057962011261704300 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.256596932382890050 ,  1.000000000000000000 ,  0.000000000000000000 ,  47.464123570537537900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  29.025219197242979200 ,  1.000000000000000000 ,  0.000000000000000000 ,  167.889276716077518000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.388439304433811500 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.129012518533073700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.398123682042487380 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.601217976648609200 ,  1.000000000000000000 ,  0.000000000000000000 ,  84.106564590180809700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.867376886056188120 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.771019042435898250 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.778172826789862900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.602376235638684590 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.431461063833836530 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.104054996684046940 ,  1.000000000000000000 ,  0.000000000000000000 ,  54.381936824423270600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  29.025219197242979200 ,  1.000000000000000000 ,  0.000000000000000000 ,  167.889276716077518000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.162187023625914900 ,  1.000000000000000000 ,  0.000000000000000000 ,  47.011820432251148800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.827737844265049820 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.273289726668704000 ,  1.000000000000000000 ,  0.000000000000000000 ,  104.076603661127138000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.467456907525302330 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.707206065595613610 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.827047450155820500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.928160165326085810 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.888072633451820530 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.134719042069177060 ,  1.000000000000000000 ,  0.000000000000000000 ,  62.770953177064924900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.02 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  29.025219197242979200 ,  1.000000000000000000 ,  0.000000000000000000 ,  167.889276716077518000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.657866437848586100 ,  1.000000000000000000 ,  0.000000000000000000 ,  61.402523900410862700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  9.184638118545187520 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.807754140732615900 ,  1.000000000000000000 ,  0.000000000000000000 ,  123.007884497003204000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.156512804081815560 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.781350736653158680 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.322846319119143700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.294459986269078210 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.400624694590436370 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.289709277455116880 ,  1.000000000000000000 ,  0.000000000000000000 ,  72.165037604799351800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_04Numerator[13][7][24] = {
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.222573594055458290 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.162361972271185100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.895393315222705600 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.245810437920458650  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.464914326317793500 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.270613230501987710 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.777773515857626700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.238541511646042090 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.576891184148648590 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.839440942781567220  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.114902778610552090 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.269455312284458960 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.032665350266471100 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.363582959802945600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.109783224909276280 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.386852363687587800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.210716377110736540 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.099253954137351390  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.636878291231404600 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.612811351171966830 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.439483334688137700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.326172957894292680 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.731418538082551530 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.395622904366163650  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.166779699619341580 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.355084634685166160 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.258461344019158990 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.320710839804494400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.330764755183869500 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.541035725794742500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.655647009940739880 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.291863683958359490  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.848049572940778650 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.025200922886403630 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.611787669756747500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.440606807025490930 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.924340660214224560 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.065761529641880760  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.240769534118871590 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.472767609799291890 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.558802777691886470 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.895385526981822700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.812739023390880000 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.201968802643534700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.163033830613524170 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.636361909365292360  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.105307960040177840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.521102927458702240 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.396424840066234900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.581237521586991380 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.154809864913545430 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.847715685157088390  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.324156499052165260 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.599635873835122180 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.869429469803093550 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.512873590083650300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.999608123510254740 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.974050688637596800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.822508530763310210 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.379469803175075170  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.424057224498827880 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.128252513062634850 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.992217062640516900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.747246916106757640 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.424509019443483290 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.757497888211145390  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.419984570774942780 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.742351074165355620 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.211857421346197670 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.375505888841090000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  12.037498431332979400 ,  1.000000000000000000 ,  0.000000000000000000 ,  68.694934422969282600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.754026418118528770 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.829141168379814900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.752741490927386180 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.750071192946265340 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.677044033779978400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.949407507157990160 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.747896068521113120 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.833563587911230640  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.534916611676110150 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.910710614589019580 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.609385657575683750 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.676917815626897100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  15.430950341185365500 ,  1.000000000000000000 ,  0.000000000000000000 ,  88.481520319018500200  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734849240686393120 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.407674403532805500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.154901662635260170 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.508205188195611330 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.375939370500347500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.189716115125781130 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.129494960633185220 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.095677420419296060  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.669422057808749970 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.105353467793281920 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.063494266316949320 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.429396481630597300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  20.439750990849663700 ,  1.000000000000000000 ,  0.000000000000000000 ,  117.686869404632418000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.113102221405357640 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.021968717104694900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.654548176037687670 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.447564948296014010 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.424576375147175600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.470371680617058360 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.572818232375771340 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.555524799219034200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.835118649393824300 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.341833221246696790 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.607099146462432240 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.888528481221392500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.741312740167863600 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.421637721230837000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.028157112077774830 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.043032137498297400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.301412027620245660 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.659752835803538500 ,  1.000000000000000000 ,  0.000000000000000000 ,  54.497698140936542200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.820608715875719600 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.122671383038961860 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.355653110851331900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.019390872268313150 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.604203323829266560 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.209344966530601080 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.830395329451725700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.741312740167863600 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.421637721230837000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.016462857868978300 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.252644564762235300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.119924785546418280 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.193014483558652470 ,  1.000000000000000000 ,  0.000000000000000000 ,  65.974542266333713800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.215065790437699360 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.741478593305973100 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.381426464020192300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.242348132763988030 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.919028055034381010 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.925365152539120220 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.677592736516381000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.741312740167863600 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.421637721230837000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.662877116176838800 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.184882231227931000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.034217935833323660 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.902871863069322000 ,  1.000000000000000000 ,  0.000000000000000000 ,  78.756629859293354900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.684413620876985100 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.475187172962356460 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.775004120097236200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.504793794092396110 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.288308403988443370 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.762243592266509400 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.501244620793080700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.741312740167863600 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.421637721230837000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.978516337928724100 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.868774774814170300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.321684377608451480 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.308936634391338700 ,  1.000000000000000000 ,  0.000000000000000000 ,  96.734664859037366100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.247094741372369420 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.353604536034197280 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.637359213780005300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.797004379426073940 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.698430420788402890 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.689309520170098720 ,  1.000000000000000000 ,  0.000000000000000000 ,  59.052056127701838800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.04 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.741312740167863600 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.421637721230837000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.751298839916390900 ,  1.000000000000000000 ,  0.000000000000000000 ,  56.365411061992567500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  8.782957745522381290 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.038737891410413300 ,  1.000000000000000000 ,  0.000000000000000000 ,  117.126640449851806000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.915889132501034990 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.396632302862795600 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.033189847969321600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.144288975869743210 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.184860830376504030 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.786566853284004000 ,  1.000000000000000000 ,  0.000000000000000000 ,  67.980678484219268400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_06Numerator[13][7][24] = {
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.137892250395051260 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.565838968022564600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.846749840023534710 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.085929932501646710  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.439877026072126530 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.207926197500663430 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.215270309090215000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.220328972842221080 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.539580562086136920 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.689942114524434660  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.098514132441239080 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.238575090189796500 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.942329045243008870 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.547860828081317300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.968638992134029490 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.461637130598770500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.143085697623476890 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.892235008750907890  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.600855638814876250 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.531044433965054630 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.741375328696596700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.304003832300691720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687283222275007600 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.221241490494255010  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.162348768082760440 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.344647421704342040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.223745490184520610 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.990359000644252400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.094948587471936460 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.063094999194007100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.574941279630584830 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.048193901506519590  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.797013233067864310 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.916560340547087900 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.718585246632617500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.416351917399465200 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.876645281726574190 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.877941447804380990  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.228772842842831950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.450075364875466730 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.492174396948602770 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.286419648535176200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.593359746127979370 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.828770525498114800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.095441637198003800 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.431885367635147510  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.047376884229026750 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.399224849621514720 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.399123305575589200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.546278190996118740 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.092604934724675130 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.619904855611246750  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.304192874905566460 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.566023291957377110 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.778874226412741070 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.712010402287571500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.615807410236607570 ,  1.000000000000000000 ,  0.000000000000000000 ,  48.640306694054736200  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.730558718620124560 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.110239209479169990  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.356474125066427390 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.987814736852759450 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.850273767316345200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.706862919265313310 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.353323104975677980 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.497926339152260590  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.397273508541137190 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.705570889240918040 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.115825032028773030 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.535869212137019000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  11.338516879117396000 ,  1.000000000000000000 ,  0.000000000000000000 ,  64.525845754896735700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.589515245003339850 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.373055474171208000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.732531415680292670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.701312614815725160 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.237702828957822000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.899863578357569690 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.663115605097715830 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.531645730287204190  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.504837979115237530 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.864004894917748120 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.491939608055535160 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.666564587524440800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  14.962332603187857000 ,  1.000000000000000000 ,  0.000000000000000000 ,  85.656729244361301300  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.533328499378313840 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.851190889028771400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.184644713485007550 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.553689493232703890 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.644613528946059900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.134946586958632600 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.037264021643486970 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.771407220551344250  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.640641302956734160 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.060068197518702960 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.947805472157787320 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.423570074194021400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  19.545189822289604100 ,  1.000000000000000000 ,  0.000000000000000000 ,  112.380732723345005000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.979110694622967510 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.645482372377660600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.743714294206129890 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.603177870628491600 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.508198689219234700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.401916337046761550 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.459722286168797290 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.164662074009353200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.793985413033678090 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.280545067383648930 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.458883997844900190 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.635181176470617000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.818650584226059400 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.776593137322578000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.538070092928016660 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.732261692514555300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.408847262767630500 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.851068464671738130 ,  1.000000000000000000 ,  0.000000000000000000 ,  55.858872791764405000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.733884620219333430 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.981493371871790640 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.874487481122272900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.978118203244983110 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.542241896824182670 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.057965848667747900 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.540950230363257800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.818650584226059400 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.776593137322578000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.688749061216116200 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.367754433254791700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.240037042122552610 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.406770624537898580 ,  1.000000000000000000 ,  0.000000000000000000 ,  67.494316050788526700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.128526213798863860 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.600017570835756690 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.896640902738301600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.192356773938167080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.845192830974117900 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.747988281392293790 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.179384655275747200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.818650584226059400 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.776593137322578000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.146638909962177900 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.806735315502059800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.886144458830703030 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.615636596328517500 ,  1.000000000000000000 ,  0.000000000000000000 ,  76.534858215375607000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.587176051909658270 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.317408187412056900 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.238091342441414600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.435807541540934370 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.188168804875015640 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.526248350122125790 ,  1.000000000000000000 ,  0.000000000000000000 ,  49.528752191765143200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.818650584226059400 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.776593137322578000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.931258888989756200 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.719019069048428600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.103845616043060750 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.891730343110035400 ,  1.000000000000000000 ,  0.000000000000000000 ,  93.543826120647295800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.120244927634932000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.149975131639934920 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.952114834841779600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.722724125279519570 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.591201459930750680 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.438067747672665320 ,  1.000000000000000000 ,  0.000000000000000000 ,  56.958091127127559600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.06 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.818650584226059400 ,  1.000000000000000000 ,  0.000000000000000000 ,  154.776593137322578000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.495285479695716900 ,  1.000000000000000000 ,  0.000000000000000000 ,  53.050917239526427200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  8.504428151453831580 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.507183477570217500 ,  1.000000000000000000 ,  0.000000000000000000 ,  113.073791648741164000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.750230564041772570 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.132828387908736190 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.153041515285210000  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.064229391794999200 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.069826115315429150 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.518380287278185750 ,  1.000000000000000000 ,  0.000000000000000000 ,  65.751160475362908600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_08Numerator[13][7][24] = {
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.010049414557094400 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.743551576374986900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.810472730477290290 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.966320446412330720  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.411578658932489240 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.144856999137197300 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.687100659978433700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.206649795829983550 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.511626818104196080 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.578265571730482990  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.090472063209104950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.222999456572836820 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.895847438685073660 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.125603761339448300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.852690458015386900 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.714936754327943900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.126996496327534740 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.829140719706034980  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.572560816533685960 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.467604468166386680 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.204822864611646300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.293502275337095720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.663973456954259470 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.122845995180032470  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.153586636713214820 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.329076142728238710 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.180137395968900550 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.601853098417493000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.900924137424691910 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.864124151433465200  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.503664416648279990 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.840700415491104640  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.768185118821302470 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.851579268529877090 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.164223561534889500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.395106685046571650 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.838371498462671120 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.737101561909552670  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.213521722241271530 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.425294191413711830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.427663816812983820 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.727786388950281800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.478490740883439790 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.089414026800120900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.004634081706177180 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.172139027536232450  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.009797138196636630 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.318802040765417070 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.733645946609947000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.523260654179245500 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.050777154354575860 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.464218309146810170  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.284359136022477310 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.535101706677779900 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.701282763814790490 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.049763859791482900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.393486505742215440 ,  1.000000000000000000 ,  0.000000000000000000 ,  47.276384565500208600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.676245487319445000 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.947947449876147980  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.306231364208946030 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.886123856796568350 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.040614491600084800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.679262153946847970 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.304937697726330280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.322533117154773840  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.380368667526874260 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.678283339572468110 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.044854552728054830 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.917113933140033300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.937337773118496600 ,  1.000000000000000000 ,  0.000000000000000000 ,  62.120667385159556800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.413973791341427420 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.891099005119460900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.666969263550905910 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.569929315081425080 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.197718741712545700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.866321514401475530 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.605948019693138830 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.328998468790421050  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.485519867729063080 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.833757372934199600 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.415307814244917140 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.005267315803699500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  14.907766785934184000 ,  1.000000000000000000 ,  0.000000000000000000 ,  85.273247025129734300  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.402402846944728050 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.489498991098035000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.109391191545388900 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.404797459378845480 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.476569928029398700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.087224830319387880 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.958271907868436300 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.498540259002757220  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.613899885751865830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.019625926614203590 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.848679288646331460 ,  1.000000000000000000 ,  0.000000000000000000 ,  27.581064709231835500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  19.409112519085365300 ,  1.000000000000000000 ,  0.000000000000000000 ,  111.517934471094605000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.770763735902662890 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.081604029599159100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.657479251650243240 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.434223167268816820 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.192300721901190700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.359612571840363330 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.388571732585750060 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.914463072248812740  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.769415326837503470 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.242591177759965420 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.363544327584733650 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.812357579527816400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.322023970320795900 ,  1.000000000000000000 ,  0.000000000000000000 ,  151.817667332471160000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.233187973892732710 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.915960710062101700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.326389551679701740 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.688178277152706740 ,  1.000000000000000000 ,  0.000000000000000000 ,  54.580065148356816000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.679974788243562500 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.892553439491186930 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.567225123534905500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.941310355856783910 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.488307666689959950 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.929830082843724700 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.467357571092904300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.322023970320795900 ,  1.000000000000000000 ,  0.000000000000000000 ,  151.817667332471160000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.230215851592534600 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.149478025709953500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.105170859205529150 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.147664809114047560 ,  1.000000000000000000 ,  0.000000000000000000 ,  65.508329463744061600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.059489221548117310 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.487773533090936520 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.514303518903862100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.148616972417661140 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.781798793538324600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.599093183339229010 ,  1.000000000000000000 ,  0.000000000000000000 ,  41.938905331190902400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.322023970320795900 ,  1.000000000000000000 ,  0.000000000000000000 ,  151.817667332471160000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.982472468179711500 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.359734240989723700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.710695339217652490 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.279511547371127400 ,  1.000000000000000000 ,  0.000000000000000000 ,  73.964478325033312000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.512619822757160560 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.196853203885092750 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.829495110022897100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.391336448765015280 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.124008929259121550 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.376210248495673300 ,  1.000000000000000000 ,  0.000000000000000000 ,  48.280928704028909200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.322023970320795900 ,  1.000000000000000000 ,  0.000000000000000000 ,  151.817667332471160000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.967745290585798000 ,  1.000000000000000000 ,  0.000000000000000000 ,  41.177026290580464300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.857289295891554560 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.423110139850406200 ,  1.000000000000000000 ,  0.000000000000000000 ,  89.985419710906200000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.041244671499359950 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.022734617082370790 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.522416489455924400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.669050991889948770 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.514358520034195800 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.259900089158884740 ,  1.000000000000000000 ,  0.000000000000000000 ,  55.483110730063671700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.08 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  26.322023970320795900 ,  1.000000000000000000 ,  0.000000000000000000 ,  151.817667332471160000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.041986054904494800 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.847207667092909800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  8.137840166087201510 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.816015094322697400 ,  1.000000000000000000 ,  0.000000000000000000 ,  107.864460129349382000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.647407054160575870 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.968663863734853690 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.603788735278676800  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.999197009846676920 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.976474453396925400 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.301054655972576410 ,  1.000000000000000000 ,  0.000000000000000000 ,  63.946363620216466500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_10Numerator[13][7][24] = {
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  2.981718773896979700 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.519395816382466800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.783554483342089990 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.880553936826415030  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.398557074455600620 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.111570443096097540 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.386401058608695900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.198888495381165380 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.493603850114814340 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.500766511690756480  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.094678316058159680 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.227716904293104870 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.903160247476226590 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.168518297252335400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.795843933102543750 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.327880149395039000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.064998530761663180 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648647739173708440  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.552232077058592450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.421367083008249650 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.810774108748495000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.276707567744882920 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.633670081245272470 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.011798283036835500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.147545749365803890 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.317648795870105530 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.146654730390666010 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.298245060676146400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.800720667020049960 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.226539492214723000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.462264377114409260 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.716957995717931950  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.742831070486916100 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.796808822834240700 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.710366630499031300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.381345335473946450 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.812187507535168330 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.636672833923928930  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.204137120872869950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.409295222657244830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.384272716949281890 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.345208787208601800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.298715585420412210 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.989755718539271400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.945254696772510620 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.002207868499528590  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.977625913413998180 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.251970934444387230 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.192809315174695700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.507108851897220440 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.021412087636605290 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.354980817946657370  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.278902361942984680 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.524016527424132410 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.667143878451684100 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.731199880344402000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.121805881978870540 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.636949638325631900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.589140306286996210 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.706176970202204050  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.273125144038977470 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.816527527197596470 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.470606148908196100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.659980468563983620 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.271126550732118780 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.200033866110609360  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.365400211609635540 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655003350580699810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.986458635729883240 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.417500691544496800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.879186854179572200 ,  1.000000000000000000 ,  0.000000000000000000 ,  61.727766213549905700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.390162091170868310 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.816327316277458700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.622926121154026010 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.480361716838472400 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.480669997016747200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.837757263429856240 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.557851046726132170 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.160554765059331750  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.470471679577436850 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.809555103040670020 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.352432337495210750 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.456106502377370000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  14.120651374551620400 ,  1.000000000000000000 ,  0.000000000000000000 ,  80.631456690762505000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.378678743924240280 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.412970813506527500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.061704712393921390 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.308936245229424420 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.714838489276729700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.060211076386921470 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.911798925371619880 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.332015057941827510  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.594559522350633030 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.990786908566526630 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.779100069938106100 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.995102309000923700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  19.068796040851474100 ,  1.000000000000000000 ,  0.000000000000000000 ,  109.481529248849171000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.535890361415544800 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.452047841176128200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.592424225412054200 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.306205787643069270 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.191703339672095300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.325589973860290090 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.331456798425755040 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.714077981364185990  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.742166896602448390 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.202780134232627420 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.269479469549815500 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.028169001627020400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  25.651064011182622700 ,  1.000000000000000000 ,  0.000000000000000000 ,  147.854585186444552000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.148099887492563200 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.679339590329160400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.222258238778616500 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.488867665372555220 ,  1.000000000000000000 ,  0.000000000000000000 ,  53.058572749167197500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.636107988861164040 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.821491883710967930 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.326450501737731700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.916824707423372850 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.451710361936573880 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.840938809800169550 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.713248584546363400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  25.651064011182622700 ,  1.000000000000000000 ,  0.000000000000000000 ,  147.854585186444552000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.088382769093318100 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.764492382659341300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.008429293188186190 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.961039413507942090 ,  1.000000000000000000 ,  0.000000000000000000 ,  64.072710763477019200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.004530426555754640 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.399606930447260920 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.218373322669425100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.119305708713965510 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.738647547476698030 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.495894852051286250 ,  1.000000000000000000 ,  0.000000000000000000 ,  41.070052278962990000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  25.651064011182622700 ,  1.000000000000000000 ,  0.000000000000000000 ,  147.854585186444552000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.746468138071202600 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.728385267541718000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.648396525701040010 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.157401285269697800 ,  1.000000000000000000 ,  0.000000000000000000 ,  73.011414445085634400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.443258672230788520 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.086413299939115620 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.461573027262513600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.355968693208929650 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.072566329523832350 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.254754697236879930 ,  1.000000000000000000 ,  0.000000000000000000 ,  47.265100948756519000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  25.651064011182622700 ,  1.000000000000000000 ,  0.000000000000000000 ,  147.854585186444552000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.612346219206493500 ,  1.000000000000000000 ,  0.000000000000000000 ,  40.233175682959888800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.765959627778858730 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.246945000560117500 ,  1.000000000000000000 ,  0.000000000000000000 ,  88.629462999783314100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.968196104531609420 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.905689997583954740 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.129479300722252300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.626243489488799380 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.452678062393534920 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.115776651050094160 ,  1.000000000000000000 ,  0.000000000000000000 ,  54.284370341778036100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.10 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  25.651064011182622700 ,  1.000000000000000000 ,  0.000000000000000000 ,  147.854585186444552000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.494175127079724800 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.399756533961806300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  8.009391818791719690 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.570683117398102600 ,  1.000000000000000000 ,  0.000000000000000000 ,  105.992730470797312000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.574006020257503020 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.851244922794861300 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.210103536093772200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.957553812781841260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.916580968020024760 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.161305937879596680 ,  1.000000000000000000 ,  0.000000000000000000 ,  62.784311070128730100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_12Numerator[13][7][24] = {
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  2.927277584838558600 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.159433738212174000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.757851090101390180 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.797429012588059200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.384277037418391480 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.077967067718353270 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.096454839492864200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.189511523387925870 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.474825655213643840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.426855274953831460  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.086707876733889800 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.211943906505084770 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.855357847106503750 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.732243297887818000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.719938614421853810 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.839360730618725600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.062428388234648580 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.629265365797024860  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.535021855345615190 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.383187620646005640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.490793436222665100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.271124754708653050 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.620155815190007290 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.952192925287282500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.141574233416015270 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.306211887293305060 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.112858283622954140 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.991021199405144800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.793685436353725570 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.143020025293004900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.414585437037280350 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.578899089837281440  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.721649662275684940 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.751887564498135760 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.343125968635060700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.371307475407201220 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.791982679827336830 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.556118638801595290  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.197647513234195270 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.397631789642117760 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.351309352399085560 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.049546640622598000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.115788564027392130 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.875118979214747100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.889368121826153770 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.840938104975510470  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.952496479346906620 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.198324295023203150 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.750333948279223500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.495213917794875110 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.998824745012726110 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.268129595206503700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.268712094219876410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.507232064997092500 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.622861707723231460 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.344407974226012200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  8.049537738268005780 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.175298613119103700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.507448297733711580 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.478134582197187190  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.240451271632208470 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.749284613027062280 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.928836225993929800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.641230448460655470 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.238043971517323700 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.079607465599853990  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.354012011535602240 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.636901145282790400 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.940102564970314970 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.016946557556071400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.728204597151975700 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.808104472290878600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.271909792078082190 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.493137892641511800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.589121879100656280 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.411560078289417320 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.929717603960259700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820342903774138540 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.527382211876742170 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.050069680082688530  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.457588004197930780 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.789625632567040990 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.302603441038331590 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.029510776759458700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  13.854035729835340300 ,  1.000000000000000000 ,  0.000000000000000000 ,  79.038284275143510600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.191066320958626080 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.910984062732023200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.016383967716901980 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.218941554665907920 ,  1.000000000000000000 ,  0.000000000000000000 ,  36.007086661233373100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.038719964526692680 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.875226799967498530 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.202352886153809310  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.581625926635480410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.969945305239901860 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.724725482784641000 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.518104889917417200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  18.583344612418216000 ,  1.000000000000000000 ,  0.000000000000000000 ,  106.613310298017552000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.470773056077042010 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.269371874774368100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.531770984687639280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.187817299624750690 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.272993361221956800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.298809562984080570 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.286853528490200290 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.558861842374248850  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.726840708744468820 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.178826565989893100 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.208647904689437170 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.500568939754909800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.806302616310809100 ,  1.000000000000000000 ,  0.000000000000000000 ,  142.892520331996451000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.041217456857369330 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.387535902715491400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.158856549246272570 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.365667389112255490 ,  1.000000000000000000 ,  0.000000000000000000 ,  52.105404771923538500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.593732141422734560 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.752693986126417690 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.092854245498386900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.898462676399391700 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423713156721314820 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.771472817104831070 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.117079265490481500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.806302616310809100 ,  1.000000000000000000 ,  0.000000000000000000 ,  142.892520331996451000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.921723453337277120 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.316371166207016300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.921853371154275790 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.794738089845477450 ,  1.000000000000000000 ,  0.000000000000000000 ,  62.798494795892054300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.962143486853571430 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.331045062338295890 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.986212245364773900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.097161509446128220 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.705537182844490740 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.415324008208610710 ,  1.000000000000000000 ,  0.000000000000000000 ,  40.385027506794827400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.806302616310809100 ,  1.000000000000000000 ,  0.000000000000000000 ,  142.892520331996451000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.483062769060532900 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.027145536076602600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.709847350369401830 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.267907104510111800 ,  1.000000000000000000 ,  0.000000000000000000 ,  73.805730426167926300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.402108813486810050 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.018884358647067500 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.229124136877338500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.329068201111094800 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.032964238582350540 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.159936296064348230 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.465550551491979500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.806302616310809100 ,  1.000000000000000000 ,  0.000000000000000000 ,  142.892520331996451000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.885522948175298200 ,  1.000000000000000000 ,  0.000000000000000000 ,  40.937154903941888000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.654936533642310080 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.035283394139558100 ,  1.000000000000000000 ,  0.000000000000000000 ,  87.017813575907411000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.899844539921180960 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.796950277195386470 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.767382149191075100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.602139153197799090 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.417164687602284090 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.030566513897278820 ,  1.000000000000000000 ,  0.000000000000000000 ,  53.564266186681386700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.12 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.806302616310809100 ,  1.000000000000000000 ,  0.000000000000000000 ,  142.892520331996451000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.834678299431743700 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.280084984573925100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.858881251024411260 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.285422205163984100 ,  1.000000000000000000 ,  0.000000000000000000 ,  103.832151835140749000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.486987124988962350 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.713561751222227870 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.754257001943564400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.906280981237856500 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.844007092943583890 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.995378799876181830 ,  1.000000000000000000 ,  0.000000000000000000 ,  61.422411694036640000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_14Numerator[13][7][24] = {
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  2.876322358586961060 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.818256698432604100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.727005265681594850 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.703254653870896810  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.373786698477926780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.051701977497910170 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.862291835619350500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.183009769150035860 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.460955036374925960 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.370062760891556760  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.086707876733889800 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.211943906505084770 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.855357847106503750 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.732243297887818000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.637418539539408970 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.316819963348919500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.021600184168419910 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.510406040414628670  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.522250128935032310 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.353505899725813140 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.235034023254987900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.260853007322728870 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.600582377264895940 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.877513523021113960  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.133862038502688070 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.293880507250332320 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.081256448993074940 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.721134777463897000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.660326100247671910 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.326117945155417000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.408853216974490060 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.551732565695148840  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.701056446126932190 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.707933811851936490 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.982447043539309600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359674558468911830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.770830553802052340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.477892328239718720  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.191238008856639220 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.386003870493690560 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.318224062572765030 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.752162495826880200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  6.050524637870632110 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.463745437951061500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.867780570349466980 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.775448518309709730  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.932472493389709280 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.156188710494069570 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.406546405649045500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.478002748893206820 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.969113425871412250 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.162497771065988950  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.261641366845050350 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.495053572256651590 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.589507983467372650 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.048234031462737200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  7.957436582992632350 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.602505884565374600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.487052014734800310 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.413674601022931300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.214734868305862750 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.696889552166737310 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.510110825036413500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.625525727173057880 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.211267449702639890 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.985117816011325860  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.342812338073918750 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.619004340598589000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.894064133410947810 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.618455967806834600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.219900669107879300 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.803397455244670800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.238189249839134480 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.394177429345148800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.555792651741977560 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.345249202997892190 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.408443269190097900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.803381006354072950 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.497545509853964510 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.941417815131793570  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.448545445159915790 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.775226981076401640 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.265582264660237040 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.708126250437032900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  13.562626555844660800 ,  1.000000000000000000 ,  0.000000000000000000 ,  77.304975917170310100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.138276276498088710 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.762415789967281600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.972739019057714320 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.133619925324479640 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.345011819855741900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.010260107016430100 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.828872210717200630 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.045017344026209830  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.571191107666022320 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.953785466147298640 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.684182394008347130 ,  1.000000000000000000 ,  0.000000000000000000 ,  26.169603434584409700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  18.075876717564796100 ,  1.000000000000000000 ,  0.000000000000000000 ,  103.621114861871732000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.387026634703120820 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.039993372348373400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.488176646985314380 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.102893240298548070 ,  1.000000000000000000 ,  0.000000000000000000 ,  42.615193903550917300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.264298578179888110 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.231260576597502430 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.371987445565030940  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.714672374192437680 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.160395875078594760 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.163352618289469120 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.114668235440404900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.032169028975982200 ,  1.000000000000000000 ,  0.000000000000000000 ,  138.340742652141387000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.912614610793659690 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.040915927267938900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.097974571564948930 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.247072093303454920 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.185988422405749500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.569810911611028370 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.712785136132155550 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.953524308546297700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.878781052442354490 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.394803888328746580 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.702663780666328290 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.540388390360043000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.032169028975982200 ,  1.000000000000000000 ,  0.000000000000000000 ,  138.340742652141387000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.730494231887851340 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.805955666791287900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.861324085413438740 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.677045325087242130 ,  1.000000000000000000 ,  0.000000000000000000 ,  61.886899177012153900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.921086628957031200 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.264506574537509030 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.760474951272801300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.080114159409633070 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.680529480767373410 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.355791309784168950 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.885353497905811100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.032169028975982200 ,  1.000000000000000000 ,  0.000000000000000000 ,  138.340742652141387000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.192738340765512100 ,  1.000000000000000000 ,  0.000000000000000000 ,  31.257444766951909500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.751373376138801950 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.342582546576370100 ,  1.000000000000000000 ,  0.000000000000000000 ,  74.342497606714033500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.350254022531425240 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.936770933693864550 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.957323853999289600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.301437970551952090 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.993204753067539860 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.067309663015780780 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.697358272887854000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.032169028975982200 ,  1.000000000000000000 ,  0.000000000000000000 ,  138.340742652141387000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.443214344833235100 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.769638076538079500  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.548496179612780740 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.832090836185084700 ,  1.000000000000000000 ,  0.000000000000000000 ,  85.468820522727327200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.849501442693253230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.716258721402237340 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.496443125910840200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.568971269471284740 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.369781315777033550 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.921050557097991880 ,  1.000000000000000000 ,  0.000000000000000000 ,  52.659746840629395600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.14 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  24.032169028975982200 ,  1.000000000000000000 ,  0.000000000000000000 ,  138.340742652141387000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.186903997330091400 ,  1.000000000000000000 ,  0.000000000000000000 ,  49.574765741135337300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.714969969430566900 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.012395481771076100 ,  1.000000000000000000 ,  0.000000000000000000 ,  101.762343685090528000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.441775097496063470 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.641000617141779470 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.510121642026067700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.876553418657378900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.801560911529541900 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.897259999144918830 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.611451554685515900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_16Numerator[13][7][24] = {
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  2.809335630508651430 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.395630875124320300  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.721443433339432390 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.677285735707452650  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.361805167703428190 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.024918662337421350 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.638603534205533000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.179311684547075470 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.451943652892912160 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.330426066118048030  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.086707876733889800 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.211943906505084770 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.855357847106503750 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.732243297887818000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.622723959239945830 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.196880425258580500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.012758947056003670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.476843655057813900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.504142860303578020 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.315565832697339400 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.929477148250162700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.256681785491179810 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.591329870973701380 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.838612100298803840  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.130561000703282250 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.286530181992914690 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.057486044329716270 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.498092941974341800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.628328181336367030 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.106754824554098300  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.355967466434823800 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.402057640740907550  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.690286981431620370 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.682806211177978020 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.764257268480157800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.351532585437660310 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.755401502324989820 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.419023710611786450  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.187623444383463900 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.378626365938348640 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.295516964245960970 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.541782882856438200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.988910151996842400 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.072821808513552400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.846953340046186830 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.711777076225760470  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.905895668480408700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.103413719657913460 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.994614761899534000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.468578846678048720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.952034042417631680 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.099252645527644570  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.252791103390753720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.481699689295918000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.557137688542686420 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.777510553893680600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  7.845344068146752110 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.918131338227191000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.388169873797016510 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.145029068340866370  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.181658932912602820 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.632304040233734540 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.011425264394308200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.616203143907388730 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.193165479902903230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.914119424985725180  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.338225759844424930 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.610822094518781220 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.870968015352753700 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.409899004483410300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.391228793958935800 ,  1.000000000000000000 ,  0.000000000000000000 ,  58.772423282077085100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.191149455000441120 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.262634454301689900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.523444507869128990 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.280697896658949730 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.899903634000498400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790068175371274830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.474883753161547520 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.861337738322092150  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.439632629380966260 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.760976617306470700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.228812974564044590 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.388484992176373100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  13.246527918226336200 ,  1.000000000000000000 ,  0.000000000000000000 ,  75.432605404257685700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.069912931204591190 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.575322077485166600  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.930465799975320080 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.050794053538058210 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.701198991533111400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.001673805254073010 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.812602156377967510 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.981835676090250690  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.560912590624483620 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.937817286269033930 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.644004686047631840 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.823822069721970300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  17.546341695212650100 ,  1.000000000000000000 ,  0.000000000000000000 ,  100.505109634817074000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.284875267869400870 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.764648888446494400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.445850688893493000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.020283747983620870 ,  1.000000000000000000 ,  0.000000000000000000 ,  41.974353644836668300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.253026258938051640 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.210971331570965590 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.296184354372185150  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.696238615326283390 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.134086493706781160 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.102934427092373330 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.619826034650198900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.167284410461732600 ,  1.000000000000000000 ,  0.000000000000000000 ,  133.270345508257975000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.762715536764917880 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.640737194185852800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.058729874830949490 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.170460097887621840 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.590990407080042200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.546518473716179900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.673810367116038480 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.817083370193307400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.864733154746886830 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.373853283412785680 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.651945069712994930 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.111234338164976500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.167284410461732600 ,  1.000000000000000000 ,  0.000000000000000000 ,  133.270345508257975000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.515366802587632120 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.235149112982121300  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.785564970155448350 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.532299569354254490 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.783408059038393400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.901646486437657660 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.231757686717241280 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.644873132645729500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.057265498987494560 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.647309681538832390 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.277547804482930260 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.232876511834788900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.167284410461732600 ,  1.000000000000000000 ,  0.000000000000000000 ,  133.270345508257975000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.918479426583113300 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.529587406896943900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.678517968850159470 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.203440090814334300 ,  1.000000000000000000 ,  0.000000000000000000 ,  73.281643524772377400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.312113664759053220 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.874941622685353690 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.747309787321459100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.281498518505028720 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.964237261020675530 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.999053795939302880 ,  1.000000000000000000 ,  0.000000000000000000 ,  45.127420332704176100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.167284410461732600 ,  1.000000000000000000 ,  0.000000000000000000 ,  133.270345508257975000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.974740798329234400 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.535771109144235400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.422955002687873000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.594598779738250400 ,  1.000000000000000000 ,  0.000000000000000000 ,  83.673724400496865900  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.801814038475817450 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.640759465173635690 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.246455267649963600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.544915821428241910 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.335156916467617360 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.840285994128254110 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.988925898733477000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.16 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.167284410461732600 ,  1.000000000000000000 ,  0.000000000000000000 ,  133.270345508257975000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.582152612859502500 ,  1.000000000000000000 ,  0.000000000000000000 ,  47.981871268524798800  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.771180858067342710 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.114489100421730300 ,  1.000000000000000000 ,  0.000000000000000000 ,  102.503779075113471000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.397796486826555550 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.570326989732062200 ,  1.000000000000000000 ,  0.000000000000000000 ,  20.272006321446447400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.858175761877547400 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.774132872867876460 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.830406209003958120 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.040892806098568700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_18Numerator[13][7][24] = {
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  2.796322118133908850 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.289592352948933000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.709852262923788000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.638359822421037390  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.353377915807332910 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.005183958713158350 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.469279004576197400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.171150591504419800 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437391514095418320 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.277898142834044130  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.086061225788628400 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.208381853276754290 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.840117108282297130 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.578132861003309600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.523833067166227910 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.589658650100190600  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.996982933024120440 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.427830833759056620  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.499755794848683040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.302081489810410720 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.796777893635450600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.249596850192919550 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.577012396552727620 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.781777308290512170  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.125481299647879800 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.278320822106456590 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.036232781063313180 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.315899535404447800  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.582813813891928770 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.814180380094924800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.334664230487217380 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.339161637107125100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.673507979342997890 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.648143331688583400 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.486643546509224500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.343500390290167210 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.740087268422112480 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.360361989033616940  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.182198367495547410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.370188923447392740 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.274459369801475890 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.363718057045385700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.930804976857355190 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.701502524799572300  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.777174931235320890 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.518483386662858870  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.892416482513168410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.073587164784761240 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.742940743148100300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.459289464784154560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.935122196584825540 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.036429137151707810  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.248806928587912870 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.474083904548684210 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.534621340207212190 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.571234452068413400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  7.713403794110566200 ,  1.000000000000000000 ,  0.000000000000000000 ,  43.123442314386181800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.346770917872313070 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.030359690156590350  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.164069746490807410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.595151078084007690 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.706626683985145800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.605252397568592170 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.173932721449567840 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.844491931718863500  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.328598387058218980 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.596564281527701690 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.837025312767654130 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.127981504964427700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  10.170493712486802400 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.460653534607018900  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.130987521089672930 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.099150565860856700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.499329293974457130 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.233395200570343240 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.532565175062917000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.772050992084196920 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444672927786013970 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.756127264237207440  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.425391026252294860 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.740208877309182830 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.180109769162349220 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.986312189339045600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  13.469717384890518700 ,  1.000000000000000000 ,  0.000000000000000000 ,  76.703562071030802400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.004125857613781920 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.394825516853734000  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.910151434702693770 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.009189599661294070 ,  1.000000000000000000 ,  0.000000000000000000 ,  34.365980561407134500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.980017194888906040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.776926222051410600 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.859405785429639300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.544885181643149700 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.914720686439318250 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.590462103136958570 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.383670965577440600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  17.841481039232579800 ,  1.000000000000000000 ,  0.000000000000000000 ,  102.195662152460358000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.186836958103362430 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.499930434696587200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.404748167667378760 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.939903906559720780 ,  1.000000000000000000 ,  0.000000000000000000 ,  41.349832897615236500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233616275482096740 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.179094972325209320 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.186898427673657610  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.684479525378073110 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.116189500258100600 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.058748172752024710 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.242602967941742500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.478251192629201700 ,  1.000000000000000000 ,  0.000000000000000000 ,  135.059189196918140000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.619338544967858340 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.257495813900966700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.003772268844315360 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.064366390955657770 ,  1.000000000000000000 ,  0.000000000000000000 ,  49.775171355004218300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.522635867618791660 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.635085393157754030 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.685845460053004200  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.849073077801474120 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.351629294213605890 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.601192136418823960 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.696462936939710200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.478251192629201700 ,  1.000000000000000000 ,  0.000000000000000000 ,  135.059189196918140000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.617044087622964810 ,  1.000000000000000000 ,  0.000000000000000000 ,  24.494115920605803900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.733696677738602250 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.432189188767303280 ,  1.000000000000000000 ,  0.000000000000000000 ,  60.013203809123368200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.871905464804748400 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.184013219102238780 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.484591083686241900  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.039143410233686730 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.621718245572626670 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.219386625646624370 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.758468883361516100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.478251192629201700 ,  1.000000000000000000 ,  0.000000000000000000 ,  135.059189196918140000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.045835381953629900 ,  1.000000000000000000 ,  0.000000000000000000 ,  30.855813985985626900  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.607825307796520780 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.068292181070972900 ,  1.000000000000000000 ,  0.000000000000000000 ,  72.250335027332283700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.275027805036627720 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.815850621340209690 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.550386001744357100  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.268925418824788930 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.945615985347184830 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.954182944991679880 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.747783917781113400  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.478251192629201700 ,  1.000000000000000000 ,  0.000000000000000000 ,  135.059189196918140000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.134470237016710600 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.946816426826089200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.302155664612925530 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.365911043629767000 ,  1.000000000000000000 ,  0.000000000000000000 ,  81.944062639976962700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.770281980130861580 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.590455866725195120 ,  1.000000000000000000 ,  0.000000000000000000 ,  17.078460653809571600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.521313424766624680 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.301150289210586840 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.760873233757204840 ,  1.000000000000000000 ,  0.000000000000000000 ,  51.328933136165979100  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.18 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  23.478251192629201700 ,  1.000000000000000000 ,  0.000000000000000000 ,  135.059189196918140000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.780360838726640100 ,  1.000000000000000000 ,  0.000000000000000000 ,  48.493674275208647400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.604987689994364250 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.801179792064381500 ,  1.000000000000000000 ,  0.000000000000000000 ,  100.142892254851020000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.338778323429993480 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.477244487457882730 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.965009863705265800  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.818867012938620850 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.719095628023326940 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.706372012845448260 ,  1.000000000000000000 ,  0.000000000000000000 ,  59.032542507548221500  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;

 const double Elliptic_20Numerator[13][7][24] = {
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 40.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  2.774426156713529770 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.138134840486630500  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.692283613259890410 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.586735480808226570  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.345061626878602650 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.985577477260638050 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.300527273497525700  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.170229639378825180 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.433208044233013960 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.255345024940251350  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.083643242816578760 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.204249043743442990 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.828835684015641010 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.479524322353375600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 45.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  3.488502147521610920 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.361249396550395100  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.952186477803828480 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.300695645546717570  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.486026008415732180 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.272531965686118040 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.554689182495211600  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.240774946917951430 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.561821056892536140 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.728385065929045620  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.124760693145729240 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.275032455476445610 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022713375152437100 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.179869123991313300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 50.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  4.436517511450549730 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.931688159711356400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.313890958106286090 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.277541152390561760  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.661551116586332370 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.622750472708833770 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.279335808480020400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.333728125935911500 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.723737949592880850 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.304161922388041180  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.174122876576322530 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.357587217633964060 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.242907651813182300 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.096591734793255700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 55.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  5.836173910274011330 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.129313657687710300  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.748561732344737770 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.436790926219110530  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.877663633204136760 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.043289141649394570 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.500421725090284000  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.453790060056969980 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.924372896441267630 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.994319006455936180  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.242988918159127150 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.465263080818443920 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.513140122502303340 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.391263069765354700  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 60.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  7.587725651053271570 ,  1.000000000000000000 ,  0.000000000000000000 ,  42.364694997928424400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.318497497925789740 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.947296984604883540  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.145528283439706920 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.558028806699010450 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.414268950534815200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588431757254168410 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.146766600916401040 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.753628898971716590  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.324127446489907990 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588494162302376060 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.814057237470860340 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.920014516703876200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.283168298504414830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.476842998586931580 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.204727570645369280 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.424586176068506130  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.172625538505021710 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.282915884279580480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.655253195467236700 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.167024867994393310 ,  1.000000000000000000 ,  0.000000000000000000 ,  22.606671204590824700  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 65.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  9.961179770361525240 ,  1.000000000000000000 ,  0.000000000000000000 ,  56.214907892021116700  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.087358466839514650 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.976268335974578960  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.476683808784160500 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.187020517436248040 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.159834608346837800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.764072357087602900 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.430459809686725150 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.703862766636818020  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.420257749564734830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.731405028175527990 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.155962187908379240 ,  1.000000000000000000 ,  0.000000000000000000 ,  21.770381950773199300  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.364590452918162720 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.588935910278457040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.423579867426438380 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.231223316916885580  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.229252644182818880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.359061862235081410 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.790655411373016200 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.522006583045827810 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.690572500825961600  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 70.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  13.125253922429085000 ,  1.000000000000000000 ,  0.000000000000000000 ,  74.670500865312746400  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.940808794276740910 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.220644364076806100  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.879680348907109890 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.950147410840110670 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.911523515225923100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.970206552194824660 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.759998384772889060 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.798691509755122380  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.538877466204365830 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.904854554887213110 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.564308318438621730 ,  1.000000000000000000 ,  0.000000000000000000 ,  25.152752549545581900  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.454417717900519100 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.711008482321693780 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.658513205756848130 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.090453878222788830  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.288817303552933560 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.437777836175971660 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.927756757568533260 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.876767091098892060 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.758763438081057700  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 75.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  17.283136788873275000 ,  1.000000000000000000 ,  0.000000000000000000 ,  98.916347584293134800  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.070939233387788290 ,  1.000000000000000000 ,  0.000000000000000000 ,  15.190832273244618400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.365268750369887480 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.864169778386632000 ,  1.000000000000000000 ,  0.000000000000000000 ,  40.771302877711363500  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.214548933270649830 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.147731893343491900 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.079220259882925250  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.677376274936230430 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.104921947877096860 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.029747742951855120 ,  1.000000000000000000 ,  0.000000000000000000 ,  28.989601787442620000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.553928350836426330 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.844911900507063550 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.913367233180857770 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.016862573114737600  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.358272726531888570 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.527429078309586520 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.079374100531862450 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.261013473632052850 ,  1.000000000000000000 ,  0.000000000000000000 ,  32.051963442992196500  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 80.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  22.593666323358515800 ,  1.000000000000000000 ,  0.000000000000000000 ,  129.878692729828344000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.673859577158253840 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.395294986752990200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  3.968689885557672440 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.997053145160855970 ,  1.000000000000000000 ,  0.000000000000000000 ,  49.260415641842691100  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.499200285986628690 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.597038324418026180 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.556756146808215400  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.835450520120350590 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.331243054445449660 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.551670952518025000 ,  1.000000000000000000 ,  0.000000000000000000 ,  33.276770523026620200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.687632837200602200 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.023043979131277310 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.248473784303752330 ,  1.000000000000000000 ,  0.000000000000000000 ,  10.226563408002235200  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.428212895699493950 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.617691452180100460 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.232116519418025020 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.648907079892681260 ,  1.000000000000000000 ,  0.000000000000000000 ,  35.383852773058663400  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 85.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  22.593666323358515800 ,  1.000000000000000000 ,  0.000000000000000000 ,  129.878692729828344000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.410431676457182700 ,  1.000000000000000000 ,  0.000000000000000000 ,  23.945346011250585200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  4.687129853336124970 ,  1.000000000000000000 ,  0.000000000000000000 ,  8.343513660555370140 ,  1.000000000000000000 ,  0.000000000000000000 ,  59.339305209435934800  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.842757199429012620 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.137175066820844020 ,  1.000000000000000000 ,  0.000000000000000000 ,  12.327204144976464700  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.022999939603876030 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.597926893962860360 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.162477335007618070 ,  1.000000000000000000 ,  0.000000000000000000 ,  38.279677493229954200  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.820713633573717290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.198709186482235900 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.575225875886664630 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.397575058253188700  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.529439873045173480 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.746036924979732290 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.444336684937315600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.178588326052775410 ,  1.000000000000000000 ,  0.000000000000000000 ,  39.896861662135236800  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 90.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  22.593666323358515800 ,  1.000000000000000000 ,  0.000000000000000000 ,  129.878692729828344000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.700831302541900000 ,  1.000000000000000000 ,  0.000000000000000000 ,  29.947277097655739200  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  5.566377448195615860 ,  1.000000000000000000 ,  0.000000000000000000 ,  9.987765224412491700 ,  1.000000000000000000 ,  0.000000000000000000 ,  71.626824986848035300  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.250773534702843910 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.776798893431893230 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.418743413262152600  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.241077022595995950 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.906422165358240050 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.865403212309877820 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.024578835185863600  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.022941828439897450 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464875970526197340 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.068840816321629640 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.164483238662450400  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.635877531896907880 ,  1.000000000000000000 ,  0.000000000000000000 ,  1.880807508933482810 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.666957278406201230 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.734532579905399000 ,  1.000000000000000000 ,  0.000000000000000000 ,  44.639316805526092900  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 95.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  22.593666323358515800 ,  1.000000000000000000 ,  0.000000000000000000 ,  129.878692729828344000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.689208619854696300 ,  1.000000000000000000 ,  0.000000000000000000 ,  37.773471318917337700  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  6.325261623841621270 ,  1.000000000000000000 ,  0.000000000000000000 ,  11.407582543216127800 ,  1.000000000000000000 ,  0.000000000000000000 ,  82.244498914667005400  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.738410920851101600 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.538500734633815090 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.900799102169799000  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.506273901819159100 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.279144296088427170 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.708527495645496330 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.889024769279402000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.233613737008687040 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.741136762218354670 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.579015167232970640 ,  1.000000000000000000 ,  0.000000000000000000 ,  14.986401295457184800  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.761144978060678270 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.037842996742150080 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.922903662258107720 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.367145270629263720 ,  1.000000000000000000 ,  0.000000000000000000 ,  50.009244990277345300  } },
/* Elliptic 0.20 dB Ripple  Stop Band Attenuation = 100.00 db */
/* 4 Zeros*/  {{ 1.000000000000000000 ,  0.000000000000000000 ,  22.593666323358515800 ,  1.000000000000000000 ,  0.000000000000000000 ,  129.878692729828344000  },
/* 5 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  18.080987955857256800 ,  1.000000000000000000 ,  0.000000000000000000 ,  46.657543181462287400  },
/* 6 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  7.445569730902943380 ,  1.000000000000000000 ,  0.000000000000000000 ,  13.500470091095117200 ,  1.000000000000000000 ,  0.000000000000000000 ,  97.875771492271866200  },
/* 7 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  4.299477204428049150 ,  1.000000000000000000 ,  0.000000000000000000 ,  6.414888439155420840 ,  1.000000000000000000 ,  0.000000000000000000 ,  19.757945682122173300  },
/* 8 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  2.800467814514622230 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.692465155170386650 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.643784591801686010 ,  1.000000000000000000 ,  0.000000000000000000 ,  58.509987044721548000  },
/* 9 Zeros*/  { 0.000000000000000000 ,  0.000000000000000000 ,  1.000000000000000000 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.464807475001371980 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.042846757347944300 ,  1.000000000000000000 ,  0.000000000000000000 ,  5.132764148627376950 ,  1.000000000000000000 ,  0.000000000000000000 ,  16.955585240729956100  },
/* 10 Zeros*/  { 1.000000000000000000 ,  0.000000000000000000 ,  1.929496811550269750 ,  1.000000000000000000 ,  0.000000000000000000 ,  2.247893747342876840 ,  1.000000000000000000 ,  0.000000000000000000 ,  3.263158005911955590 ,  1.000000000000000000 ,  0.000000000000000000 ,  7.204465075392611160 ,  1.000000000000000000 ,  0.000000000000000000 ,  57.103819080183541200  } } } ;


 int j, n, p, z, MinNumPoles, MaxNumPoles;
 int ArrayNumber, OuterArrayDim, NumSections;
 double LoopBeta, BetaMin, BetaStep, BetaMax;
 TSPlaneCoeff SPlaneCoeff;

 BetaMin =  BetaMinArray[FilterType];
 BetaStep = BetaStepArray[FilterType];
 BetaMax =  BetaMaxArray[FilterType];
 MinNumPoles = MinNumPolesArray[FilterType];
 MaxNumPoles = MaxNumPolesArray[FilterType];

 // Range check
 if(NumPoles < MinNumPoles)NumPoles = MinNumPoles;
 if(NumPoles > MaxNumPoles)NumPoles = MaxNumPoles;
 if(Beta < BetaMin)Beta = BetaMin;
 if(Beta > BetaMax)Beta = BetaMax;

 // Need the array number that corresponds to Beta.
 // The max array size was originally determined with this piece of code,
 // so we use it here so we can range check ArrayNumber.
 OuterArrayDim = 0;
 for(LoopBeta=BetaMin; LoopBeta<=BetaMax; LoopBeta+=BetaStep)OuterArrayDim++;

 ArrayNumber = 0;
 for(LoopBeta=BetaMin; LoopBeta<=BetaMax; LoopBeta+=BetaStep)
  {
   if( LoopBeta >= Beta - BetaStep/1.9999 &&  LoopBeta <= Beta + BetaStep/1.9999)break;
   ArrayNumber++;
  }
 if(ArrayNumber > OuterArrayDim-1)ArrayNumber = OuterArrayDim-1; // i.e. If the array is dimensioned to N, the highest we can access is N-1

 // n is the array location for the given pole count.
 // The arrays start at 0. MinNumPoles is either 2 or 4.
 n = NumPoles - MinNumPoles;
 if(n < 0)n = 0;

 // NumSections is the number of biquad sections for a given pole count.
 NumSections = (NumPoles + 1) / 2;
 p = z = 0;

 switch(FilterType)
  {
   case ftBUTTERWORTH:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = ButterworthDenominator[n][p++];
     SPlaneCoeff.B[j] = ButterworthDenominator[n][p++];
     SPlaneCoeff.C[j] = ButterworthDenominator[n][p++];
     SPlaneCoeff.D[j] = 0.0;
     SPlaneCoeff.E[j] = 0.0;
     SPlaneCoeff.F[j] = 1.0;
    }
   break;

   case ftGAUSSIAN:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = GaussDenominator[n][p++];
     SPlaneCoeff.B[j] = GaussDenominator[n][p++];
     SPlaneCoeff.C[j] = GaussDenominator[n][p++];
     SPlaneCoeff.D[j] = 0.0;
     SPlaneCoeff.E[j] = 0.0;
     SPlaneCoeff.F[j] = 1.0;
    }
   break;

   case ftBESSEL:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = BesselDenominator[n][p++];
     SPlaneCoeff.B[j] = BesselDenominator[n][p++];
     SPlaneCoeff.C[j] = BesselDenominator[n][p++];
     SPlaneCoeff.D[j] = 0.0;
     SPlaneCoeff.E[j] = 0.0;
     SPlaneCoeff.F[j] = 1.0;
    }
   break;

   case ftADJGAUSS:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = AdjGaussDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = AdjGaussDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = AdjGaussDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = 0.0;
     SPlaneCoeff.E[j] = 0.0;
     SPlaneCoeff.F[j] = 1.0;
    }
   break;

   case ftCHEBYSHEV:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = ChebyshevDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = ChebyshevDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = ChebyshevDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = 0.0;
     SPlaneCoeff.E[j] = 0.0;
     SPlaneCoeff.F[j] = 1.0;
    }
   break;

   case ftINVERSE_CHEBY:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = InvChebyDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = InvChebyDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = InvChebyDenominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = InvChebyNumerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = InvChebyNumerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = InvChebyNumerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_00:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_00Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_00Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_00Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_00Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_00Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_00Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_02:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_02Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_02Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_02Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_02Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_02Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_02Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_04:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_04Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_04Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_04Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_04Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_04Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_04Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_06:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_06Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_06Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_06Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_06Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_06Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_06Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_08:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_08Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_08Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_08Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_08Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_08Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_08Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_10:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_10Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_10Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_10Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_10Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_10Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_10Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_12:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_12Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_12Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_12Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_12Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_12Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_12Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_14:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_14Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_14Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_14Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_14Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_14Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_14Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_16:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_16Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_16Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_16Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_16Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_16Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_16Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_18:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_18Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_18Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_18Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_18Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_18Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_18Numerator[ArrayNumber][n][z++];
    }
   break;

   case ftELLIPTIC_20:
   for(j=0; j<NumSections; j++)
    {
     SPlaneCoeff.A[j] = Elliptic_20Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.B[j] = Elliptic_20Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.C[j] = Elliptic_20Denominator[ArrayNumber][n][p++];
     SPlaneCoeff.D[j] = Elliptic_20Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.E[j] = Elliptic_20Numerator[ArrayNumber][n][z++];
     SPlaneCoeff.F[j] = Elliptic_20Numerator[ArrayNumber][n][z++];
    }
   break;

 }

 SPlaneCoeff.NumSections = NumSections;
 return(SPlaneCoeff);

}


// This function calculates the frequency response of an IIR filter.
// Probably the easiest way to determine the frequency response of an IIR filter is to send
// an impulse through the filter and do an FFT on the output. This method does a DFT on
// the coefficients of each biquad section. The results from the cascaded sections are
// then multiplied together. A clearer example, which uses complex variables, is given just below.

// This approach works better than an FFT when the filter is very narrow. To analyze highly selective
// filters with an FFT can require a very large number of points, which can be quite cumbersome.
// This approach allows you to set the range of frequencies to be analyzed by modifying the statement
// Arg = M_PI * (double)j / (double)NumPts; .
void IIRFreqResponse(TIIRCoeff IIRCoeff, int NumSections, double *RealHofZ, double *ImagHofZ, int NumPts)
{
 int j, n;
 double Arg, MagSq, RealDFT, ImagDFT, TempR,  TempI;
 double RealZ, ImagZ, RealZ2, ImagZ2;
 double RealNumerator, ImagNumerator, RealDenominator, ImagDenominator; // Real and Imag parts of the numerator and denominator.

 for(j=0; j<NumPts; j++)
  {
   // We evaluate from 0 to Pi. i.e. The positive frequencies.
   Arg = M_PI * (double)j / (double)NumPts;
   RealZ = cos(Arg);
   ImagZ = -sin(Arg);
   RealZ2 = RealZ * RealZ - ImagZ * ImagZ;  // Z2 is Z^2
   ImagZ2 = RealZ * ImagZ + RealZ * ImagZ;

   RealNumerator = RealDenominator = 1.0;
   ImagNumerator = ImagDenominator = 0.0;
   for(n=0; n<NumSections; n++)
	{
     // These 1st 2 lines are a DFT of the numerator coeff.
     RealDFT = IIRCoeff.b0[n] + IIRCoeff.b1[n] * RealZ + IIRCoeff.b2[n] * RealZ2;
     ImagDFT = IIRCoeff.b1[n] * ImagZ + IIRCoeff.b2[n] * ImagZ2;
     TempR = RealNumerator;
     TempI = ImagNumerator;
     RealNumerator = RealDFT * TempR - ImagDFT * TempI;  // Num *= the DFT
     ImagNumerator = RealDFT * TempI + ImagDFT * TempR;

     // These next 2 lines are a DFT of the denominator coeff.
     RealDFT = IIRCoeff.a0[n] + IIRCoeff.a1[n] * RealZ + IIRCoeff.a2[n] * RealZ2;
     ImagDFT = IIRCoeff.a1[n] * ImagZ + IIRCoeff.a2[n] * ImagZ2;
     TempR = RealDenominator;
     TempI = ImagDenominator;
     RealDenominator = RealDFT * TempR - ImagDFT * TempI; // Denom *= the DFT
     ImagDenominator = RealDFT * TempI + ImagDFT * TempR;
    }

   // HofZ[j] =  Numerator / Denominator;
   MagSq = RealDenominator * RealDenominator + ImagDenominator * ImagDenominator; // Magnitude Squared
   if(MagSq > 0.0)
    {
     RealHofZ[j] = (RealNumerator * RealDenominator + ImagNumerator * ImagDenominator) / MagSq;
     ImagHofZ[j] = (ImagNumerator * RealDenominator - RealNumerator * ImagDenominator) / MagSq;
     if( (RealHofZ[j] * RealHofZ[j] + ImagHofZ[j] * ImagHofZ[j]) < 1.0E-12)RealHofZ[j] = 1.0E-12; // This happens in the nulls. Need to prevent a problem when plotting dB.
    }
   else
    {
     RealHofZ[j] = 1.0E-12;
     ImagHofZ[j] = 0.0;
    }
 }

}


//---------------------------------------------------------------------------

/*
 By Iowa Hills Software, LLC  IowaHills.com
 If you find a problem, please leave a note at:
 http://www.iowahills.com/feedbackcomments.html
 December 5, 2014

 This root solver code is needed for the IIR band pass and IIR notch filters to
 reduce the 4th order terms to biquads.

 It is composed of the functions: QuadCubicRoots, QuadRoots, CubicRoots, and BiQuadRoots.
 This code originated at: http://www.netlib.org/toms/  Algorithm 326
 Roots of Low Order Polynomials by  Terence R.F.Nonweiler  CACM  (Apr 1968) p269
 Original C translation by: M.Dow  Australian National University, Canberra, Australia

 We made extensive modifications to the TOMS code in order to improve numerical accuracy.

 The polynomial format is: Coeff[0]*x^4 + Coeff[1]*x^3 + Coeff[2]*x^2 + Coeff[3]*x + Coeff[4]
 The roots are returned in RootsReal and RootsImag. N is the polynomial's order.  2 <= N <= 4
 N is modified if there are leading or trailing zeros.  N is returned.
 Coeff needs to be length N+1. RealRoot and ImagRoot need to be length N.

 Do not call QuadRoots, CubicRoots, and BiQuadRoots directly. They assume that QuadCubicRoots
 has removed leading and trailing zeros and normalized P.

 Modifications by JRH: 4 October 2015
 - remove requirement for vcl.h header for "ShowMessage" - just print to stderr instead
*/

//---------------------------------------------------------------------------
int QuadCubicRoots(int N, double *Coeff, double *RootsReal, double *RootsImag)
{
 if(N <= 1 || N > 4)
  {
   fprintf(stderr,"Invalid Poly Order in QuadCubicRoots()");
   return(0);
  }

 int j, k;
 long double P[5], RealRoot[4], ImagRoot[4];

 // Must init to zero, in case N is reduced.
 for(j=0; j<4; j++)RealRoot[j] = ImagRoot[j] = 0.0;
 for(j=0; j<5; j++)P[j] = 0.0;

 // Reduce the order if there are trailing zeros.
 for(k=N; k>=0; k--)
  {
   if(fabs(Coeff[k]) > ZERO_PLUS)break;  // break on the 1st nonzero coeff
   Coeff[k] = 0.0;
   N--;
  }

 // Mandatory to remove leading zeros.
 while( fabs(Coeff[0]) < ZERO_PLUS && N>0)
  {
   for(k=0; k<N; k++)
    {
     Coeff[k] = Coeff[k+1];
    }
   Coeff[N] = 0.0;
   N--;
  }

 // The functions below modify the coeff array, so we pass P instead of Coeff.
 for(j=0; j<=N; j++)P[j] = Coeff[j];

 // Mandatory to normalize the coefficients
 if(P[0] != 1.0)
  {
   for(k=1; k<=N; k++)
    {
     P[k] /= P[0];
    }
   P[0] = 1.0;
  }

 if(N==4)BiQuadRoots(P, RealRoot, ImagRoot);
 if(N==3)CubicRoots(P, RealRoot, ImagRoot);
 if(N==2)QuadRoots(P, RealRoot, ImagRoot);
 if(N==1)
  {
   RealRoot[0] = -P[1]/P[0];
   ImagRoot[0] = 0.0;
  }

 // We used separate long double arrays in the function calls for interface reasons.
 // if N==0 all zeros are returned (we init RealRoot and ImagRoot to 0).
 // for(j=0; j<4; j++)Roots[j] = ComplexD(RealRoot[j], ImagRoot[j]);
 for(j=0; j<4; j++)RootsReal[j] = RealRoot[j];
 for(j=0; j<4; j++)RootsImag[j] = ImagRoot[j];
 return(N);
}

//---------------------------------------------------------------------------
// This function is the quadratic formula with P[0] = 1
// y = P[0]*x^2 + P[1]*x + P[2]
void QuadRoots(long double *P, long double *RealRoot, long double *ImagRoot)
{
 long double g;

 g = P[1]*P[1] - 4.0*P[2];
 if(fabsl(g) < ZERO_PLUS)g = 0.0;

 if(g >= 0.0)  // 2 real roots
  {
   RealRoot[0] = (-P[1] + sqrtl(g)) / 2.0;
   RealRoot[1] = (-P[1] - sqrtl(g)) / 2.0;
   ImagRoot[0] = 0.0;
   ImagRoot[1] = 0.0;
  }
 else // 2 complex roots
  {
   RealRoot[0] = -P[1] / 2.0;
   RealRoot[1] = -P[1] / 2.0;
   ImagRoot[0] = sqrtl(-g) / 2.0;
   ImagRoot[1] = -sqrtl(-g) / 2.0;
  }
}

//---------------------------------------------------------------------------
// This finds the roots of y = P0x^3 + P1x^2 + P2x+ P3   P[0] = 1
// The return value indicates the location of the largest positive root which is used by BiQuadRoots.
int CubicRoots(long double *P, long double *RealRoot, long double *ImagRoot)
{
 long double  s, t, b, c, d;

 s = P[1] / 3.0;
 b = (6.0*P[1]*P[1]*P[1] - 27.0*P[1]*P[2] + 81.0*P[3]) / 162.0;
 t = (P[1]*P[1] - 3.0*P[2]) / 9.0;
 c = t * t * t;
 d = 2.0*P[1]*P[1]*P[1] - 9.0*P[1]*P[2] + 27.0*P[3];
 d = d * d / 2916.0 - c;

 if(d > ZERO_PLUS) // 1 complex and 1 real root
  {
   d = powl( (sqrtl(d) + fabsl(b)), 1.0/3.0);
   if(d != 0.0)
    {
     if(b>0) b = -d;
     else b = d;
     c = t / b;
    }
   d = M_SQRT3_2 * (b-c);
   b = b + c;
   c = -b/2.0 - s;

   RealRoot[0] = c;
   ImagRoot[0] = -d;
   RealRoot[1] = c;
   ImagRoot[1] = d;
   RealRoot[2] = b-s;
   if( fabsl(RealRoot[2]) < ZERO_PLUS)RealRoot[2] = 0.0;
   ImagRoot[2] = 0.0;
   return(2); // Return 2 because it contains the real root.
  }

 else // d < 0.0  3 real roots
  {
   if(b == 0.0)d = M_PI_2 / 3.0; //  b can be as small as 1.0E-25
   else d = atanl(sqrtl(fabsl(d))/fabsl(b)) / 3.0;

   if(b < 0.0) b =  2.0 * sqrtl(fabsl(t));
   else        b = -2.0 * sqrtl(fabsl(t));

   c = cosl(d) * b;
   t = -M_SQRT3_2 * sinl(d) * b - 0.5 * c;

   RealRoot[0] = t - s;
   RealRoot[1] = c - s;
   RealRoot[2] = -(t + c + s);
   ImagRoot[0] = 0.0;
   ImagRoot[1] = 0.0;
   ImagRoot[2] = 0.0;

   if( fabsl(RealRoot[0]) < ZERO_PLUS)RealRoot[0] = 0.0;
   if( fabsl(RealRoot[1]) < ZERO_PLUS)RealRoot[1] = 0.0;
   if( fabsl(RealRoot[2]) < ZERO_PLUS)RealRoot[2] = 0.0;

   int MaxK = 0;
   if(RealRoot[1] > RealRoot[MaxK])MaxK = 1;
   if(RealRoot[2] > RealRoot[MaxK])MaxK = 2;
   return(MaxK);  // Return the index with the largest real root.
 }

}

//---------------------------------------------------------------------------
// This finds the roots of  y = P0x^4 + P1x^3 + P2x^2 + P3x + P4    P[0] = 1
// This function calls CubicRoots
void BiQuadRoots(long double *P, long double *RealRoot, long double *ImagRoot)
{
 int k, MaxK;
 long double a, b, c, d, e, g, P1, P3Limit;

 P1 = P[1];

 e = P[1]*0.25;
 d = P[1]*P[1]*0.1875;   // 0.1875 = 3/16
 b = P[3] + P[1]*P[1]*P[1]*0.125 - P[1]*P[2]*0.5;
 c = 256.0*P[4] + 16.0*P[1]*P[1]*P[2];
 c += -3.0*P[1]*P[1]*P[1]*P[1] - 64.0*P[1]*P[3];
 c *= 0.00390625;             // 0.00390625 = 1/256
 a = P[2] - P[1]*P[1]*0.375;  // 0.375 = 3/8

 P[1] = P[2]*0.5 - P[1]*P[1]*0.1875;  // 0.1875 = 3/16
 P[2] = 16.0*P[2]*P[2]  - 16.0*P1*P1*P[2] + 3.0*P1*P1*P1*P1;
 P[2] += -64.0*P[4] + 16.0*P1*P[3];
 P[2] *= 3.90625E-3;    // 3.90625E-3 = 1/256
 P[3] = -b*b*0.015625;  // 0.015625 = 1/64
 if(P[3] > 0.0)P[3] = 0.0; // Only numerical errors make P[3] > 0


/* Any inaccuracies in this algorithm are directly attributable to the magnitude of P[3]
 which takes on values of:  -1E30 < P[3] <= 0.0
 If P[3] = 0 we use the quadratic formula to get the roots (i.e. the 3rd root is at 0).
 Inaccuracies are caused in CubicRoots when P[3] is either huge or tiny because of the loss of
 significance that ocurrs there in the calculation of b and d. P[3] can also cause problems when it
 should have calculated to zero (just above) but it is instead ~ -1.0E-17 because of round off errors.
 Consequently, we need to determine whether a tiny P[3] is due to roundoff, or if it is legitimately small.
 It can legitimately have values of ~-1E-28 . When this happens, we assume P2 should also be small.
 We use the following criteria to test for a legitimate tiny P[3], which we must to send to CubicRoots */


 // if P[3] is tiny && legitimately tiny we use 0 as the threshold.
 // else P[3] must be more negative than roundoff errors cause.
 if(P[3] > ZERO_MINUS && fabs(P[2]) < 1.0E-6) P3Limit = 0.0;
 else P3Limit = ZERO_MINUS;

 if(P[3] < P3Limit)
  {
   MaxK = CubicRoots(P, RealRoot, ImagRoot);
   if(RealRoot[MaxK] > 0.0)  // MaxK is the index of the largest real root.
    {
     d = 4.0*RealRoot[MaxK];
     a += d;
     if(a*b < 0.0)P[1] = -sqrtl(d);
     else         P[1] = sqrtl(d);
     b = 0.5 * (a + b/P[1]);
     goto QUAD;
    }
  }

 if(P[2] < -1.0E-8)  // 2 sets of equal imag roots
  {
   b = sqrtl(fabsl(c));
   d = b + b - a;
   if(d > 0.0)P[1] = sqrtl(fabsl(d));
   else       P[1] = 0.0;
  }
 else
  {
   if(P[1] > 0.0)b =  2.0*sqrtl(fabsl(P[2])) + P[1];
   else          b = -2.0*sqrtl(fabsl(P[2])) + P[1];

   if(fabsl(b) < 10.0*ZERO_PLUS) // 4 equal real roots.  Was originally if(b == 0.0)
    {
     for(k=0; k<4; k++)
      {
       RealRoot[k] = -e;
       ImagRoot[k] = 0.0;
      }
     return;
    }
   else P[1] = 0.0;
  }

 // Calc the roots from two 2nd order polys and subtract e from the real part.
 QUAD:
 P[2] = c/b;
 QuadRoots(P, RealRoot, ImagRoot);

 P[1] = -P[1];
 P[2] = b;
 QuadRoots(P, RealRoot+2, ImagRoot+2);

 for(k=0; k<4; k++)RealRoot[k] -= e;

}

//---------------------------------------------------------------------------


