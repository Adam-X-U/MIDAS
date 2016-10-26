//
// FitGrain.c
//
//
// Created by Hemant sharma on 2016/10/24
//
// Will NOT optimize: p0, p1, p2, RhoD, Lsd
// Will optimize: tx,ty,tz,yBC,zBC,wedge, x,y,z, orient, a,b,c,alpha,beta,gamma
// Optimize wedge separate of everything else
// Things to read in: SpotMatrix.csv, Grains.csv, Params.txt
// Things to read from Params.txt: tx, ty, tz, Lsd, p0, p1, p2,
//				RhoD, BC, Wedge, NrPixels, px,
//				Wavelength, OmegaRange, BoxSize
//				MinEta, Hbeam, Rsample, RingNumbers (will provide cs),
//				RingRadii, 

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <nlopt.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>

#define deg2rad 0.0174532925199433
#define rad2deg 57.2957795130823
#define EPS 1E-12
#define CalcNorm3(x,y,z) sqrt((x)*(x) + (y)*(y) + (z)*(z))
#define CalcNorm2(x,y) sqrt((x)*(x) + (y)*(y))
#define MAX_LINE_LENGTH 4096
#define MaxNSpotsBest 1000

static inline
int**
allocMatrixInt(int nrows, int ncols)
{
    int** arr;
    int i;
    arr = malloc(nrows * sizeof(*arr));
    if (arr == NULL ) {
        return NULL;
    }
    for ( i = 0 ; i < nrows ; i++) {
        arr[i] = malloc(ncols * sizeof(*arr[i]));
        if (arr[i] == NULL ) {
            return NULL;
        }
    }
    return arr;
}

static inline
void
FreeMemMatrixInt(int **mat,int nrows)
{
    int r;
    for ( r = 0 ; r < nrows ; r++) {
        free(mat[r]);
    }
    free(mat);
}

static inline
double**
allocMatrix(int nrows, int ncols)
{
    double** arr;
    int i;
    arr = malloc(nrows * sizeof(*arr));
    if (arr == NULL ) {
        return NULL;
    }
    for ( i = 0 ; i < nrows ; i++) {
        arr[i] = malloc(ncols * sizeof(*arr[i]));
        if (arr[i] == NULL ) {
            return NULL;
        }
    }
    return arr;
}

static inline
void
FreeMemMatrix(double **mat,int nrows)
{
    int r;
    for ( r = 0 ; r < nrows ; r++) {
        free(mat[r]);
    }
    free(mat);
}

static inline void Convert9To3x3(double MatIn[9],double MatOut[3][3]){int i,j,k=0;for (i=0;i<3;i++){for (j=0;j<3;j++){MatOut[i][j] = MatIn[k];k++;}}}
static inline void Convert3x3To9(double MatIn[3][3],double MatOut[9]){int i,j; for (i=0;i<3;i++) for (j=0;j<3;j++) MatOut[(i*3)+j] = MatIn[i][j];}
static inline double sind(double x){return sin(deg2rad*x);}
static inline double cosd(double x){return cos(deg2rad*x);}
static inline double tand(double x){return tan(deg2rad*x);}
static inline double asind(double x){return rad2deg*(asin(x));}
static inline double acosd(double x){return rad2deg*(acos(x));}
static inline double atand(double x){return rad2deg*(atan(x));}
static inline double sin_cos_to_angle (double s, double c){return (s >= 0.0) ? acos(c) : 2.0 * M_PI - acos(c);}

static inline 
void OrientMat2Euler(double m[3][3],double Euler[3])
{
    double psi, phi, theta, sph;
	if (fabs(m[2][2] - 1.0) < EPS){
		phi = 0;
	}else{
	    phi = acos(m[2][2]);
	}
    sph = sin(phi);
    if (fabs(sph) < EPS)
    {
        psi = 0.0;
        theta = (fabs(m[2][2] - 1.0) < EPS) ? sin_cos_to_angle(m[1][0], m[0][0]) : sin_cos_to_angle(-m[1][0], m[0][0]);
    } else{
        psi = (fabs(-m[1][2] / sph) <= 1.0) ? sin_cos_to_angle(m[0][2] / sph, -m[1][2] / sph) : sin_cos_to_angle(m[0][2] / sph,1);
        theta = (fabs(m[2][1] / sph) <= 1.0) ? sin_cos_to_angle(m[2][0] / sph, m[2][1] / sph) : sin_cos_to_angle(m[2][0] / sph,1);
    }
    Euler[0] = rad2deg*psi;
    Euler[1] = rad2deg*phi;
    Euler[2] = rad2deg*theta;
}

