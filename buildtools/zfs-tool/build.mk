ZFS_TOOL_SRC = zfs-tool.c

ZFS_FUSE_SRC = cmd_listener.c   \
               ptrace.c         \
               util.c           \
               zfs_acl.c        \
               zfs_dir.c        \
               zfs_ioctl.c      \
               zfs_log.c        \
               zfs_replay.c     \
               zfs_rlock.c      \
               zfs_vfsops.c     \
               zfs_vnops.c      \
               zvol.c           \
               fuse_listener.c  \
               zfsfuse_socket.c \
               zfs_operations.c

zfs-tool-base = $(src)/buildtools/zfs-tool
zfs-tool-src = $(patsubst %.c,$(zfs-tool-base)/%.c,$(ZFS_TOOL_SRC))

zfs-fuse-base = $(zfs-tool-base)/zfs-fuse/src/zfs-fuse
zfs-fuse-src = $(patsubst %.c,$(zfs-fuse-base)/%.c,$(ZFS_FUSE_SRC))

ZFS_FUSE_LIBS = libumem libavl libnvpair libzpool libzfscommon libsolkerncompat
libumem-base = $(zfs-tool-base)/zfs-fuse/src/lib/libumem
libavl-base = $(zfs-tool-base)/zfs-fuse/src/lib/libavl
libnvpair-base = $(zfs-tool-base)/zfs-fuse/src/lib/libnvpair
libzfscommon-base = $(zfs-tool-base)/zfs-fuse/src/lib/libzfscommon
libsolkerncompat-base = $(zfs-tool-base)/zfs-fuse/src/lib/libsolkerncompat

libzpool-base = $(zfs-tool-base)/zfs-fuse/src/lib/libzpool

zfs-fuse-libs = $(patsubst %,buildtools/zfs-tool/%.a,$(ZFS_FUSE_LIBS))
zfs-fuse-libs-src := $(shell find $(zfs-tool-base)/zfs-fuse/src/lib -name "*.[ch]")

zfs-fuse-CFLAGS = -std=c99 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT -DTEXT_DOMAIN=\\"zfs-fuse\\" -DLINUX_AIO \
                  -Wno-switch -Wno-unused -Wno-missing-braces -Wno-parentheses -Wno-uninitialized -fno-strict-aliasing \
                  -Wp,-w

zfs-tool-CFLAGS = -I$(zfs-fuse-base) \
		  -I$(libumem-base)/include          \
		  -I$(libavl-base)/include           \
		  -I$(libnvpair-base)/include        \
		  -I$(libzfscommon-base)/include     \
		  -I$(libsolkerncompat-base)/include \
		  $(zfs-fuse-CFLAGS) -D_KERNEL -D__OSV__

buildtools/zfs-tool/libumem.a: HOST_CFLAGS=$(zfs-fuse-CFLAGS) -I$(libumem-base) -I$(libumem-base)/include

buildtools/zfs-tool/libavl.a: HOST_CFLAGS=$(zfs-fuse-CFLAGS) -I$(libavl-base)/include \
				-I$(libumem-base)/include -I$(libsolkerncompat-base)/include

buildtools/zfs-tool/libnvpair.a: HOST_CFLAGS=$(zfs-fuse-CFLAGS) -I$(libnvpair-base)/include \
				-I$(libumem-base)/include -I$(libsolkerncompat-base)/include -D_KERNEL

buildtools/zfs-tool/libzfscommon.a: HOST_CFLAGS=$(zfs-fuse-CFLAGS) -I$(libzfscommon-base)/include \
				-I$(libumem-base)/include -I$(libsolkerncompat-base)/include \
				-I$(libavl-base)/include -I$(libnvpair-base)/include -D_KERNEL

buildtools/zfs-tool/libsolkerncompat.a: HOST_CFLAGS=$(zfs-fuse-CFLAGS) -I$(libsolkerncompat-base)/include \
					-I$(libumem-base)/include -I$(libavl-base)/include -D_KERNEL

buildtools/zfs-tool/libzpool.a: HOST_CFLAGS=$(zfs-fuse-CFLAGS) \
				-I$(libzfscommon-base)/include -I$(libavl-base)/include \
				-I$(libnvpair-base)/include -I$(libumem-base)/include \
				-I$(libsolkerncompat-base)/include -D_KERNEL

buildtools/zfs-tool/libzfs-fuse.a: HOST_CFLAGS=$(zfs-tool-CFLAGS)

$(zfs-fuse-libs): $(zfs-fuse-libs-src)
	$(makedir)
	NAME=`basename -s .a $@` ; \
	THIS_BASE=$(zfs-tool-base)/zfs-fuse/src/lib/$${NAME} ; \
	THIS_SRC=`find $(zfs-tool-base)/zfs-fuse/src/lib/$${NAME} -name "*.c"` ; \
	THAT_OBJ="" ; \
	for THIS in $${THIS_SRC} ; do \
		THAT=`echo $${THIS} | sed 's,^$(src)/\(.*\).c,\1.o,'` ; \
		mkdir -p `dirname $${THAT}` ; \
		$(HOST_CC) $(HOST_CFLAGS) -c -o $${THAT} $${THIS} ; \
		THAT_OBJ="$${THAT_OBJ} $${THAT}" ; \
		echo "THAT=$${THAT}" ; \
	done ; \
	echo "THAT_OBJ=$${THAT_OBJ}" ; \
	$(HOST_AR) rcs $@ $${THAT_OBJ} ;

buildtools/zfs-tool/libzfs-fuse.a: $(zfs-fuse-src)
	$(makedir)
	for THIS in $(zfs-fuse-src) ; do \
		THAT=`echo $${THIS} | sed 's,^$(src)/\(.*\).c,\1.o,'` ; \
		mkdir -p `dirname $${THAT}` ; \
		$(HOST_CC) $(HOST_CFLAGS) -c -o $${THAT} $${THIS} ; \
		THAT_OBJ="$${THAT_OBJ} $${THAT}" ; \
		echo "THAT=$${THAT}" ; \
	done ; \
        echo "THAT_OBJ=$${THAT_OBJ}" ; \
        $(HOST_AR) rcs $@ $${THAT_OBJ} ;

buildtools/zfs-tool/zfs-tool.o: $(zfs-tool-src)
	$(makedir)
	$(HOST_CC) $(zfs-tool-CFLAGS) -c -o $@ $(zfs-tool-src)

buildtools/zfs-tool/atomic.o: $(src)/bsd/sys/cddl/compat/opensolaris/kern/opensolaris_atomic.cc
	$(makedir)
	$(HOST_CXX) -std=c++11 -I$(src) -I$(src)/bsd/$(host_arch) -c -o $@ $<

zfs-tool: buildtools/zfs-tool/zfs-tool.o buildtools/zfs-tool/atomic.o buildtools/zfs-tool/libzfs-fuse.a $(zfs-fuse-libs)
	$(makedir)
	$(HOST_CC) -o buildtools/zfs-tool/zfs-tool $^ -lpthread -lfuse -ldl -lz -laio
