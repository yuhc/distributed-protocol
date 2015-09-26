#!bin/bash

LOGDIR="./data"
LOGFILE="$LOGDIR/broadcast.log"
PROC_NUM=5
PROC_IP="127.0.0.1"
echo -e "PROCESS INFORMATION\n" > $LOGFILE

for (( i = 0; i < $PROC_NUM; i++ ))
do
    ./bin/broadcast $i $PROC_IP &
    pid[$i]=$!
    echo "- Process ($i): ${pid[$i]}" >> $LOGFILE
done

echo -e "\nBROADCAST LOG\n" >> $LOGFILE
echo -e "TIME\tPROC\tEVENT\tMESSAGE ID" >> $LOGFILE

