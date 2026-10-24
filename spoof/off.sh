#!/bin/sh
rm -f /run/systemd/system/acvs.service.d/91-forcespoof.conf && systemctl daemon-reload && systemctl restart acvs
