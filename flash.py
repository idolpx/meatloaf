#!/usr/bin/env python3

import configparser, argparse, sys, os, json, re, subprocess, venv

def read_ini_value(file_path, section='meatloaf', key='environment'):
    """
    Read a value from an INI file for a specific section and key.
    
    Args:
        file_path (str): Path to the INI file
        section (str): Section name (default: 'meatloaf')
        key (str): Key name (default: 'environment')
    
    Returns:
        str: The value associated with the key, or None if not found
    """
    try:
        config = configparser.ConfigParser()
        config.read(file_path)
        
        if section in config:
            if key in config[section]:
                return config[section][key]
            else:
                print(f"Key '{key}' not found in section '{section}'")
                return None
        else:
            print(f"Section '{section}' not found in the INI file")
            return None
            
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found")
        return None
    except configparser.Error as e:
        print(f"Error parsing INI file: {e}")
        return None
    except Exception as e:
        print(f"Unexpected error: {e}")
        return None

def parse_littlefs_partition(csv_path):
    """
    Find the 'littlefs' data partition row in an ESP-IDF partitions CSV.
    Returns (offset_str, size_str) e.g. ("0xA70000", "5696K").
    """
    with open(csv_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = [p.strip() for p in line.split(',')]
            if len(parts) >= 5 and parts[1] == 'data' and parts[2] == 'littlefs':
                return parts[3], parts[4]
    raise RuntimeError(f"No littlefs data partition found in {csv_path}")

def size_str_to_bytes(size_str):
    size_str = size_str.strip()
    unit = size_str[-1].upper()
    if unit == 'K':
        return int(size_str[:-1], 0) * 1024
    if unit == 'M':
        return int(size_str[:-1], 0) * 1024 * 1024
    return int(size_str, 0)

def get_name_max(environment_value):
    """
    Read CONFIG_LITTLEFS_OBJ_NAME_LEN from the last build's resolved sdkconfig.
    Falls back to the Kconfig default (64) if the project hasn't been built yet.
    """
    sdkconfig_cmake = f".pio/build/{environment_value}/config/sdkconfig.cmake"
    if os.path.exists(sdkconfig_cmake):
        with open(sdkconfig_cmake) as f:
            m = re.search(r'CONFIG_LITTLEFS_OBJ_NAME_LEN\s+"(\d+)"', f.read())
            if m:
                return int(m.group(1))
    print(f"Warning: {sdkconfig_cmake} not found (build the project first); assuming name-max=64")
    return 64

def get_littlefs_python(venv_dir):
    """
    Create (if needed) a venv with littlefs-python and return the path to its CLI.
    PlatformIO's own mklittlefs.exe hard-caps filenames at 32 bytes; littlefs-python
    honors --name-max, which is what components/esp_littlefs's own (unused-by-PlatformIO)
    CMake target relies on.
    """
    scripts_dir = "Scripts" if os.name == "nt" else "bin"
    exe_suffix = ".exe" if os.name == "nt" else ""
    littlefs_py = os.path.join(venv_dir, scripts_dir, f"littlefs-python{exe_suffix}")

    if not os.path.exists(littlefs_py):
        print(f"Creating venv for littlefs-python at {venv_dir}...")
        venv.create(venv_dir, with_pip=True)
        pip = os.path.join(venv_dir, scripts_dir, f"pip{exe_suffix}")
        subprocess.run([pip, "install", "-r", "components/esp_littlefs/image-building-requirements.txt"], check=True)

    return littlefs_py

def build_littlefs_image(ini_file, environment_value, json_board, data_dir):
    """
    Rebuild .pio/build/{env}/littlefs.bin with littlefs-python instead of PlatformIO's
    bundled mklittlefs, so CONFIG_LITTLEFS_OBJ_NAME_LEN (e.g. 64) is actually honored.
    """
    partitions_csv = read_ini_value(ini_file, f"env:{environment_value}", "board_build.partitions")
    if not partitions_csv:
        partitions_csv = json_board["build"]["partitions"]

    offset, size_str = parse_littlefs_partition(partitions_csv)
    fs_size = size_str_to_bytes(size_str)
    name_max = get_name_max(environment_value)

    littlefs_py = get_littlefs_python(".pio/littlefs_py_venv")

    out_dir = f".pio/build/{environment_value}"
    out_bin = f"{out_dir}/littlefs.bin"
    os.makedirs(out_dir, exist_ok=True)

    print(f"Building {out_bin} from {data_dir} (fs-size={fs_size}, name-max={name_max})")
    subprocess.run([
        littlefs_py, "create", data_dir, out_bin,
        "-v",
        f"--fs-size={fs_size}",
        f"--name-max={name_max}",
        "--block-size=4096",
    ], check=True)

def main():
    # Example usage
    ini_file = "platformio.ini"  # Replace with your INI file path
    
    # Read the environment value from meatloaf section
    environment_value = read_ini_value(ini_file).split(';')[0].strip()

    # Get board info
    board_file = "boards/" + read_ini_value(ini_file, f"env:{environment_value}", "board") + ".json"
    with open(board_file) as f:
        json_board = json.load(f)
        mcu = json_board["build"]["mcu"]
        flash_size = json_board["upload"]["flash_size"]
        upload_speed = json_board["upload"]["speed"]
        print(f"mcu[{mcu}] flash[{flash_size}]")

    
    if environment_value is not None:
        print(f"Environment: [{environment_value}]")
        #return environment_value
    else:
        print("Could not retrieve the environment value")
        return None

    # parse command line
    parser = argparse.ArgumentParser()
    parser.add_argument("-a", "--all", action='store_true', help="Flash all components (bootloader, partitions, firmware, filesystem)")
    parser.add_argument("-f", "--filesystem", action='store_true', help="Flash filesystem")
    parser.add_argument("-r", "--rebuild-fs", action='store_true',
                         help="Rebuild littlefs.bin with littlefs-python before flashing, "
                              "so CONFIG_LITTLEFS_OBJ_NAME_LEN is honored (PlatformIO's bundled "
                              "mklittlefs hard-caps filenames at 32 bytes regardless of Kconfig)")
    args = parser.parse_args()

    if args.rebuild_fs and (args.all or args.filesystem):
        build_platform = read_ini_value(ini_file, "meatloaf", "build_platform").split(';')[0].strip()
        flash_size_cfg = read_ini_value(ini_file, "meatloaf", "flash_size").split(';')[0].strip()
        data_dir = f"data/{build_platform}.{flash_size_cfg}"
        build_littlefs_image(ini_file, environment_value, json_board, data_dir)

    command = f"esptool -b {upload_speed} write-flash "

    if args.all:
        if (mcu == "esp32"):
            command += f"0x1000 '.pio/build/{environment_value}/bootloader.bin' "
        if (mcu == "esp32s3"):
            command += f"0x0000 '.pio/build/{environment_value}/bootloader.bin' "
        command += f"0x8000 '.pio/build/{environment_value}/partitions.bin' "

    if not args.filesystem:
        command += f"0x10000 '.pio/build/{environment_value}/firmware.bin' "

    if args.all or args.filesystem:
        if (flash_size == "4MB"):
            command += "0x340000 "
        if (flash_size == "8MB"):
            command += "0x570000 "
        if (flash_size == "16MB"):
            command += "0xA70000 "
        command += f"'.pio/build/{environment_value}/littlefs.bin'"

    #print(command)
    os.system(command)

if __name__ == "__main__":
    main()