#!/bin/sh
rm -f /tmp/forcespoof.log
cp /data/mpc-addins/forcespoof/91-forcespoof.conf /run/systemd/system/acvs.service.d/91-forcespoof.conf && systemctl daemon-reload && systemctl restart acvs
