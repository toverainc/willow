# This script is GROSS

from requests import get
import re
import sys
import os
import string

max_commands = 400

entity_types = ['light.', 'switch.']
entity_types_str = str(entity_types)

tag = "MULTINET: Generate speech commands:"

print(f'{tag} Attempting to fetch your {entity_types_str} entities from Home Assistant...')

willow_path = os.getenv('WILLOW_PATH')

willow_config = f'{willow_path}/sdkconfig'

file = open(willow_config, 'r')
lines = file.readlines()
for line in lines:
    if 'CONFIG_HOMEASSISTANT_URI=' in line:
        ha_uri = line.replace('CONFIG_HOMEASSISTANT_URI=', '')
        ha_uri = ha_uri.strip('\n')
        ha_uri = ha_uri.replace('\"', '')
        ha_uri = ha_uri.replace('/api/conversation/process', '')

    if 'CONFIG_HOMEASSISTANT_TOKEN=' in line:
        ha_token = line.replace('CONFIG_HOMEASSISTANT_TOKEN=', '')
        ha_token = ha_token.strip('\n')
        ha_token = ha_token.replace('\"', '')

file.close()

# Basic sanity checking
substring = 'http'
if substring not in ha_uri:
    print('ERROR: Could not get Home Assistant URI from Willow Configuration')
    sys.exit(1)

if ha_token is None:
    print('ERROR: Could not get Home Assistant token from Willow Configuration')
    sys.exit(1)

# Construct auth header value
auth = f'Bearer {ha_token}'

url = f"{ha_uri}/api/states"
headers = {
    "Authorization": auth,
    "content-type": "application/json",
}

response = get(url, headers=headers)
entities = response.json()

# Define re to remove anything but alphabet and spaces - multinet doesn't support them and too lazy to make them words
pattern = r'[^A-Za-z ]'

devices = []

for type in entity_types:
    for entity in entities:
        entity_id = entity['entity_id']

        if entity_id.startswith(type):
            attr = entity.get('attributes')
            friendly_name = attr.get('friendly_name')
            # Just in case so we don't blow up multinet
            friendly_name = friendly_name.replace('_',' ')
            friendly_name = re.sub(pattern, '', friendly_name)
            friendly_name = ' '.join(friendly_name.split())
            friendly_name = friendly_name.upper()
            # Add device
            if friendly_name not in devices:
                devices.append(friendly_name)


# Make the devices unique
devices = [*set(devices)]

# Start index
index = 0

commands = []
for device in devices:
    index = index + 1
    on = (f'{index} TURN ON {device}')
    index = index + 1
    off = (f'{index} TURN OFF {device}')
    commands.append(on)
    commands.append(off)

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

for device in devices:
    device = string.capwords(device)
    on = f'Turn on {device}'
    off = f'Turn off {device}'
    multinet_header.write(f'\t\"{on}.\",\n')
    multinet_header.write(f'\t\"{off}.\",\n')

multinet_header.write('};\n\n')

fn = """
char *lookup_cmd_multinet(int id) {
    if (cmd_multinet[id] == NULL) {
        return "INVALID";
    }

    return cmd_multinet[id];
}
"""

multinet_header.write(fn);

multinet_header.close()

print(f'{tag} Success!')
