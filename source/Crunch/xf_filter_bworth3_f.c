/*
DESCRIPTION: 

	Apply a tweaked biquad Butterworth filter to an array of numbers
		- this version overwrites the original input array
		
	Makes two passes at the data (forward & reverse), to avoid time-shifting
	Does this twice, if required, to do both low- and high-pass filtering 

	Coefficient calculations based on public domain code originally 
	posted by Patrice Tarrabia (http://www.musicdsp.org/showone.php?id=38)

	NOTE : interpolation should be applied first if needed to remove NANs or INFs
	NOTE : padding the array can help remove effects from large deflections at either edge 
           of the data, but is NOT required to compensate for data offset from zero


USES: 
	To remove unwanted frequencies from data

DEPENDENCY TREE: 
	No dependencies
	
ARGUMENTS: 
	float *X:           data array to be filtered, fixed sample rate is assumed
	size_t nn:          length of X array
	float sample_freq:  sample frequency (samples per second)
	float low_freq:     cut-off for the high-pass filter - set to 0 to skip this step
	float high_freq:    cut-off for the low-pass filter - set to 0 to skip this step
							NOTE: if neither low_freq nor high_freq are set, data is unaffected 
	float res:          resonance value (typically 1, range 0-sqrt(2) )
	                        NOTE: low values produce sharper rolloffs but can produce ringing in the output
	                        NOTE: high values produce gentle rolloffs but can dampen the signal
	char message[]:	    message indicating success or reason for failure

RETURN VALUE: 
	0 on success, -1 on fail
	
*/

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int xf_filter_bworth1_f(float *X, size_t nn, float sample_freq, float low_freq, float high_freq, float res, char *message) {

	char *thisfunc="xf_filter_bworth1_f\0";
	size_t ii;
	float *Y=NULL;
	double s,p,r,omega,a0,a1,a2,b1,b2; 

	sprintf(message,"%s (incomplete))\0",thisfunc);

	if(nn<4) {
		sprintf(message,"%s (no filtering - number of input samples is less then 4)",thisfunc);
		return(-1);
	}
	if(low_freq>(sample_freq/2.0)) {
		sprintf(message,"%s (low frequency %g must be <= half of sample frequency %g)",thisfunc,low_freq,sample_freq);
		return(-1);
	}
	if(high_freq>(sample_freq/2.0)) {
		sprintf(message,"%s (high frequency %g must be <= half of sample frequency %g)",thisfunc,high_freq,sample_freq);
		return(-1);
	}
	
	/* ALLOCATE MEMORY FOR SWAP VARIABLE */
	Y=(float *) malloc(nn*sizeof(float));
	if(Y==NULL) {
		sprintf(message,"%s (memory allocation error)",thisfunc);
		return(-1);
	}

	/* BI-DIRECTIONAL HIGH-PASS (LOW-CUT) FILTER */
	if(low_freq>0.0) {
		/* DEFINE COEFFICIENTS */
		omega = tan( M_PI * (double)low_freq/(double)sample_freq );
		a0= 1.0 / ( 1.0 + (double)res*omega + omega*omega);
		a1= -2*a0;
		a2= a0;
		b1= 2.0 * ( omega*omega - 1.0) * a0;
		b2= ( 1.0 - (double)res*omega + omega*omega) * a0;
	
		/* FORWARD FILTER, COPYING DATA TO ARRAY Y */
		/* initialize: using all coefficients for the non-recursive terms helps reduce edge effects if data is offset from zero */
		ii=0; Y[ii] = a0*X[ii] + a1*X[ii] + a2*X[ii]; 
		ii=1; Y[ii] = a0*X[ii] + a1*X[ii-1] + a2*X[ii-1] - b1*Y[ii-1]; 
		for(ii=2;ii<nn;ii++) Y[ii] = a0*X[ii] + a1*X[ii-1] + a2*X[ii-2] - b1*Y[ii-1] - b2*Y[ii-2];

		/* BACKWARD FILTER TO REMOVE PHASE-SHIFT, AND COPY DATA BACK TO INPUT ARRAY X */
		/* initialize: using all coefficients for the non-recursive terms helps reduce edge effects if data is offset from zero */
		ii=nn-1; X[ii] = a0*Y[ii] + a1*Y[ii] + a2*Y[ii];
		ii=nn-2; X[ii] = a0*Y[ii] + a1*Y[ii+1] + a2*Y[ii+1] - b1*X[ii+1];
		while(ii-- >0) { /* ii is size_t, so if it becomes negative any test for i<0 will fail */
			X[ii] = a0*Y[ii] + a1*Y[ii+1] + a2*Y[ii+2] - b1*X[ii+1] - b2*X[ii+2];
		}
	}
	
	/* BI-DIRECTIONAL LOW-PASS (HIGH-CUT) FILTER */
	if(high_freq>0.0) {
		/* DEFINE COEFFICIENTS FOR LOW-PASS FILTER */
		omega = 1.0 / tan( M_PI * (double)high_freq/(double)sample_freq );
		a0= 1.0 / (1.0 + (double)res*omega + omega*omega);
		a1= 2* a0;
		a2= a0;
		b1= 2.0 * (1.0 - omega*omega) * a0;
		b2= (1.0 - (double)res*omega + omega*omega) * a0;

		/* FORWARD FILTER, COPYING DATA TO ARRAY Y */
		/* initialize: using all coefficients for the non-recursive terms helps reduce edge effects if data is offset from zero */
		ii=0; Y[ii] = a0*X[ii] + a1*X[ii] + a2*X[ii]; 
		ii=1; Y[ii] = a0*X[ii] + a1*X[ii-1] + a2*X[ii-1] - b1*Y[ii-1]; 
		for(ii=2;ii<nn;ii++) Y[ii] = a0*X[ii] + a1*X[ii-1] + a2*X[ii-2] - b1*Y[ii-1] - b2*Y[ii-2];

		/* BACKWARD FILTER TO REMOVE PHASE-SHIFT, AND COPY DATA BACK TO INPUT ARRAY X */
		/* initialize: using all coefficients for the non-recursive terms helps reduce edge effects if data is offset from zero */
		ii=nn-1; X[ii] = a0*Y[ii] + a1*Y[ii] + a2*Y[ii];
		ii=nn-2; X[ii] = a0*Y[ii] + a1*Y[ii+1] + a2*Y[ii+1] - b1*X[ii+1];
		while(ii-- >0) { /* ii is size_t, so if it becomes negative any test for i<0 will fail */
			X[ii] = a0*Y[ii] + a1*Y[ii+1] + a2*Y[ii+2] - b1*X[ii+1] - b2*X[ii+2];
		}
	}
	
	/* WRAP UP */
	sprintf(message,"%s (successfully filtered %ld points)",thisfunc,nn);
	free(Y);
	return(0);

}

