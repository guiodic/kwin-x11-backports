#! /usr/bin/env bash
$EXTRACTRC *.ui >> rc.cpp || exit 11
$XGETTEXT *.cpp -o $podir/kcm_kwintabbox_x11.pot
rm -f rc.cpp
