#define thisprog "xe-oversample1"
#define TITLE_STRING thisprog" v 1: 17.March.2019 [JRH]"
#define MAXLINELEN 1000

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
<TAGS> signal_processing filter </TAGS>

v 1: 17.March.2019 [JRH]

*/

/* external functions start */
char *xf_lineread1(char *line, long *maxlinelen, FILE *fpin);
long *xf_lineparse2(char *line,char *delimiters, long *nwords);
int xf_filter_bworth1_f(float *X, size_t nn, float sample_freq, float low_freq, float high_freq, float res, char *message);
long xf_interp3_f(float *data, long ndata);
float *xf_expand1_f(float *data , long nn, long setn, char *message);
/* external functions end */


int main (int argc, char *argv[]) {

	/* general variables */
	char *line=NULL,message[MAXLINELEN];
	long ii,jj,kk,nn,nbad,maxlinelen=0;
	int w,x,y,z;
	float a,b,c,d,result_f[64];
	double aa,bb,cc,dd,ee, result_d[64];
	FILE *fpin,*fpout;
	/* program-specific variables */
	long sizeofd1,n2;
	float *data1=NULL;
	double sr2,divisor=2.0;
	/* arguments */
	char *infile;
	int setverb=0;
	long setcolx=1,setcoly=2;
	double setsr=1000.0,setmul=4.0;
	float setres=pow(2.0,0.5),setlow=0.0,sethigh=-1.0;

	/********************************************************************************
	PRINT INSTRUCTIONS IF THERE IS NO FILENAME SPECIFIED
	********************************************************************************/
	if(argc<2) {
		fprintf(stderr,"\n");
		fprintf(stderr,"----------------------------------------------------------------------\n");
		fprintf(stderr,"%s\n",TITLE_STRING);
		fprintf(stderr,"----------------------------------------------------------------------\n");
		fprintf(stderr,"Resample an input, adding points and smoothing (Butterworth filter)\n");
		fprintf(stderr,"- assumes one valid numeric value per input line\n");
		fprintf(stderr,"- non-numeric values will be interpolated\n");
		fprintf(stderr,"USAGE: %s [infile] [options]\n",thisprog);
		fprintf(stderr,"	[input]: file name or \"stdin\"\n");
		fprintf(stderr,"VALID OPTIONS: defaults in []\n");
		fprintf(stderr,"	-sr: sample-rate (Hz) of input [%g]\n",setsr);
		fprintf(stderr,"	-mul: multiplier for sample-rate [%g]\n",setmul);
		fprintf(stderr,"	-low: low-cut filter (Hz, 0=NONE) [%g]\n",setlow);
		fprintf(stderr,"	-high: high-cut filter (Hz, -1=AUTO, 0=NONE) [%g]\n",sethigh);
		fprintf(stderr,"		AUTO= sr/%g\n",divisor);
		fprintf(stderr,"	-verb: verbose output (0=NO 1=YES 999=DEBUG) [%d]\n",setverb);
		fprintf(stderr,"EXAMPLES:\n");
		fprintf(stderr,"	%s data.txt -sr 1000 -mul 4 -low 10 1\n",thisprog);
		fprintf(stderr,"	cat temp.txt | %s stdin -sr 1000 -mul 2\n",thisprog);
		fprintf(stderr,"OUTPUT:\n");
		fprintf(stderr,"	higher-resolution, filtered values\n");
		fprintf(stderr,"----------------------------------------------------------------------\n");
		fprintf(stderr,"\n");
		exit(0);
	}

	/********************************************************************************
	READ THE FILENAME AND OPTIONAL ARGUMENTS - including comma-separated list item
	********************************************************************************/
	infile= argv[1];
	for(ii=2;ii<argc;ii++) {
		if( *(argv[ii]+0) == '-') {
			if((ii+1)>=argc) {fprintf(stderr,"\n--- Error[%s]: missing value for argument \"%s\"\n\n",thisprog,argv[ii]); exit(1);}
			else if(strcmp(argv[ii],"-sr")==0)   setsr=   atof(argv[++ii]);
			else if(strcmp(argv[ii],"-mul")==0)  setmul=  atof(argv[++ii]);
			else if(strcmp(argv[ii],"-low")==0)  setlow=  atof(argv[++ii]);
			else if(strcmp(argv[ii],"-high")==0) sethigh= atof(argv[++ii]);
			else if(strcmp(argv[ii],"-verb")==0) setverb= atoi(argv[++ii]);
			else {fprintf(stderr,"\n--- Error [%s]: invalid command line argument [%s]\n\n",thisprog,argv[ii]); exit(1);}
	}}
	if(setverb!=0 && setverb!=1 && setverb != 999) { fprintf(stderr,"\n--- Error [%s]: invalid -verb [%d] must be 0,1, or 999\n\n",thisprog,setverb);exit(1);}
	if(setmul<1.0) { fprintf(stderr,"\n--- Error [%s]: invalid -mul [%g] must be >=1\n\n",thisprog,setmul);exit(1);}
	if(setmul==1) divisor=2.1;

	/********************************************************************************
	STORE DATA - ASSUME WE DON'T KNOW THE LENGTH OF EACH INPUT LINE
	********************************************************************************/
	sizeofd1= sizeof(*data1);
	if(strcmp(infile,"stdin")==0) fpin=stdin;
	else if((fpin=fopen(infile,"r"))==0) {fprintf(stderr,"\n--- Error[%s]: file \"%s\" not found\n\n",thisprog,infile);exit(1);}
	nn= nbad= 0;
	if(setverb==1) fprintf(stderr,"\tstoring data from %s\n",infile);
	while((line=xf_lineread1(line,&maxlinelen,fpin))!=NULL) {
		if(maxlinelen==-1)  {fprintf(stderr,"\n--- Error[%s]: readline function encountered insufficient memory\n\n",thisprog);exit(1);}
		if(sscanf(line,"%f",&a)!=1 || !isfinite(a)) {a=NAN; nbad++;}
		data1= realloc(data1,((nn+1)*sizeofd1));
		if(data1==NULL) {fprintf(stderr,"\n--- Error[%s]: insufficient memory\n\n",thisprog);exit(1);};
		data1[nn]= a;
		nn++;
	}
	if(strcmp(infile,"stdin")!=0) fclose(fpin);
	if(setverb==1) fprintf(stderr,"\t%ld data points read - %ld invalid\n",nn,nbad);
	//TEST:	for(ii=0;ii<nn;ii++) printf("%g\n",data1[ii]); goto END;

	/********************************************************************************/
	/* INTERPOLATE IF THERE ARE INVALID DATAPOINTS */
	/********************************************************************************/
	if(nbad>0) {
		if(setverb==1) fprintf(stderr,"\tinterpolating...\n");
		kk= xf_interp3_f(data1,(off_t)nn);
		if(kk<0) { fprintf(stderr,"\n\t --- Error [%s]: input \"%s\" contains no valid numbers\n\n",thisprog,infile);free(data1);exit(1);}
	}

	/********************************************************************************/
	/* EXPAND THE DATA BY DUPLICATING POINTS */
	/********************************************************************************/
	n2= (long)nn*setmul;
	sr2= setsr*setmul;
	if(setverb==1) fprintf(stderr,"\texpanding to %ld points, samplerate= %g Hz...\n",n2,sr2);
	if(setmul>1.0) {
		data1= xf_expand1_f(data1,nn,n2,message);
		if(data1==NULL) { fprintf(stderr,"\n\t--- %s/%s\n\n",thisprog,message); exit(1); }
	}

	/********************************************************************************/
	/* APPLY THE BUTTERWORTH FILTER */
	/********************************************************************************/
	if(sethigh==-1) sethigh= (float)(setsr/divisor);
	if(setverb==1) fprintf(stderr,"\tfiltering (%g-%g Hz) ...\n",setlow,sethigh);
	z= xf_filter_bworth1_f(data1,n2,(float)sr2,setlow,sethigh,setres,message);

	/********************************************************************************/
	/* OUTPUT */
	/********************************************************************************/
	for(ii=0;ii<n2;ii++) printf("%g\n",data1[ii]);


	goto END;

	/********************************************************************************/
	/* CLEANUP AND EXIT */
	/********************************************************************************/
END:
	if(data1!=NULL) free(data1);
	exit(0);
}
