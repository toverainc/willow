import requests
import json
import os
import argparse

parser = argparse.ArgumentParser()

parser.add_argument("-c", help="Command to send to HA", required=True)
parser.add_argument("-u", help="Base home assistant URL", required=True)

args = parser.parse_args()

command = args.c
base_url = args.u

# Get HA token from environment
ha_token = os.getenv('HA_TOKEN')

url = f"{base_url}/api/conversation/process"

print(f'Token is: {ha_token}')
print(f'Command is: {command}')
print(f'URL is: {url}')

auth_bearer = "Bearer " + ha_token

headers = {
    "Authorization": auth_bearer,
}

body = {
  "text": command,
  "language": "en"
}

try:
    response = requests.request("POST", url, headers=headers, json=body)
except:
    print(f'POST request to {url} failed')

print(f'Response is: \n\n')
pretty_response = json.dumps(response.json(), indent=2)
print(pretty_response)