static inline
void Euler2OrientMat(
    double Euler[3],
    double m_out[3][3])
{
    double psi, phi, theta, cps, cph, cth, sps, sph, sth;
    psi = Euler[0];
    phi = Euler[1];
    theta = Euler[2];
    cps = cosd(psi) ; cph = cosd(phi); cth = cosd(theta);
    sps = sind(psi); sph = sind(phi); sth = sind(theta);
    m_out[0][0] = cth * cps - sth * cph * sps;
    m_out[0][1] = -cth * cph * sps - sth * cps;
    m_out[0][2] = sph * sps;
    m_out[1][0] = cth * sps + sth * cph * cps;
    m_out[1][1] = cth * cph * cps - sth * sps;
    m_out[1][2] = -sph * cps;
    m_out[2][0] = sth * sph;
    m_out[2][1] = cth * sph;
    m_out[2][2] = cph;
}

static inline
void
MatrixMult(
           double m[3][3],
           double  v[3],
           double r[3])
{
    int i;
    for (i=0; i<3; i++) {
        r[i] = m[i][0]*v[0] +
        m[i][1]*v[1] +
        m[i][2]*v[2];
    }
}

static inline void CorrectHKLsLatC(double LatC[6], double **hklsIn,int nhkls,double Lsd,double Wavelength,double **hkls)
{
	double a=LatC[0],b=LatC[1],c=LatC[2],alpha=LatC[3],beta=LatC[4],gamma=LatC[5];
	int hklnr;
	for (hklnr=0;hklnr<nhkls;hklnr++){
		double ginit[3]; ginit[0] = hklsIn[hklnr][0]; ginit[1] = hklsIn[hklnr][1]; ginit[2] = hklsIn[hklnr][2];
		double SinA = sind(alpha), SinB = sind(beta), SinG = sind(gamma), CosA = cosd(alpha), CosB = cosd(beta), CosG = cosd(gamma);
		double GammaPr = acosd((CosA*CosB - CosG)/(SinA*SinB)), BetaPr  = acosd((CosG*CosA - CosB)/(SinG*SinA)), SinBetaPr = sind(BetaPr);
		double Vol = (a*(b*(c*(SinA*(SinBetaPr*(SinG)))))), APr = b*c*SinA/Vol, BPr = c*a*SinB/Vol, CPr = a*b*SinG/Vol;
		double B[3][3]; B[0][0] = APr; B[0][1] = (BPr*cosd(GammaPr)), B[0][2] = (CPr*cosd(BetaPr)), B[1][0] = 0,
			B[1][1] = (BPr*sind(GammaPr)), B[1][2] = (-CPr*SinBetaPr*CosA), B[2][0] = 0, B[2][1] = 0, B[2][2] = (CPr*SinBetaPr*SinA);
		double GCart[3];
		MatrixMult(B,ginit,GCart);
		double Ds = 1/(sqrt((GCart[0]*GCart[0])+(GCart[1]*GCart[1])+(GCart[2]*GCart[2])));
		hkls[hklnr][0] = GCart[0];hkls[hklnr][1] = GCart[1];hkls[hklnr][2] = GCart[2];
        hkls[hklnr][3] = Ds;
        double Theta = (asind((Wavelength)/(2*Ds)));
        hkls[hklnr][4] = Theta;
        double Rad = Lsd*(tand(2*Theta));
        hkls[hklnr][5] = Rad;
        hkls[hklnr][6] = hklsIn[hklnr][6];
	}
}


