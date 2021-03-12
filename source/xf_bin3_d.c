/*
<TAGS>signal_processing transform</TAGS>
DESCRIPTION:
	- based on xf_bin1b_d
	- fixed-bin-size averaging which doesn't destroy the position of the data in the original array
		- parallel array (0-1) indicates which elements represent the "new" array
	- non-finite values (INF, NAN) do not contribute to the averages
	- allows for different numbers of elements to go into different bins to evenly spread the results (fractional bin-widths)
	- allows definition of a "zero" sample which is guaranteed to be the first sample in the bin corresponding with the new "zero"
	- guarantees no bins will be under-sampled - edge bins will contain between 1x and just-under-2x bindwidth samples
		- exception - a single partial-bin just before zero can be included, as data around zero is usually too important to exclude

USES:
	- Downsampling data
	- Allows re-combination of binned data with columns of data corresponding with the original data

DEPENDENCY TREE:
	No dependencies

ARGUMENTS:
	double *data      : pointer to input array, which will be overwritten
	short *flag       : pointer to flag (0-1) array - will identify elements which have been binned
	long *setn        : number of elements in data (overwritten - pass as address)
	long *setz        : element to treat as "zero" (overwritten - pass as address)
 	double setbinsize : desired bin-width (samples - can be a fraction)
	char *message     : array to hold error message

RETURN VALUE:
	- status (0=success, -1=fail)
	- data will be modified so some elements hold averages
	- flag will be modified to "1" for elements which hold the averages, and "0" for unchanged values
	- setz and setn are also updated
		- can be used to reconstruct timestamps
SAMPLE CALL:
	char message[256];
	double data[19];
	double aa,bb,nbins,sampinterval=1,setbinsize=3.5;
	long ii,setn=19, setz=6;
	for(ii=0;ii<setn;ii++) { data[ii]=(double)ii; printf("%g\n",data[ii]); }
	printf("setz=item number %ld\n",setz);
	printf("\n");

	nbins= xf_bin1b_d(data,&setn,&setz,setbinsize,message);
	if(nbins==0) {fprintf(stderr,"*** %s\n",message); exit(1);}

	aa=(double)(setz)*(-1)*setbinsize*sampinterval;
	bb=setbinsize*sampinterval;
	for(ii=0;ii<setn;ii++)  { printf("%g\t%f\n",aa,data[ii]);	aa+=bb; }
	printf("new setz=item number %ld\n",setz);

*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int xf_bin3_d(double *data, short *flag, long *setn, long *setz, double setbinsize, char *message) {

	char *thisfunc="xf_bin3_d\0";
	long ii,jj,n1,n2,nbins,zero,nsums,start;
	double aa,bb,cc,prebins,limit,sum;

	n1=*setn;
	zero=*setz;
	//TEST: fprintf(stderr,"n1=%ld\tzero=%ld\tsetbinsize=%.9f\n",n1,zero,setbinsize);

	/* CHECK PARAMETERS */
	if(n1<1) {
		sprintf(message,"%s [ERROR]: number of samples (%ld) must be >0",thisfunc,n1);
		return(-1);
	}
	if(setbinsize==1.0) {
		return(n1);
	}
	if(setbinsize<=0) {
		sprintf(message,"%s [ERROR]: bin size (%g) must be >0",thisfunc,setbinsize);
		return(-1);
	}
	if(zero>=n1) {
		sprintf(message,"%s [ERROR]: specified zero-sample (%ld) must be less than data array length (%ld)",thisfunc,zero,n1);
		return(-1);
	}
	//TEST: for(ii=0;ii<n1;ii++) printf("%g\n",data[ii]); exit(1);

	/* IF "ZERO" IS SET, CALCULATE THE NUMBER OF BINS BEFORE "ZERO" (PREBINS) */
	/* note that if prebins is not an integer, a portion will be combined with another bin */
	if(zero>0) prebins=(double)(zero)/setbinsize;
	else prebins=0.0;

	/* INITIALISE VARIABLES HERE */
	sum= 0.0;
	n2=nsums= 0;
	for(ii=0;ii<n1;ii++) flag[ii]= 0;

	/* CALCULATE THE LIMIT FOR THE FIRST BIN - MAY BE FRACTIONAL */
	/* if there is at least one full bin before "zero", the first limit comes before "zero" as well */
	if(prebins>=1.0) {
		limit= ((double)(zero)-1.0) - ((long)(prebins-1.0)*setbinsize);
		start= 0;
	}
	/* otherwise accumulate data up to zero - this is the only instance where a bin can include less than the normal amount of data  */
	else {
		limit= (double)zero + setbinsize - 1.0;
		start= zero;
		for(ii=0;ii<zero;ii++) if(isfinite(data[ii])) { sum+= data[ii]; nsums++;}
		if(nsums>0) data[n2]= (sum/(double)nsums);
		else data[n2]=NAN;
		sum= 0.0;
		nsums= 0;
		prebins=1.0; // indicates a part-bin was created - this ensures new zero is element "1"
		n2++;
	}
	//TEST: fprintf(stderr,"start: %ld	zero: %ld	setbinsize:%.4f	prebins=%g	limit:%.16f\n",start,zero,setbinsize,prebins,limit);

printf("\nprebins=%g\n",prebins);
printf("start=%ld\n",start);
printf("limit=%g\n",limit);
printf("n2=%ld\n",n2);

	/* START BINNING: LEFTOVER DATA AT THE END IS ADDED TO THE PRECEDING BIN */
	for(ii=start;ii<n1;ii++) {
		/* build runing sum and total data-points - good data only */
		if(isfinite(data[ii])) {
			sum+= data[ii];
			nsums++;
		}
		// if the current sample-number is >= the limit defining the right edge of the curent window...
		if(ii>=limit) {
printf("ii=%ld data=%g nsums=%ld sum=%g n2=%ld\n",ii,data[ii],nsums,sum,n2);
			//TEST:	printf("\tii=%ld	bin=%ld nsums=%ld	limits: %f to %f: next=%f\n",ii,n2,nsums,(limit-setbinsize),(limit),(limit+setbinsize));
			if(nsums>0) data[n2]= (sum/(double)nsums);
			else data[n2]=NAN;
			n2++;
			sum=0.0;            // reset the run1ing sum
			nsums=0;            // reset the count within the window
			limit+= setbinsize; // readjust limit
		}
	}
	//TEST: fprintf(stderr,"ii: %ld limit:%g	nsums:%ld sum:%g	\n",ii,limit,nsums,sum);


	/* MAKE ONE MORE BIN IF THERE IS LEFTOVER DATA (IE. IF LAST SAMPLE DIDN'T TIP THE LIMIT)  */
	if( ((ii-1)+setbinsize) != limit ) {
		jj= n1-(long)setbinsize;
		if(jj<0) jj=0;
		sum=0.0;
		nsums=0;
		for(ii=jj;ii<n1;ii++) {
			if(isfinite(data[ii])) {
				sum+= data[ii];
				nsums++;
		}}
		if(nsums>=0) data[n2]= sum/(double)nsums;
		else data[n2]=NAN;
		n2++;
	}

	/* REASSIGN SETZ AND N */
	(*setz)= prebins;
	(*setn)= n2;
	return(0);
}
