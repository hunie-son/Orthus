savedcmd_tdxmap.mod := printf '%s\n'   tdxmap.o | awk '!x[$$0]++ { print("./"$$0) }' > tdxmap.mod
