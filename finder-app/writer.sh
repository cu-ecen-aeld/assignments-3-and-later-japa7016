#!/bin/bash

writefile=$1
writestr=$2

if [ "$#" -ne 2 ]
then
	echo "Error: Two Arguments Required!!"
	exit 1
fi

path=$(dirname "$writefile")
mkdir -p "$path"

if echo "$writestr" > "$writefile"
then
	echo "Content Written Succesfully"
else
	echo "Error creating file"
	exit 1
fi