static inline
void DisplacementInTheSpot(double a, double b, double c, double xi, double yi, double zi,
						double omega, double wedge, double chi, double *Displ_y, double *Displ_z)
{
	double sinOme=sind(omega), cosOme=cosd(omega), AcosOme=a*cosOme, BsinOme=b*sinOme;
	double XNoW=AcosOme-BsinOme, YNoW=(a*sinOme)+(b*cosOme), ZNoW=c;
	double WedgeRad=deg2rad*wedge, CosW=cos(WedgeRad), SinW=sin(WedgeRad), XW=XNoW*CosW-ZNoW*SinW, YW=YNoW;
    double ZW=(XNoW*SinW)+(ZNoW*CosW), ChiRad=deg2rad*chi, CosC=cos(ChiRad), SinC=sin(ChiRad), XC=XW;
    double YC=(CosC*YW)-(SinC*ZW), ZC=(SinC*YW)+(CosC*ZW);
    double IK[3],NormIK; IK[0]=xi-XC; IK[1]=yi-YC; IK[2]=zi-ZC; NormIK=sqrt((IK[0]*IK[0])+(IK[1]*IK[1])+(IK[2]*IK[2]));
    IK[0]=IK[0]/NormIK;IK[1]=IK[1]/NormIK;IK[2]=IK[2]/NormIK;
    *Displ_y = YC - ((XC*IK[1])/(IK[0]));
    *Displ_z = ZC - ((XC*IK[2])/(IK[0]));
}

static inline 
double CalcEtaAngle(double y, double z){
	double alpha = rad2deg*acos(z/sqrt(y*y+z*z));
	if (y>0) alpha = -alpha;
	return alpha;
}

