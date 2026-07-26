# Build the data folder for the filesystem image, 
# and set PROJECT_DATA_DIR to the correct path for the build

import sys, os, configparser, json, shutil, re, subprocess, pprint
from os.path import join
from datetime import datetime
from zipfile import ZipFile

Import("env")

# print(pprint.pformat(env.Dictionary()))
# exit(0)

#platform = env.PioPlatform()
config = env.GetProjectConfig()
board = env['BOARD']

print("Build data for filesystem image")

environment_name = env['PIOENV']
build_platform = config.get("meatloaf", "build_platform")

# Get flash size from {board}.json
flasher = json.load(open(env.subst("boards/" + board + ".json")))
flash_size = flasher["upload"]["flash_size"].replace("MB", "m")

print(f"Building data for environment: {environment_name} [{build_platform}][{board}][{flash_size}]")

# Set data_dir for building the filesystem image
# print(f"PROJECT_DATA_DIR before: {env['PROJECT_DATA_DIR']}")
env["PROJECT_DATA_DIR"] = join(env["PROJECT_DIR"],"data", build_platform+"."+flash_size)
# print(f"PROJECT_DATA_DIR after: {env['PROJECT_DATA_DIR']}")
