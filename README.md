booze
=====

FUSE bindings for bash.

If you, like me, have ever had the desire to create a FUSE filesystem
from a shell script, this may be just the thing for you.

booze compiles to a shared library (`booze.so`) that you can load into
bash via its `enable -f` feature.  It adds a new builtin to bash,
`booze`, that mounts a FUSE filesystem and shuffles data back and
forth between libfuse and bash functions in your script.

Its `help` text gives a basic description of how to use it:

    booze: booze [-df] FN_ASSOC MOUNTPOINT
        Mount a booze filesystem at MOUNTPOINT using functions in FN_ASSOC.

        Options:
          -d: debug mode (implies -f)
          -f: run in foreground

        FN_ASSOC must be an associative array.  Any keys it contains that match
        one of the following FUSE operation names will cause that FUSE operation
        to be implemented by the bash function named by the value corresponding
        to the key:

            getattr
            access
            readlink
            readdir
            mknod
            mkdir
            unlink
            rmdir
            symlink
            rename
            link
            chmod
            chown
            truncate
            utimens
            open
            read
            write
            statfs
            release
            fsync
            fallocate
            setxattr
            getxattr
            listxattr
            removexattr

        If for any reason this doesn't seem like a good idea, the user is
        encouraged to drink until it does.

The files `hello.sh` (a simple "hello world"), `passthrough.sh` (sort
of like a bind mount), and `cowsayfs.sh` (filenames through cowsay!)
provide examples of functioning booze filesystems.
