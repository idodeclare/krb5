#!/bin/ksh

set -o pipefail

TMPILLUMOS=/tmp/find_illumos_bybn.txt

function cleanup {
    rm -f "$TMPILLUMOS"
}

if [ $# -ne 1 ]; then
	>&2 echo "Usage: $0 <illumos root>"
	exit 1
fi
target="$1"

if [ ! -f README ] || [ ! -d src ]; then
	>&2 echo "ERROR: $0 should be run from krb5 root."
	exit 1
fi

cd "$target" || exit 2
find usr/src -type f > "$TMPILLUMOS" || exit 2
trap cleanup EXIT
cd "$OLDPWD" || exit 2

find -d -s src \
    ! -path '*/ccapi/*' \
    ! -path '*/test/*' \
    ! -path '*/windows/*' \
    ! -path 'src/appl/bsd/*' \
    ! -path 'src/appl/gss-sample/*' \
    ! -path 'src/appl/gssftp/*' \
    ! -path 'src/appl/libpty/*' \
    ! -path 'src/appl/sample/*' \
    ! -path 'src/appl/simple/*' \
    ! -path 'src/appl/telnet/*' \
    ! -path 'src/appl/user_user/*' \
    ! -path 'src/clients/kcpytkt/*' \
    ! -path 'src/clients/kdeltkt/*' \
    ! -path 'src/clients/ksu/*' \
    ! -path 'src/include/kim/*' \
    ! -path 'src/kadmin/testing/*' \
    ! -path 'src/kim/*' \
    ! -path 'src/lib/apputils/*' \
    ! -path 'src/lib/crypto/*' \
    ! -path 'src/lib/kadm5/unit-test/*' \
    ! -path 'src/lib/krb5/unicode/*' \
    ! -path 'src/lib/rpc/*' \
    ! -path 'src/plugins/audit/*' \
    ! -path 'src/tests/*' \
    ! -path 'src/util/verto/*' \
    ! -path 'src/util/wshelper/*' \
    ! -name 't_*' \
    -name '*.[hc]' -type f -print0 | \
    xargs -0 ./bneq.pl "$TMPILLUMOS"
exit $?
