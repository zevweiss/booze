#!/bin/bash

if [ $# -ne 2 ]; then
	echo >&2 "Usage: `basename $0` DIR MNTPT"
	exit 1
fi

rootdir="$(readlink -f "$1")"
mntpt="$2"

. ./booze.sh

set -u
shopt -s nullglob
shopt -s dotglob

dostat()
{
	local st ino mode rest
	st=$(stat -c '%i 0x%f %h %u %g %d %s %b %X %Y %Z' "$1") || return 1
	read ino mode rest <<<"$st"
	printf '%d %o %s\n' $ino $mode "$rest"
}

pt_getattr()
{
	[ -e "$rootdir/$1" ] || { booze_err=-$ENOENT; return 1; }
	booze_out=`dostat "$rootdir/$1"`
}

pt_access()
{
	[ -e "$rootdir/$1" ] || { booze_err=-$ENOENT; return 1; }
	[ "$2" == "$F_OK" ] && return 0

	if { [ "$(($2 & $R_OK))" -ne 0 ] && ! [ -r "$rootdir/$1" ]; } ||
		{ [ "$(($2 & $W_OK))" -ne 0 ] && ! [ -w "$rootdir/$1" ]; } ||
		{ [ "$(($2 & $X_OK))" -ne 0 ] && ! [ -x "$rootdir/$1" ]; }; then
		booze_err=-$EACCES
		return 1
	else
		return 0
	fi
}

pt_readlink()
{
	[ -e "$rootdir/$1" ] || { booze_err=-$ENOENT; return 1; }
	[ -l "$rootdir/$1" ] || { booze_err=-$EINVAL; return 1; }
	booze_out="$(readlink "$rootdir/$1")"
	return $?
}

pt_readdir()
{
	[ -e "$rootdir/$1" ] || { booze_err=-$ENOENT; return 1; }
	[ -d "$rootdir/$1" ] || { booze_err=-$ENOTDIR; return 1; }
	booze_out="./.."
	# try to handle weird filenames correctly
	local prefix="$rootdir/$1/"
	for p in "$prefix"*; do
		booze_out+="/${p#$prefix}"
	done
	return 0
}

pt_mknod()
{
	local perms=`printf %o $(($2 & 07777))`

	if S_ISREG $2; then
		[ -e "$rootdir/$1" ] && { booze_err=-$EEXIST; return 1; }
		touch "$rootdir/$1" &&  chmod $perms "$rootdir/$1"
		return
	elif S_ISFIFO $2; then
		mkfifo -m $perms "$rootdir/$1"
		return
	else
		booze_err=-$EOPNOTSUPP
		return 1
	fi
}

pt_mkdir() { mkdir -m `printf %o $2` "$rootdir/$1"; }
pt_unlink() { rm -f "$rootdir/$1"; }
pt_rmdir() { rmdir "$rootdir/$1"; }
pt_symlink() { ln -sn "$rootdir/$1" "$2"; }
pt_rename() { mv -f "$rootdir/$1" "$rootdir/$2"; }
pt_link() { ln -n "$rootdir/$1" "$rootdir/$2"; }
pt_chmod() { chmod `printf %o $(($2 & 07777))` "$rootdir/$1"; }
pt_chown() { chown -h $2:$3 "$rootdir/$1"; }
pt_truncate() { truncate -s $2 "$rootdir/$1"; }
pt_utimens() { touch -h -d @$2 "$rootdir/$1" && touch -h -d @$3 "$rootdir/$1"; }

pt_open()
{
	if ! [ -e "$rootdir/$1" ]; then
		if [ "$(($2 & $O_CREAT))" -ne 0 ]; then
			touch "$rootdir/$1"
		else
			booze_err=-$ENOENT
			return 1
		fi
	fi

	if [ "$(($2 & $O_TRUNC))" -ne 0 ]; then
		> "$rootdir/$1"
	fi

	return 0
}

pt_read()
{
	dd if="$rootdir/$1" bs=1 count=$2 skip=$3
}

pt_write()
{
	dd of="$rootdir/$1" bs=1 count=$2 seek=$3 || {
		booze_err=-$EIO
		booze_out=-$EIO
		return 1
	}
	booze_out="$2"
	return 0
}

pt_statfs() { booze_out="$(stat -L -f "$rootdir" -c "%S %b %f %a %c %d %l")"; }

declare -A passthrough_ops

for name in ${BOOZE_CALL_NAMES[@]}; do
	if [ "`type -t pt_$name`" == "function" ]; then
		passthrough_ops[$name]=pt_$name
	fi
done

booze passthrough_ops "$mntpt"
