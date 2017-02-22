#!/bin/bash -eu

if [[ ${#*} != 2 ]]
then
  echo "Provide the number of Nodes to use!"
  echo "EG. ./IndexRefine.sh 6 Params.txt"
  exit 1
fi

source ${HOME}/.MIDAS/paths

cp SpotsToIndex.csv SpotsToIndexIn.csv
cat SpotsToIndex.csv |sort|uniq|less > SpotsToIndexUnq.csv
mv SpotsToIndexUnq.csv SpotsToIndex.csv
fldr=$( pwd )

${PFDIR}/SHMOperators.sh

nNODES=${1}
export nNODES
if [ ${nNODES} == 7 ] && [ ${MACHINE_NAME} == 'ort' ]
then
	MACHINE_NAME="ortextra"
fi
echo "MACHINE NAME is ${MACHINE_NAME}"

mkdir -p Output
mkdir -p Results
mkdir -p logs
${SWIFTDIR}/swift -config ${PFDIR}/sites.conf -sites ${MACHINE_NAME} ${PFDIR}/IndexRefine.swift \
 -folder=${fldr}
${BINFOLDER}/ProcessGrains $2
ls -lh
