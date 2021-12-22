#!/bin/sh
# Licensed under the zlib/libpng license (same as NSIS)
#
# Script to run all tests
#
# Usage: run.sh [<helper dir [<Windows architecture> [<test dir>]]]

set -e

SOURCEDIR=$(cd "$(dirname "$0")"; pwd -P)
TOPDIR=$(cd "${SOURCEDIR}/.."; pwd -P)
HELPERDIR=${1:-"${TOPDIR}/build"}

. "${SOURCEDIR}/common/funcs.sh"

main() {
       for r in ${SOURCEDIR}/*/run.sh
       do
               $r "${HELPERDIR}" "${WINEARCH}" "${PWD}"
       done
}

after() {
	return 0
}

[ $# -gt 1 ] && shift
wine_set_up $@
err=$?
case ${err} in
0)
	main
	;;
2)
	echo "Wine is not available"
	;;
95)
	echo "Wine with ${WINEARCH} architecture is not supported on this host"
	;;
*)
	echo "Failed to boot wine"
	;;
esac
wine_clean_up
