#!/bin/bash

#example ./gradeit.sh dir1 dir2 logfile

DIR1=$1
DIR2=$2
LOG=${3:-${DIR2}/LOG.txt}

INS="`seq 0 9`"
INPRE="inputs/input"
OUTPRE="out"
ALGOS="N  S  L  C  F"

[[ ${#} -lt 2 ]] && echo "usage: $0 <refoutdir> <youroutdir>" && exit
[[ ! -d ${DIR1} ]] && echo "refoutdir <$DIR1> does not exist"  && exit
[[ ! -d ${DIR2} ]]  && echo "youroutdir <$DIR2> does not exist"  && exit

DARGS=         # nothing
DARGS="-q"
CHKSUM="md5sum"

declare -ai counters
declare -i x=0
declare -i total=0
declare -i count=0
for s in ${ALGOS}; do
        let counters[$x]=0
        let x=$x+1
done

X1=`id -un`;X2=`id -ua`;X3=`hostname -s`;X4=$(date +"%m%d/%H%M");X5=`gcc -v 2>&1 | grep "^gcc" | cut -d' ' -f3` ; X6="/home/${X1}/${X2}/${X3}/${X4}/${X5}" ; X7=`echo ${X6} | ${CHKSUM}` ; echo "${X7} ${X6}" ; echo;

rm -f ${LOG}
echo "gradeit.sh ${DIR1} ${DIR2} ${LOG}" > ${LOG}
echo "         ${ALGOS}" 

for F in ${INS}; do
    OUTLINE=`printf "%-7s" "${F}"`
    x=0
    for A in ${ALGOS}; do 
        let count=`expr ${count} + 1`
	    OUTF="out_${F}_${A}"
        if [[ ! -e ${DIR1}/${OUTF} ]]; then
            echo "${DIR1}/${OUTF} does not exist" >> ${LOG}
            OUTLINE=`printf "%s  o" "${OUTLINE}"`
            continue;
        elif [[ ! -e ${DIR2}/${OUTF} ]]; then
            echo "${DIR2}/${OUTF} does not exist" >> ${LOG}
            OUTLINE=`printf "%s  o" "${OUTLINE}"`
            continue;
        else
	        # echo "diff -b ${DARGS} ${DIR1}/${OUTF} ${DIR2}/${OUTF}"
	        DIFF=`diff -b ${DARGS} ${DIR1}/${OUTF} ${DIR2}/${OUTF}`
	        if [[ "${DIFF}" == "" ]]; then
	            OUTLINE=`printf "%s  ." "${OUTLINE}"`
	            let counters[$x]=`expr ${counters[$x]} + 1`
	        else
	            echo "diff -b ${DARGS} ${DIR1}/${OUTF} ${DIR2}/${OUTF}" >> ${LOG}
	            SUMX=`egrep "^SUM" ${DIR1}/${OUTF}`
	            SUMY=`egrep "^SUM" ${DIR2}/${OUTF}`
	            echo "   DIR1-SUM ==> ${SUMX}" >> ${LOG}
	            echo "   DIR2-SUM ==> ${SUMY}" >> ${LOG}
	            OUTLINE=`printf "%s  x" "${OUTLINE}"`
	        fi
        fi
	    let x=$x+1
    done
    echo "${OUTLINE}"
done


OUTLINE=`printf "%-7s" "SUM"`
x=0
for A in ${ALGOS}; do 
    OUTLINE=`printf "%s %2d" "${OUTLINE}" "${counters[$x]}"`
    let total=`expr ${counters[$x]} + ${total}`
    let x=$x+1
done
echo "${OUTLINE}"
echo "TOTAL: ${total} out of ${count}"

