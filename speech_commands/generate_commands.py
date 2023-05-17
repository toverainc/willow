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

print(f'{tag} Attempting to fetch your {entity_types_str} entities from Home Assistant...')

willow_path = os.getenv('WILLOW_PATH')

willow_config = f'{willow_path}/sdkconfig'

ha_tls = False

file = open(willow_config, 'r')
lines = file.readlines()
for line in lines:
    if 'CONFIG_HOMEASSISTANT_HOST=' in line:
        ha_host = line.replace('CONFIG_HOMEASSISTANT_HOST=', '')
        ha_host = ha_host.strip('\n')
        ha_host = ha_host.replace('\"', '')

    if 'CONFIG_HOMEASSISTANT_PORT=' in line:
        ha_port = line.replace('CONFIG_HOMEASSISTANT_PORT=', '')
        ha_port = ha_port.strip('\n')

    if 'CONFIG_HOMEASSISTANT_TLS=' in line:
        ha_tls = line.replace('CONFIG_HOMEASSISTANT_TLS=', '')
        ha_tls = ha_tls.strip('\n')

    if 'CONFIG_HOMEASSISTANT_TOKEN=' in line:
        ha_token = line.replace('CONFIG_HOMEASSISTANT_TOKEN=', '')
        ha_token = ha_token.strip('\n')
        ha_token = ha_token.replace('\"', '')

file.close()

if ha_tls == 'y':
    ha_url_scheme = "https"
else:
    ha_url_scheme = "http"

ha_uri = f"{ha_url_scheme}://{ha_host}:{ha_port}"

# Basic sanity checking
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
            if friendly_name is None:
                # in case of blank or misconfigured HA entities
                continue
            numbers = re.search(r'(\d{1,})', friendly_name)
            if numbers:
                for number in numbers.groups():
                    friendly_name = friendly_name.replace(number, f" {num2words(int(number))} ")
            # Just in case so we don't blow up multinet
            friendly_name = friendly_name.replace('_',' ')
            friendly_name = re.sub(pattern, '', friendly_name)
            friendly_name = ' '.join(friendly_name.split())
            friendly_name = friendly_name.upper()
            # ESP_MN_MAX_PHRASE_LEN=63
            # "TURN OFF " = 9
            if len(friendly_name) > 63 - 9:
                print(f"Turn off command for {friendly_name} will be longer than ESP_MN_MAX_PHRASE_LEN, dropping this entity")
                continue
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
