#!/bin/bash

. ./booze.sh

booze_getattr()
{
	local now=`date +%s`
	local times="$now $now $now"
	local ids="`id -u` `id -g`"
	if [ "$1" == "/" ]; then
		booze_out="0 $(printf '%o' $((S_IFDIR | 0755))) 2 $ids 0 0 0 $times"
		return 0
	elif [ "$1" == "/booze" ]; then
		booze_out="1 $(printf '%o' $((S_IFREG | 0644))) 1 $ids 0 7 1 $times"
		return 0
	else
		booze_err=-$ENOENT
		return 1
	fi
}

booze_readdir()
{
	booze_out="./../booze"
	return 0
}

booze_open()
{
	if [ "$1" == "/booze" ]; then
		return 0
	else
		booze_err=-$ENOENT
		return 1
	fi
}

file=$'Booze!\n'
booze_read()
{
	if [ "$1" != "/booze" ]; then
		booze_err=-$ENOENT
		return 1
	fi

	local readlen="$2"
	local offset="$3"

	local filelen="${#file}"

	if [ "$offset" -lt "$filelen" ]; then
		if [ "$((offset + readlen))" -gt "$filelen" ]; then
			readlen="$((filelen - offset))"
		fi
		data="${file:offset:readlen}"
	else
		data=""
	fi

	printf "%s" "$data"
	return 0
}

declare -A booze_ops

for name in ${BOOZE_CALL_NAMES[@]}; do
	if [ "`type -t booze_$name`" == "function" ]; then
		booze_ops[$name]=booze_$name
	fi
done

booze booze_ops "$1"
