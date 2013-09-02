#!/bin/bash

. ./booze.sh

basestat()
{
	local now=`date +%s`
	local times="$now $now $now"
	local ids="`id -u` `id -g`"
	echo "0 $1 2 $ids 0 102400000 0 $times"
}

dirstat() { basestat "$(printf '%o' $((S_IFDIR | ${1:-0755})))"; }
regstat() { basestat "$(printf '%o' $((S_IFREG | ${1:-0644})))"; }

cows=($(cd /usr/share/cowsay/cows/ && for c in *.cow; do echo "${c%.cow}"; done))

cowpat="$(echo ${cows[@]} | tr ' ' '|')"

cs_getattr()
{
	if [ "$1" == "/" ] || [[ "$1" =~ ^/($cowpat)$ ]]; then
		booze_out=`dirstat`
		return 0
	elif [[ "$1" =~ ^/($cowpat)/[^/]+$ ]]; then
		booze_out=`regstat`
		return 0
	else
		booze_err=-$ENOENT
		return 1
	fi
}

cs_readdir()
{
	if [ "$1" == "/" ]; then
		booze_out="./../${cowpat//|//}"
		return 0
	elif [[ "$1" =~ ^/($cowpat)$ ]]; then
		booze_out="./.."
		return 0
	else
		booze_err=-$ENOENT
		return 1
	fi
}

cs_open()
{
	if [[ "$1" =~ ^/($cowpat)/[^/]+$ ]]; then
		return 0
	else
		booze_err=-$EINVAL
		return 1
	fi
}

cs_read()
{
	if ! [[ "$1" =~ ^/($cowpat)/[^/]+$ ]]; then
		booze_err=-$EINVAL
		return 1
	elif [ "$3" != 0 ]; then
		return 0
	fi

	local msg="${1#/*/}"
	local cow="${1#/}"
	cow="${cow%%/*}"
	cowsay -f "$cow" "$msg"
}

declare -A cs_ops

for name in ${BOOZE_CALL_NAMES[@]}; do
	if [ "`type -t cs_$name`" == "function" ]; then
		cs_ops[$name]=cs_$name
	fi
done

booze cs_ops "$1"
