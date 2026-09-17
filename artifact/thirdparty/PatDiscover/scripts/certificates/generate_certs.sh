#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

cd "$SCRIPT_DIR"/../../data/certificates || exit

# Remove current certificates
rm *.pem
rm *.srl

# Generate Root CA key and cert
openssl req -x509 -newkey rsa:4096 -days 365 -nodes -keyout ca-key.pem -out ca-cert.pem -subj "/CN=example.com/emailAddress=example@example.com"

# Server
openssl req -newkey rsa:4096 -nodes -keyout server-key.pem -out server-req.pem -subj "/CN=server.example.com/emailAddress=example@example.com"
openssl x509 -req -in server-req.pem -days 60 -CA ca-cert.pem -CAkey ca-key.pem -CAcreateserial -out server-cert.pem -extfile localhost.cnf

# Client
openssl req -newkey rsa:4096 -nodes -keyout client-key.pem -out client-req.pem -subj "/CN=client.example.com/emailAddress=example@example.com"
openssl x509 -req -in client-req.pem -days 60 -CA ca-cert.pem -CAkey ca-key.pem -CAcreateserial -out client-cert.pem -extfile localhost.cnf

# Trusted Authority
openssl req -newkey rsa:4096 -nodes -keyout ta-key.pem -out ta-req.pem -subj "/CN=ta.example.com/emailAddress=example@example.com"
openssl x509 -req -in ta-req.pem -days 60 -CA ca-cert.pem -CAkey ca-key.pem -CAcreateserial -out ta-cert.pem -extfile localhost.cnf
