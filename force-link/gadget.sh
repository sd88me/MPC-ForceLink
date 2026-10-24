#!/bin/sh
# Force: expose a 2-port USB MIDI gadget on the rear USB-B (usb-mux external). "gadget.sh up|down"
G=/sys/kernel/config/usb_gadget/fl
case "$1" in
up)
  echo external > /sys/devices/platform/usb-mux/state
  mkdir -p $G && cd $G
  echo 0x1d6b > idVendor; echo 0x0104 > idProduct
  mkdir -p strings/0x409 && echo "Force Link" > strings/0x409/product && echo "fl" > strings/0x409/manufacturer
  mkdir -p functions/midi.usb0 && echo 2 > functions/midi.usb0/in_ports && echo 2 > functions/midi.usb0/out_ports
  mkdir -p configs/c.1 && ln -sf $G/functions/midi.usb0 configs/c.1/
  echo ff580000.usb > UDC ;;
down)
  echo "" > $G/UDC; rm -f $G/configs/c.1/midi.usb0; rmdir $G/configs/c.1 $G/functions/midi.usb0 $G/strings/0x409 $G
  echo internal > /sys/devices/platform/usb-mux/state ;;
esac