static inline
void CorrectForOme(double yc, double zc, double Lsd, double OmegaIni, double wl, double wedge, double *ysOut, double *zsOut, double *OmegaOut)
{
	double ysi = yc, zsi = zc;
	double CosOme=cos(deg2rad*OmegaIni), SinOme=sin(deg2rad*OmegaIni);
	double eta = CalcEtaAngle(ysi,zsi);
	double RingRadius = sqrt((ysi*ysi)+(zsi*zsi));
	double tth = rad2deg*atan(RingRadius/Lsd);
	double theta = tth/2;
	double SinTheta = sin(deg2rad*theta);
	double CosTheta = cos(deg2rad*theta);
	double ds = 2*SinTheta/wl;
	double CosW = cos(deg2rad*wedge);
	double SinW = sin(deg2rad*wedge);
	double SinEta = sin(deg2rad*eta);
	double CosEta = cos(deg2rad*eta);
	double k1 = -ds*SinTheta;
	double k2 = -ds*CosTheta*SinEta;
	double k3 =  ds*CosTheta*CosEta;
	if (eta == 90){k3 = 0; k2 = -CosTheta;}
	else if (eta == -90) {k3 = 0; k2 = CosTheta;}
	double k1f = (k1*CosW) + (k3*SinW);
	double k2f = k2;
	double k3f = (k3*CosW) - (k1*SinW);
	double G1a = (k1f*CosOme) + (k2f*SinOme);
	double G2a = (k2f*CosOme) - (k1f*SinOme);
	double G3a = k3f;
	double LenGa = sqrt((G1a*G1a)+(G2a*G2a)+(G3a*G3a));
	double g1 = G1a*ds/LenGa;
	double g2 = G2a*ds/LenGa;
	double g3 = G3a*ds/LenGa;
	SinW = 0;
	CosW = 1;
	double LenG = sqrt((g1*g1)+(g2*g2)+(g3*g3));
	double k1i = -(LenG*LenG*wl)/2;
	tth = 2*rad2deg*asin(wl*LenG/2);
	RingRadius = Lsd*tan(deg2rad*tth);
	double A = (k1i+(g3*SinW))/(CosW);
	double a_Sin = (g1*g1) + (g2*g2);
	double b_Sin = 2*A*g2;
	double c_Sin = (A*A) - (g1*g1);
	double a_Cos = a_Sin;
	double b_Cos = -2*A*g1;
	double c_Cos = (A*A) - (g2*g2);
	double Par_Sin = (b_Sin*b_Sin) - (4*a_Sin*c_Sin);
	double Par_Cos = (b_Cos*b_Cos) - (4*a_Cos*c_Cos);
	double P_check_Sin = 0;
	double P_check_Cos = 0;
	double P_Sin,P_Cos;
	if (Par_Sin >=0) P_Sin=sqrt(Par_Sin);
	else {P_Sin=0;P_check_Sin=1;}
	if (Par_Cos>=0) P_Cos=sqrt(Par_Cos);
	else {P_Cos=0;P_check_Cos=1;}
	double SinOmega1 = (-b_Sin-P_Sin)/(2*a_Sin);
	double SinOmega2 = (-b_Sin+P_Sin)/(2*a_Sin);
	double CosOmega1 = (-b_Cos-P_Cos)/(2*a_Cos);
	double CosOmega2 = (-b_Cos+P_Cos)/(2*a_Cos);
	if      (SinOmega1 < -1) SinOmega1=0;
	else if (SinOmega1 >  1) SinOmega1=0;
	else if (SinOmega2 < -1) SinOmega2=0;
	else if (SinOmega2 >  1) SinOmega2=0;
	if      (CosOmega1 < -1) CosOmega1=0;
	else if (CosOmega1 >  1) CosOmega1=0;
	else if (CosOmega2 < -1) CosOmega2=0;
	else if (CosOmega2 >  1) CosOmega2=0;
	if (P_check_Sin == 1){SinOmega1=0;SinOmega2=0;}
	if (P_check_Cos == 1){CosOmega1=0;CosOmega2=0;}
	double Option1 = fabs((SinOmega1*SinOmega1)+(CosOmega1*CosOmega1)-1);
	double Option2 = fabs((SinOmega1*SinOmega1)+(CosOmega2*CosOmega2)-1);
	double Omega1, Omega2;
	if (Option1 < Option2){Omega1=rad2deg*atan2(SinOmega1,CosOmega1);Omega2=rad2deg*atan2(SinOmega2,CosOmega2);}
	else {Omega1=rad2deg*atan2(SinOmega1,CosOmega2);Omega2=rad2deg*atan2(SinOmega2,CosOmega1);}
	double OmeDiff1 = fabs(Omega1-OmegaIni);
	double OmeDiff2 = fabs(Omega2-OmegaIni);
	double Omega;
	if (OmeDiff1 < OmeDiff2)Omega=Omega1;
	else Omega=Omega2;
	double SinOmega=sin(deg2rad*Omega);
	double CosOmega=cos(deg2rad*Omega);
	double Fact = (g1*CosOmega) - (g2*SinOmega);
	double Eta = CalcEtaAngle(k2,k3);
	double Sin_Eta = sin(deg2rad*Eta);
	double Cos_Eta = cos(deg2rad*Eta);
	*ysOut = -RingRadius*Sin_Eta;
	*zsOut = RingRadius*Cos_Eta;
	*OmegaOut = Omega;
}

static inline
void SpotToGv(double xi, double yi, double zi, double Omega, double theta, double *g1, double *g2, double *g3)
{
	double CosOme = cosd(Omega), SinOme = sind(Omega), eta = CalcEtaAngle(yi,zi), TanEta = tand(-eta), SinTheta = sind(theta);
    double CosTheta = cosd(theta), CosW = 1, SinW = 0, k3 = SinTheta*(1+xi)/((yi*TanEta)+zi), k2 = TanEta*k3, k1 = -SinTheta;
    if (eta == 90){
		k3 = 0;
		k2 = -CosTheta;
	} else if (eta == -90){
		k3 = 0;
		k2 = CosTheta;
	}
    double k1f = (k1*CosW) + (k3*SinW), k3f = (k3*CosW) - (k1*SinW), k2f = k2;
    *g1 = (k1f*CosOme) + (k2f*SinOme);
    *g2 = (k2f*CosOme) - (k1f*SinOme);
    *g3 = k3f;
}

