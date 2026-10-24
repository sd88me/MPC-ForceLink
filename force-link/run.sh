#!/bin/sh
# Force Link surface test: stop MPC, replay handshake, draw pattern, hold, restore MPC.
D=/tmp/fl; cd $D
restore() { kill $R0 $R1 2>/dev/null; systemctl start acvs; echo "restored $(date)" >> $D/log; }
trap restore EXIT
systemctl stop acvs; sleep 3
echo "stopped $(date)" > $D/log
amidi -l >> $D/log 2>&1
amidi -p hw:1,0,0 -d > rx0 2>&1 & R0=$!
amidi -p hw:1,0,1 -d > rx1 2>&1 & R1=$!
sleep 1
amidi -p hw:1,0,0 -s handshake.syx >> $D/log 2>&1; echo "handshake sent" >> $D/log
sleep 1
amidi -p hw:1,0,0 -s pattern.syx >> $D/log 2>&1; echo "pattern sent" >> $D/log
i=0; while [ $i -lt 10 ]; do sleep 4; amidi -p hw:1,0,0 -s ping.syx 2>>$D/log; i=$((i+1)); done
echo "hold done" >> $D/log
