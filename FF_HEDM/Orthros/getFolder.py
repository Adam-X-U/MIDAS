import sys, glob

PSFile = sys.argv[1]
LayerNr = int(sys.argv[2])
f = open(PSFile,'r')
PSContents = f.readlines()
printed = 0
for line in PSContents:
	if 'FolderName' in line:
		words = line.split('_')
		for word in words:
			if 'Layer' in word:
				LayerNrThis = int(word[5:])
				if LayerNrThis == LayerNr:
					print line
					printed = 1
folderStem = sys.argv[3] + '*'
if printed == 0:
	folderList = glob.glob(folderStem)
	print folderList[1]
