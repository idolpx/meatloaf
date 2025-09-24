#!/usr/bin/env python3

import configparser, sys, os, json

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
        print(f"mcu[{mcu}] flash[{flash_size}]")

    
    if environment_value is not None:
        print(f"Environment: [{environment_value}]")
        #return environment_value
    else:
        print("Could not retrieve the environment value")
        return None

    command = f"esptool.py -b 460800 write_flash "
    if (mcu == "esp32"):
        command += f"0x1000 '.pio/build/{environment_value}/bootloader.bin' "
    if (mcu == "esp32s3"):
        command += f"0x0000 '.pio/build/{environment_value}/bootloader.bin' "
    command += f"0x8000 '.pio/build/{environment_value}/partitions.bin' "
    command += f"0x10000 '.pio/build/{environment_value}/firmware.bin' "

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