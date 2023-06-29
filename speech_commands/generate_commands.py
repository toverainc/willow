# This script is GROSS

from num2words import num2words
from requests import get
import re
import sys
import os
import string

max_commands = 400

entity_types = ['light.', 'switch.']
entity_types_str = str(entity_types)

tag = "MULTINET: Generate speech commands:"

willow_path = os.getenv('WILLOW_PATH')

willow_config = f'{willow_path}/sdkconfig'

file = open(willow_config, 'r')
lines = file.readlines()
for line in lines:
    if 'CONFIG_WILLOW_WAS_URL=' in line:
        was_url = line.replace('CONFIG_WILLOW_WAS_URL=', '')
        was_url = was_url.strip('\n')
        was_url = was_url.replace('\"', '')

file.close()

# sdkconfig has WebSocket URL
was_url = re.sub("^ws", "http", was_url)
was_url = re.sub("/ws$", "/api/multinet", was_url)

response = get(was_url)
commands_json = response.json()

# Start index
index = 0

commands = []
for command in commands_json:
    index = index + 1
    commands.append(f"{index} {command}")

if index >= max_commands:
    print(f'WARNING: Multinet supports a maximum of {max_commands} commands and you have {index}')
    print(f'WARNING: YOU WILL NEED TO TRIM YOUR COMMANDS MANUALLY')
    sys.exit(1)

multinet_command_file = open(f'{willow_path}/speech_commands/commands_en.txt', 'w')

for command in commands:
    multinet_command_file.write(f'{command}\n')

multinet_command_file.close()

multinet_header = open(f'{willow_path}/main/generated_cmd_multinet.h', 'w')

multinet_header.write(f'#include <string.h> \n\n')

multinet_header.write('char *cmd_multinet[] = {\n')

# Different indexes
multinet_header.write(f'\t\"DUMMY\",\n')

for command in commands_json:
    multinet_header.write(f'\t\"{command}.\",\n')

multinet_header.write('};\n\n')
multinet_header.write(f'int cmd_multinet_max = {index};\n')

fn = """
char *lookup_cmd_multinet(int id) {
    if (id > cmd_multinet_max) {
        return "INVALID";
    }

    return cmd_multinet[id];
}

size_t get_cmd_multinet_size(void)
{
    int i = 0;
    size_t sz = 0;

    while(true) {
        if (strcmp(lookup_cmd_multinet(i), "INVALID") == 0) {
            break;
        }

            // strlen excludes terminating null byte
            sz += strlen(lookup_cmd_multinet(i)) + 1;
            i++;
    }

    return sz;
}
"""

multinet_header.write(fn);

multinet_header.close()

print(f'{tag} Success!')
