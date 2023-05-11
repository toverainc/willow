#!/usr/bin/awk

BEGIN {
	i = 1
	printf "#include <string.h>\n\n"
	printf "char *cmd_multinet[] = {\n"
	printf "    \"DUMMY\",\n"
}

{
	# replace first space with colon
	sub(/ /, ":", $0)

	# split line on colon
	split($0, cmds, ":")

	if (cmds[1] != i) {
		printf "command IDs need to be in consecutive order and start from 1!"
		exit 1
	}


	printf("    \"%s\",\n", cmds[2])

	i++
}

END {
	printf "};\n\n"
	printf "char *lookup_cmd_multinet(int id) {\n"
	printf "    return cmd_multinet[id];\n"
	printf "}\n"
}
