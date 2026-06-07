#!/bin/sh
# Build a .deb package for lockay
# Usage: ./build-deb.sh
set -e

VERSION="0.1.0"
NAME="lockay"
DEB="${NAME}_${VERSION}_amd64.deb"
PKGROOT="/tmp/lockay-deb-$$"

echo "Building ${DEB}..."

# Build binary
cd "$(dirname "$0")/../.."
make clean && make

# Set up package structure
mkdir -p "${PKGROOT}/DEBIAN"
mkdir -p "${PKGROOT}/usr/local/bin"
mkdir -p "${PKGROOT}/usr/share/doc/${NAME}"

cp build/lockay "${PKGROOT}/usr/local/bin/"
cp README.md "${PKGROOT}/usr/share/doc/${NAME}/"

cat > "${PKGROOT}/DEBIAN/control" << EOF
Package: ${NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.17), git
Maintainer: Steven-ZN <3154469801@qq.com>
Description: capability shell for coding agents
 lockay controls what code an agent can change (linelock)
 and what commands an agent can run (cmdlock).
 .
 chmod for lines of code. sudo for autonomous agents.
EOF

dpkg-deb --build "${PKGROOT}" "${DEB}"
echo "Built: ${DEB}"

rm -rf "${PKGROOT}"
