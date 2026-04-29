savedcmd_tdxmod.mod := printf '%s\n'   tdxmod.o | awk '!x[$$0]++ { print("./"$$0) }' > tdxmod.mod
