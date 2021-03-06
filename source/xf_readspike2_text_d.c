/*
<TAGS>file spike2 </TAGS>
DESCRIPTION:
	Read a Spike2 text-export file
		- generated by Spike2 script: ss2_export_activity_perchannel.s2s
		- assumes a constant sample-rate with missing-values filled by Spike2
		- START line specifies sample-interval (seconds) in the 3rd field

DEPENDENCIES:
	char *xf_lineread1(char *line, long *maxlinelen, FILE *fpin);
	long *xf_lineparse2(char *line, char *delimiters, long *nwords);

ARGUMENTS:
	char *infile     - input: name of input file, or "stdin"
	long *ndata      - output: number of data-points ( pass as address)
	double *sampint  - output: the sample-interval, in seconds ( pass as address )
	char *message    - output[256]: the chandata2el-label, or error message (calling function must pre-allocate)

RETURN VALUE:
	- on success:
		- returns pointer to an array of values (64-bit float)
		- ndata updated
		- sampint updated
		- message[256] holds the chandata2el label
	- on error: NULL
		- message[256] describes the error

SAMPLE INPUT
- here the START line specifies a 1-second sample-interval (3rd field on the START line)
- note also that there is a padding of zeros at the begindata2ing, filling the time before the activity signal was acquired by the system

	SUMMARY
	3	RealWave	Act: 1	Counts	1	1	1	0

	CHAndata2EL	3

	Counts	1
	START	0.00000	1.00000
	0.00000
	0.00000
	0.00000
	0.00000
	0.00000
	0.00000
	0.00205
	-0.00671
	0.02480
	-0.09248
	0.73903
	...etc...

SAMPLE CALL:
	char infile="data.txt",message[256];
	long ndata2;
	double *data1=NULL,sampint;

	data1= xf_readtable1_d(infile,&ndata2,&sampint,message);
	if(data1==NULL) { fprintf(stderr,"\n---Error: %s\n\n",message); exit(1); }


*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* external functions start */
char *xf_lineread1(char *line, long *maxlinelen, FILE *fpin);
long *xf_lineparse2(char *line, char *delimiters, long *nwords);
/* external functions end */

double *xf_readspike2_text_d(char *infile, long *ndata, double *sampint, char *message) {

	FILE *fpin=NULL;
	char *thisfunc= "xf_readspike2_text_d\0";
	char *line=NULL,*templine=NULL,*pchar=NULL;
	int foundsummary=0,foundstart=0;
	long *start=NULL,nwords,ndata2=0,nlines=0,maxlinelen=0;
	double *data1=NULL,aa,sampint2=0;
	size_t sizeofdata1;

	sizeofdata1= sizeof(*data1);

	if(strcmp(infile,"stdin")==0) fpin=stdin;
	else if((fpin=fopen(infile,"r"))==0) { sprintf(message," %s: file \"%s\" not found",thisfunc,infile);goto ERROR;}

	while((line=xf_lineread1(line,&maxlinelen,fpin))!=NULL) {
		if(maxlinelen==-1)  {sprintf(message," %s: memory allocation error",thisfunc);goto ERROR;}
		/* read the header if it hasn't been found yet */
		if(foundstart==0) {
			start= xf_lineparse2(line,"\t",&nwords);
			if(nwords<1) continue;
			else if(strcmp((line+start[0]),"SUMMARY")==0) {
				foundsummary= 1;
				continue;
			}
			else if(foundsummary==1) {
				if(nwords<3) {sprintf(message," %s: no label found on line following SUMMARY in input",thisfunc);goto ERROR;};
				strncpy(message,(line+start[2]),256);
				foundsummary= 0;
				continue;
			}
			else if(strcmp((line+start[0]),"START")==0) {
				if(nwords<3) {sprintf(message," %s: no sample-interval on START line of input",thisfunc);goto ERROR;};
				aa= atof(line+start[2]);
				if(!isfinite(aa) || aa<=0) {sprintf(message," %s: invalid sample-interval (%s) on START line of input",thisfunc,(line+start[2]));goto ERROR;}
				sampint2= atof(line+start[2]);
				foundstart= 1;
				continue;
			}
			else continue;
		}

		/* skip blank lines */
		if(strlen(line)<2) continue;

 		data1= realloc(data1,(ndata2+1)*sizeofdata1);
		if(data1==NULL) {sprintf(message,"%s: memory allocation error storing data1",thisfunc);goto ERROR;}
		if(sscanf(line,"%lf",&aa)==1 && isfinite(aa)) data1[ndata2]= aa;
		else data1[ndata2]= NAN;
		ndata2++;
	}

	goto FINISH;


ERROR:
	if(strcmp(infile,"stdin")!=0 && fpin!=NULL) fclose(fpin);
	if(line!=NULL) free(line);
	if(start!=NULL) free(start);
	*ndata=-1;
	*sampint=-1;
	return(NULL);

FINISH:
	if(strcmp(infile,"stdin")!=0) fclose(fpin);
	if(line!=NULL) free(line);
	if(start!=NULL) free(start);
	*ndata= ndata2;
	*sampint= sampint2;
	return(data1);
}
