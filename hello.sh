
enable -f ./booze.so booze

booze_getattr()
{
	if [ "$1" == "/" ]; then
		booze_out="0 0040755 2 `id -u` `id -g` 0 0 0 0 0 0"
		return 0
	elif [ "$1" == "/booze" ]; then
		booze_out="1 0100644 1 `id -u` `id -g` 0 7 1 0 0 0"
		return 0
	else
		booze_err=-2
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
		booze_err=-2
		return 1
	fi
}

data=$'Booze!\n'
booze_read()
{
	if [ "$1" != "/booze" ]; then
		booze_err=-2
		return 1
	fi

	local readlen="$2"
	local offset="$3"

	local datalen="${#data}"

	if [ "$offset" -lt "$datalen" ]; then
		if [ "$((offset + readlen))" -gt "$datalen" ]; then
			readlen="$((datalen - offset))"
		fi
		booze_out="${data:offset:readlen}"
	else
		booze_out=""
	fi

	return 0
}

booze "$1"
