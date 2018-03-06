#
# Copyright (c) 2014, UChicago Argonne, LLC
# See LICENSE file.
#

type file;

app (file out) Medians (string pf, int layernr, file trial)
{
	runmedianparallel pf layernr stdout=@filename(out);
}

app (file outm) Images (string paramf, int layern, int filenr, file inp)
{
	runimageprocessingparallel paramf layern filenr stdout=@filename(outm);
}

app (file done) PlaceHolder (int prefix, string outfolder, file out[])
{
	echo2 prefix outfolder stdout=@filename(done);
}

app (file done) PlaceHolder2 (string prefix,file tmp)
{
	echo prefix stdout=@filename(done);
}

app runfitorientation (string pf, int nr, file mmapdone)
{
	fitorientation pf nr;
}

app (file mmapdone) mmapcode (string paramfn, file imagedone)
{
	mmaps paramfn stdout=@filename(mmapdone);
}

app (file done) initialsetup ( string paramfn, int ffseed, int dogrid )
{
	setupNF paramfn ffseed dogrid stdout=@filename(done);
}

type BulkNames{
	string paramfn;
	string datadir;
}

# Parameters to be modified ############

string paramf = arg("FileData","/data/tomo1/NFTest/ParametersGoldApril14.txt");
int NrDistances = toInt(arg("NrDistances","3"));
int NrFilesPerDistance = toInt(arg("NrFilesPerDistance","180"));
int startnr = toInt(arg("StartNumber","1"));
int endnr = toInt(arg("EndNumber","2000"));
int DoPeakSearch = toInt(arg("DoPeakSearch","1"));
int ffseed = toInt(arg("FFSeedOrientations","1"));
int DoFullLayer = toInt(arg("DoFullLayer","1"));
int DoGrid = toInt(arg("DoGrid","1"));

# End parameters #######################

# Read data
BulkNames NameData[] = readData(paramf);

BulkNames dat = NameData[0];
string paramfile = dat.paramfn;
string direct = dat.datadir;
string outfolder = strcat(direct,"/output/");

# Do the initial setup
string fn = strcat(outfolder,"initialsetup.csv");
file setupdone <single_file_mapper;file=fn>;
setupdone = initialsetup(paramfile,ffseed,DoGrid);

## Whether do peak search or not
string fn2 = strcat(outfolder,"imageprocessing.txt");
file imagesdone <single_file_mapper;file=fn2>;
if (DoPeakSearch == 1){
	trace("Doing peaksearch.");
	string prefixC = "LayersCompleted.txt";
	file simCout[]<simple_mapper;location=outfolder,prefix=prefixC,suffix=".out">;
	foreach distance in [1:NrDistances] {
		string prefix1 = strcat("Median_",distance);
		file simAout <simple_mapper;location=outfolder,prefix=prefix1,suffix=".out">;
		file simBout[];
		simAout = Medians(paramfile,distance,setupdone);
		foreach FileNr in [0:(NrFilesPerDistance-1)]{
			file simx<simple_mapper;location=outfolder,prefix=strcat("ImageProcessing_",FileNr),suffix=".out">;
			simx = Images(paramfile, distance, FileNr,simAout);
			if (FileNr %% 100 == 0){
				int simAidx = (FileNr%/100) + (distance-1)*(NrFilesPerDistance%/100);
				tracef("%d %d\n",FileNr,simAidx);
				simBout[simAidx] = simx;
			}
		}
		simCout[distance] = PlaceHolder(NrFilesPerDistance,outfolder,simBout);
	}
	string printoutimages = "All images done.";
	imagesdone = PlaceHolder2(printoutimages,simCout);
} else {
	string prefix2 = "ImageProcessing was not done.";
	tracef("%s\n",prefix2);
	imagesdone = PlaceHolder2(prefix2,setupdone);
}

if (DoFullLayer == 1){
	## Now MMap Images
	string fn3 = strcat(outfolder, "mmapdone.txt");
	file mmapdone <single_file_mapper;file=fn3>;
	mmapdone = mmapcode(paramfile,imagesdone);
	## Now do FitOrientation
	foreach i in [startnr:endnr] {
		runfitorientation(paramfile,i,mmapdone);
	}
}
