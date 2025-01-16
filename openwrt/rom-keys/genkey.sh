#!/bin/sh

openssl genrsa -F4 -out wt.key 2048 2>/dev/null

openssl req -batch -new -x509 -key wt.key -out wt.crt

