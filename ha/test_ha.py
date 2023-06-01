import requests
import json
import os
import sys
import datetime
import argparse

parser = argparse.ArgumentParser()

parser.add_argument("-c", help="Command to send to HA", required=True)
parser.add_argument("-u", help="Base home assistant URL", required=True)
parser.add_argument("-v", help="Verbose", action='store_true')

args = parser.parse_args()

command = args.c
base_url = args.u
verbose = args.v

# Get HA token from environment
ha_token = os.getenv('HA_TOKEN')

url = f"{base_url}/api/conversation/process"

# print(f'Token is: {ha_token}')
print(f'URL is: {url}')
print(f'Command is: {command}')

auth_bearer = "Bearer " + ha_token

headers = {
    "Authorization": auth_bearer,
}

body = {
    "text": command,
    "language": "en"
}

time_start = datetime.datetime.now()
try:
    response = requests.request("POST", url, headers=headers, json=body, timeout=0.5)
except:
    sys.exit(f'POST request to {url} failed - exiting')

time_end = datetime.datetime.now()
ha_time = time_end - time_start
ha_time_ms = ha_time.total_seconds() * 1000
ha_time_ms_string = str(ha_time_ms)

if verbose:
    print(f'Response is: \n\n')
    pretty_response = json.dumps(response.json(), indent=2)
    print(pretty_response)

print(f'HA took {ha_time_ms_string} ms to execute command {command}')
