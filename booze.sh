
# A library of sorts providing various facilities for booze
# filesystems.

enable -f ./booze.so booze

__getmacros() { gcc -E -dD -x c - <<<"#include <$1.h>"; }

# Define all system errno values
eval `__getmacros errno | sed -n -r 's/^#define (E[^ ]+) ([0-9]+)$/\1=\2/p;'`

# Define access(2) mask values
eval `__getmacros unistd | sed -n -r 's/^#define ([^ ]+_OK) ([0-9]+)$/\1=\2/p;'`

# Define O_* open(2) flags
eval `__getmacros fcntl | sed -n -r 's/^#define (O_[^ ]+) ([0-9]+)$/\1=\2/p;'`

# Define S_* mode flags
eval `__getmacros linux/stat | sed -n -r 's/^#define (S_[^ ]+) ([0-9]+)$/\1=\2/p;'`

for __t in LNK REG DIR CHR BLK SOCK; do
	eval "S_IS$__t() { [ \"\$((\$1 & \$S_IF$__t))\" -eq \"\$((S_IF$__t))\" ]; }"
done

# This one doesn't fit the S_IS*/S_IF* pattern
S_ISFIFO() { [ "$(($1 & $S_IFIFO))" -eq "$((S_IFIFO))" ]; }

BOOZE_CALL_NAMES=(getattr access readlink readdir mknod mkdir unlink rmdir symlink
	          rename link chmod chown truncate utimens open read write statfs
	          release fsync fallocate setxattr getxattr listxattr removexattr)

# Namespace cleanup
unset -v __t
unset -f __getmacros
