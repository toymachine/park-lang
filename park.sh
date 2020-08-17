#!/bin/sh

docker run  -p8090:8090 -v $(pwd):/mnt/park -w /mnt/park -it toymachine/park-lang ./park.ubuntu.fossa $1
