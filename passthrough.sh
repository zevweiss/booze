
if [ $# -ne 2 ]; then
	echo >&2 "Usage: `basename $0` DIR MNTPT"
	exit 1
fi

rootdir="$1"
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

booze_getattr()
{
	[ -e "$rootdir/$1" ] || { booze_err=-$ENOENT; return 1; }
	booze_out=`dostat "$rootdir/$1"`
}

booze_access()
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

booze_readlink()
{
	[ -e "$rootdir/$1" ] || { booze_err=-$ENOENT; return 1; }
	[ -l "$rootdir/$1" ] || { booze_err=-$EINVAL; return 1; }
	booze_out="$(readlink "$rootdir/$1")"
	return $?
}

booze_readdir()
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

booze_mknod()
{
	local perms=`printf %o $(($2 & 07777))`
	echo >&2 "###>>> mknod $*"

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

booze_mkdir() { mkdir -m `printf %o $2` "$rootdir/$1"; }
booze_unlink() { rm -f "$rootdir/$1"; }
booze_rmdir() { rmdir "$rootdir/$1"; }
booze_symlink() { ln -sn "$rootdir/$1" "$2"; }
booze_rename() { mv -f "$rootdir/$1" "$rootdir/$2"; }
booze_link() { ln -n "$rootdir/$1" "$rootdir/$2"; }
booze_chmod() { chmod `printf %o $(($2 & 07777))` "$rootdir/$1"; }
booze_chown() { chown -h $2:$3 "$rootdir/$1"; }
booze_truncate() { truncate -s $2 "$rootdir/$1"; }
booze_utimens() { touch -h -d @$2 "$rootdir/$1" && touch -h -d @$3 "$rootdir/$1"; }

booze_open()
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

booze_read()
{
	booze_out="$(tail -c+$(($3 == 0 ? 0 : $3 + 1)) "$rootdir/$1" | head -c$2)"
}

booze_write()
{
	echo -n "$2" | dd of="$rootdir/$1" bs=1 seek=$3 || { booze_err=-$EIO; return 1; }
	booze_out="${#2}"
	return 0
}

booze_statfs() { booze_out="$(stat -L -f "$rootdir" -c "%S %b %f %a %c %d %l")"; }

booze "$mntpt"
