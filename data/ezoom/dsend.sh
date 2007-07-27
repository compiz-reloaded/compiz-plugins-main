#!/bin/sh 
# Sends a dbus option to a plugin
#
dbus-send --type=method_call --dest=org.freedesktop.compiz /org/freedesktop/compiz/$1/allscreens/$2 org.freedesktop.compiz.activate string:'root' int32:`xwininfo -root | grep id: | awk '{ print $4 }'` $3 $4 $5 $6 $7 $8 $9 ${10} ${11} ${12} ${13} ${14}
echo dbus-send --type=method_call --dest=org.freedesktop.compiz /org/freedesktop/compiz/$1/allscreens/$2 org.freedesktop.compiz.activate string:'root' int32:`xwininfo -root | grep id: | awk '{ print $4 }'` $3 $4 $5 $6 $7 $8 $9 ${10} ${11} ${12} ${13} ${14}
