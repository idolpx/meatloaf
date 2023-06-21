# Part of ESPEasy build toolchain.
#
# Combines separate bin files with their respective offsets into a single file
# This single file must then be flashed to an ESP32 node with 0 offset.
#
# Original implementation: Bartłomiej Zimoń (@uzi18)
# Maintainer: Gijs Noorlander (@TD-er)
#
# Special thanks to @Jason2866 (Tasmota) for helping debug flashing to >4MB flash
# Thanks @jesserockz (esphome) for adapting to use esptool.py with merge_bin
#
# Typical layout of the generated file:
#    Offset | File
# -  0x1000 | ~\.platformio\packages\framework-arduinoespressif32\tools\sdk\esp32\bin\bootloader_dout_40m.bin
# -  0x8000 | ~\ESPEasy\.pio\build\<env name>\partitions.bin
# -  0xe000 | ~\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin
# - 0x10000 | ~\ESPEasy\.pio\build\<env name>/<built binary>.bin

Import("env")

platform = env.PioPlatform()

import sys, os, configparser, shutil
from os.path import join
from datetime import datetime

sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))
import esptool


# Create the 'firmware' output folder if it doesn't exist
if not os.path.exists('firmware'):
    os.makedirs('firmware')

config = configparser.ConfigParser()
config.read('platformio.ini')
firmware = "meatloaf"
firmware += "." + config['meatloaf']['build_board'].split()[0]
#firmware += "." + datetime.now().strftime("%Y%m%d%H%M%S")
firmware += ".bin"

def esp32_create_combined_bin(source, target, env):
    print("Generating combined binary for serial flashing")

    # The offset from begin of the file where the app0 partition starts
    # This is defined in the partition .csv file
    app_offset = 0x10000

    new_file_name = f"firmware/{firmware}"
    sections = env.subst(env.get("FLASH_EXTRA_IMAGES"))
    firmware_name = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    chip = env.get("BOARD_MCU")
    flash_size = env.BoardConfig().get("upload.flash_size")
    flash_freq = env.BoardConfig().get("build.f_flash", '40m')
    flash_freq = flash_freq.replace('000000L', 'm')
    flash_mode = env.BoardConfig().get("build.flash_mode", "dio")
    memory_type = env.BoardConfig().get("build.arduino.memory_type", "qio_qspi")
    if flash_mode == "qio" or flash_mode == "qout":
        flash_mode = "dio"
    if memory_type == "opi_opi" or memory_type == "opi_qspi":
        flash_mode = "dout"
    cmd = [
        "--chip",
        chip,
        "merge_bin",
        "-o",
        new_file_name,
        "--flash_mode",
        flash_mode,
        "--flash_freq",
        flash_freq,
        "--flash_size",
        flash_size,
    ]

    print("    Offset | File")
    for section in sections:
        sect_adr, sect_file = section.split(" ", 1)
        print(f" -  {sect_adr} | {sect_file}")
        cmd += [sect_adr, sect_file]

    print(f" - {hex(app_offset)} | {firmware_name}")
    cmd += [hex(app_offset), firmware_name]

    print('Using esptool.py arguments: %s' % ' '.join(cmd))

    esptool.main(cmd)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", esp32_create_combined_bin)
