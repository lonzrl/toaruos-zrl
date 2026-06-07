#!/bin/bash

MAJOR=$(grep __kernel_version_major kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')
MINOR=$(grep __kernel_version_minor kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')
LOWER=$(grep __kernel_version_lower kernel/sys/version.c | sed s'/.*= \(.*\);/\1/')

cat << EOF
PRETTY_NAME="ZRL ${MAJOR}.${MINOR}"
NAME="ZRL"
VERSION_ID="${MAJOR}.${MINOR}.${LOWER}"
VERSION="${MAJOR}.${MINOR}.${LOWER}"
ID=zrl
HOME_URL="https://zrl.os"
EOF
