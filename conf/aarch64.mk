# You will die horribly without -mstrict-align, due to
# unaligned access to a stack attr variable with stp.
# Relaxing alignment checks via sctlr_el1 A bit setting should solve
# but it doesn't - setting ignored?
#
# TLS notes:
# for the kernel image itself, TLS seems to work, including with
# mixed tls models [local-exec, initial-exec, global-dynamic, local-dynamic]
#
# For dynamic libraries linked in at runtime, _nothing_ works.
#
# For testing purposes, you might want to force -mtls-model=[model]
# here to have all objects use the specified model.

arch-cflags = -mstrict-align -mtls-dialect=desc -DAARCH64_PORT_STUB