static inline
void CalcAngleErrors(int nspots, int nhkls, int nOmegaRanges, double x[12], double **spotsYZO, double **hklsIn, double Lsd, 
	double Wavelength, double OmegaRange[20][2], double BoxSize[20][4], double MinEta, double wedge, double chi,
	double **SpotsComp, double **SpList, double *Error, int *nSpotsComp)
{
	int i,j;
	int nrMatchedIndexer = nspots;
	double **MatchDiff;
	MatchDiff = allocMatrix(nrMatchedIndexer,3);
	double LatC[6];
	for (i=0;i<6;i++)LatC[i] = x[6+i];
	double **hkls;hkls = allocMatrix(nhkls,7);CorrectHKLsLatC(LatC,hklsIn,nhkls,Lsd,Wavelength,hkls);
	double OrientMatrix[3][3],EulerIn[3];EulerIn[0]=x[3];EulerIn[1]=x[4];EulerIn[2]=x[5];
	Euler2OrientMat(EulerIn,OrientMatrix);
	int nTspots,nrSp;
	double **TheorSpots;TheorSpots=allocMatrix(MaxNSpotsBest,9);
	CalcDiffractionSpots(Lsd,MinEta,OmegaRange,nOmegaRanges,hkls,nhkls,BoxSize,&nTspots,OrientMatrix,TheorSpots);
	double **SpotsYZOGCorr;SpotsYZOGCorr=allocMatrix(nrMatchedIndexer,7);
	double DisplY,DisplZ,ys,zs,Omega,Radius,Theta,lenK;
	for (nrSp=0;nrSp<nrMatchedIndexer;nrSp++){
		DisplacementInTheSpot(x[0],x[1],x[2],Lsd,spotsYZO[nrSp][5],spotsYZO[nrSp][6],spotsYZO[nrSp][4],wedge,chi,&DisplY,&DisplZ);
		CorrectForOme(spotsYZO[nrSp][5]-DisplY,spotsYZO[nrSp][6]-DisplZ,Lsd,spotsYZO[nrSp][4],Wavelength,wedge,&ys,&zs,&Omega);
		SpotsYZOGCorr[nrSp][0] = ys;
		SpotsYZOGCorr[nrSp][1] = zs;
		SpotsYZOGCorr[nrSp][2] = Omega;
		lenK = sqrt((Lsd*Lsd)+(ys*ys)+(zs*zs));
		Radius = sqrt((ys*ys) + (zs*zs));
		Theta = 0.5*atand(Radius/Lsd);
		double g1,g2,g3;
		SpotToGv(Lsd/lenK,ys/lenK,zs/lenK,Omega,Theta,&g1,&g2,&g3);
		SpotsYZOGCorr[nrSp][3] = g1;
		SpotsYZOGCorr[nrSp][4] = g2;
		SpotsYZOGCorr[nrSp][5] = g3;
		SpotsYZOGCorr[nrSp][6] = spotsYZO[nrSp][7];
	}
	double **TheorSpotsYZWE;TheorSpotsYZWE=allocMatrix(nTspots,9);
	for (i=0;i<nTspots;i++){for (j=0;j<9;j++){TheorSpotsYZWE[i][j] = TheorSpots[i][j];}}
	int sp,nTheorSpotsYZWER,nMatched=0,RowBest=0;
	double GObs[3],GTheors[3],NormGObs,NormGTheors,DotGs,**TheorSpotsYZWER,Numers,Denoms,*Angles,minAngle;
	double diffLenM,diffOmeM;
	TheorSpotsYZWER=allocMatrix(MaxNSpotsBest,9);Angles=malloc(MaxNSpotsBest*sizeof(*Angles));
	for (sp=0;sp<nrMatchedIndexer;sp++){
		nTheorSpotsYZWER=0;
		GObs[0]=SpotsYZOGCorr[sp][3];GObs[1]=SpotsYZOGCorr[sp][4];GObs[2]=SpotsYZOGCorr[sp][5];
		NormGObs = CalcNorm3(GObs[0],GObs[1],GObs[2]);
		for (i=0;i<nTspots;i++){
			if (((int)TheorSpotsYZWE[i][7]==(int)SpotsYZOGCorr[sp][6])&&(fabs(SpotsYZOGCorr[sp][2]-TheorSpotsYZWE[i][2])<3.0)){
				for (j=0;j<9;j++){TheorSpotsYZWER[nTheorSpotsYZWER][j]=TheorSpotsYZWE[i][j];}
				GTheors[0]=TheorSpotsYZWE[i][3];
				GTheors[1]=TheorSpotsYZWE[i][4];
				GTheors[2]=TheorSpotsYZWE[i][5];
				DotGs = ((GTheors[0]*GObs[0])+(GTheors[1]*GObs[1])+(GTheors[2]*GObs[2]));
				NormGTheors = CalcNorm3(GTheors[0],GTheors[1],GTheors[2]);
				Numers = DotGs;
				Denoms = NormGObs*NormGTheors;
				Angles[nTheorSpotsYZWER] = fabs(acosd(Numers/Denoms));
				nTheorSpotsYZWER++;
			}
		}
		if (nTheorSpotsYZWER==0)continue;
		minAngle = 1000000;
		for (i=0;i<nTheorSpotsYZWER;i++){
			if (Angles[i]<minAngle){
				minAngle=Angles[i];
				RowBest=i;
			}
		}
		diffLenM = CalcNorm2((SpotsYZOGCorr[sp][0]-TheorSpotsYZWER[RowBest][0]),(SpotsYZOGCorr[sp][1]-TheorSpotsYZWER[RowBest][1]));
		diffOmeM = fabs(SpotsYZOGCorr[sp][2]-TheorSpotsYZWER[RowBest][2]);
		if (minAngle < 1){
			MatchDiff[nMatched][0] = minAngle;
			MatchDiff[nMatched][1] = diffLenM;
			MatchDiff[nMatched][2] = diffOmeM;
			SpotsComp[nMatched][0] = spotsYZO[sp][3];
			for (i=0;i<6;i++){
				SpotsComp[nMatched][i+1]=SpotsYZOGCorr[sp][i];
				SpotsComp[nMatched][i+7]=TheorSpotsYZWER[RowBest][i];
			}
			SpotsComp[nMatched][13]=spotsYZO[sp][0];
			SpotsComp[nMatched][14]=spotsYZO[sp][1];
			SpotsComp[nMatched][15]=spotsYZO[sp][2];
			SpotsComp[nMatched][16]=spotsYZO[sp][4];
			SpotsComp[nMatched][17]=spotsYZO[sp][5];
			SpotsComp[nMatched][18]=spotsYZO[sp][6];
			SpotsComp[nMatched][19]=minAngle;
			SpotsComp[nMatched][20]=diffLenM;
			SpotsComp[nMatched][21]=diffOmeM;
			for (i=0;i<8;i++){SpList[nMatched][i]=spotsYZO[sp][i];}
			SpList[nMatched][8]=TheorSpotsYZWER[RowBest][8];
			nMatched++;
		}
	}
	*nSpotsComp = nMatched;
	Error[0]=0;Error[1]=0;Error[2]=0;
	for (i=0;i<nMatched;i++){
		Error[0] += fabs(MatchDiff[i][1]/nMatched);
		Error[1] += fabs(MatchDiff[i][2]/nMatched);
		Error[2] += fabs(MatchDiff[i][0]/nMatched);
	}
	FreeMemMatrix(MatchDiff,nrMatchedIndexer);
	FreeMemMatrix(hkls,nhkls);
	FreeMemMatrix(TheorSpots,MaxNSpotsBest);
	FreeMemMatrix(SpotsYZOGCorr,nrMatchedIndexer);
	FreeMemMatrix(TheorSpotsYZWE,nTspots);
	FreeMemMatrix(TheorSpotsYZWER,MaxNSpotsBest);
	free(Angles);
}

