#!/bin/bash

# 1024 or 512.   512 saves memory...
BITS=512
pushd /tmp

openssl genrsa -out tls.ca_key.pem $BITS
openssl genrsa -out tls.key_$BITS.pem $BITS
openssl rsa -in tls.key_$BITS.pem -out tls.key_$BITS -outform DER
cat > certs.conf <<EOF
[ req ]
distinguished_name = req_distinguished_name
prompt = no

[ req_distinguished_name ]
O = psychoplug
CN = 127.0.0.1
EOF
openssl req -out tls.ca_x509.req -key tls.ca_key.pem -new -config certs.conf 
openssl req -out tls.x509_$BITS.req -key tls.key_$BITS.pem -new -config certs.conf 
openssl x509 -req -in tls.ca_x509.req  -out tls.ca_x509.pem -sha256 -days 5000 -signkey tls.ca_key.pem 
openssl x509 -req -in tls.x509_$BITS.req  -out tls.x509_$BITS.pem -sha256 -CAcreateserial -days 5000 -CA tls.ca_x509.pem -CAkey tls.ca_key.pem 
openssl x509 -in tls.ca_x509.pem -outform DER -out tls.ca_x509.cer
openssl x509 -in tls.x509_$BITS.pem -outform DER -out tls.x509_$BITS.cer

xxd -i tls.key_$BITS
xxd -i tls.x509_$BITS.cer

popd
