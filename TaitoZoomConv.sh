#!/bin/bash

function convert {
	echo converting $GAME $1
	./TaitoZoom $ROM $1 "converted/${GAME}_$1.mid"
}

function convertgame {
	GAME=$1
	ROM=$2
	let songcount=$(./TaitoZoom $ROM | sed -rn 's/[^[:digit:]]*([[:digit:]]+)/\1/p')-1
	for i in `seq 0 $songcount`; { convert $i; };
}

gcc -o TaitoZoom TaitoZoom.c
mkdir -p converted

# To use the script, you must extract the .14 file from the ROM archive
# and place it in the same directory as this script.
convertgame raystorm e24-09.14
convertgame ftimpact e25-10.14
convertgame gdarius  e39-07.14