int main(int argc, char *argv[])
{
	if (argc != 4){
		printf("Usage: FitGrain Folder Parameters.txt GrainID\n");
		return;
	}
    clock_t start, end;
    double diftotal;
    start = clock();
    char aline[MAX_LINE_LENGTH];
    int LowNr;
    int GrainID = atoi(argv[3]);
    FILE *fileParam;
    char *ParamFN;
    ParamFN = argv[2];
    fileParam = fopen(ParamFN,"r");
    char *str, dummy[MAX_LINE_LENGTH];
    double tx, ty, tz, Lsd, p0, p1, p2, RhoD, yBC, zBC, wedge, px, a, 
		b, c, alpha, beta, gamma, OmegaRanges[20][2], BoxSizes[20][4],
		RingRadii[200], MaxRingRad, MaxTtheta, Wavelength, MinEta;
    int NrPixels, nOmeRanges=0, nBoxSizes=0, cs=0, RingNumbers[200], cs2=0;
    while (fgets(aline,MAX_LINE_LENGTH,fileParam)!=NULL){
		str = "tx ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &tx);
			continue;
		}
		str = "ty ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &ty);
			continue;
		}
		str = "tz ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &tz);
			continue;
		}
		str = "Lsd ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &Lsd);
			continue;
		}
		str = "p0 ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &p0);
			continue;
		}
		str = "p1 ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &p1);
			continue;
		}
		str = "p2 ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &p2);
			continue;
		}
		str = "RhoD ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &RhoD);
			continue;
		}
		str = "BC ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf %lf",dummy, &yBC, zBC);
			continue;
		}
		str = "Wedge ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &wedge);
			continue;
		}
		str = "px ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &px);
			continue;
		}
		str = "Wavelength ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &Wavelength);
			continue;
		}
		str = "ExcludePoleAngle ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %lf",dummy, &MinEta);
			continue;
		}
		str = "NrPixels ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr == 0){
			sscanf(aline,"%s %d",dummy, &NrPixels);
			continue;
		}
		str = "OmegaRange ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr==0){
			sscanf(aline,"%s %lf %lf", dummy, &OmegaRanges[nOmeRanges][0], &OmegaRanges[nOmeRanges][1]);
			nOmeRanges++;
			continue;
		}
		str = "BoxSize ";
		LowNr = strncmp(aline,str,strlen(str));
		if (LowNr==0){
			sscanf(aline,"%s %lf %lf %lf %lf", dummy, &BoxSizes[nBoxSizes][0], &BoxSizes[nBoxSizes][1],
				&BoxSizes[nBoxSizes][2], &BoxSizes[nBoxSizes][3]);
			nBoxSizes++;
			continue;
		}
        str = "RingNumbers ";
        LowNr = strncmp(aline,str,strlen(str));
        if (LowNr==0){
            sscanf(aline,"%s %d", dummy, &RingNumbers[cs]);
            cs++;
            continue;
        }
        str = "RingRadii ";
        LowNr = strncmp(aline,str,strlen(str));
        if (LowNr==0){
            sscanf(aline,"%s %lf", dummy, &RingRadii[cs2]);
            cs2++;
            continue;
        }
        str = "MaxRingRad ";
        LowNr = strncmp(aline,str,strlen(str));
        if (LowNr==0){
            sscanf(aline,"%s %lf", dummy, &MaxRingRad);
            continue;
        }
	}
	int i,j,k;
	MaxTtheta = rad2deg*atan(MaxRingRad/Lsd);
	char *hklfn = "hkls.csv";
	FILE *hklf = fopen(hklfn,"r");
	fgets(aline,MAX_LINE_LENGTH,hklf);
	int Rnr;
	double tht;
	int n_hkls = cs,nhkls = 0;
	int h, kt, l;
	double ds;
	double **hkls;
	hkls = allocMatrix(5000,7);
	while (fgets(aline,MAX_LINE_LENGTH,hklf)!=NULL){
		sscanf(aline, "%d %d %d %lf %d %s %s %s %lf %s %s",&h,&kt,&l,&ds,&Rnr,dummy,dummy,dummy,&tht,dummy,dummy);
		if (tht > MaxTtheta/2) break;
		for (i=0;i<cs;i++){
			if(Rnr == RingNumbers[i]){
				hkls[nhkls][0] = h;
				hkls[nhkls][1] = kt;
				hkls[nhkls][2] = l;
				hkls[nhkls][3] = ds;
				hkls[nhkls][4] = tht;
				hkls[nhkls][5] = RingRadii[i];
				hkls[nhkls][6] = RingNumbers[i];
				nhkls++;
			}
		}
	}
	CalcAngleErrors(nSpotsYZO,nhkls,nOmeRanges,Ini,spotsYZO,hkls,Lsd,Wavelength,OmegaRanges,BoxSizes,
					MinEta,wedge,chi,SpotsComp,Splist,ErrorIni,&nSpotsComp);
}
