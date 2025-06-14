# Create a compressed ZIP file of meatloaf firmware
# for use with the meatloaf web flaser

import sys, os, configparser, shutil, re, subprocess
from os.path import join
from datetime import datetime
from zipfile import ZipFile

Import("env")

#platform = env.PioPlatform()

print("Build firmware ZIP enabled")

ini_file = 'platformio.ini'
# this is specified with "-c /path/to/your.ini" when running pio
if env["PROJECT_CONFIG"] is not None:
    ini_file = env["PROJECT_CONFIG"]

print(f"Reading from config file {ini_file}")

def makezip(source, target, env):
    # Create the 'firmware' output dir if it doesn't exist
    firmware_dir = 'firmware'
    if not os.path.exists(firmware_dir):
        os.makedirs(firmware_dir)

    # Make sure all the files are built and ready to zip
    zipit = True
    if not os.path.exists(env.subst("$BUILD_DIR/bootloader.bin")):
        print("\033[1;31mBOOTLOADER not available to pack in firmware zip\033[1;37m")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/partitions.bin")):
        print("\033[1;31mPARTITIONS not available to pack in firmware zip\033[1;37m")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/firmware.bin")):
        print("\033[1;31mFIRMWARE not available to pack in firmware zip\033[1;37m")
        zipit = False
    if not os.path.exists(env.subst("$BUILD_DIR/littlefs.bin")):
        print("\033[1;31mLittleFS not available to archive in firmware zip, building...\033[1;37m")
        os.system("pio run -t buildfs")
        zipit = False

    if zipit == True:
        # Get the build_board variable
        config = configparser.ConfigParser()
        config.read(ini_file)
        environment = "env:"+config['meatloaf']['environment'].split()[0]
        print(f"Creating firmware zip for Meatloaf ESP32 Board: {config[environment]['board']}")

        # Get version information
        with open("include/version.h", "r") as file:
            version_content = file.read()
        defines = re.findall(r'#define\s+(\w+)\s+"?([^"\n]+)"?\n', version_content)

        version = {}
        for define in defines:
            name = define[0]
            value = define[1]
            version[name] = value

        # Get and clean the current commit message
        try:
            version_desc = subprocess.getoutput("git log -1 --pretty=%B | tr '\n' ' '")
        except subprocess.CalledProcessError as e:
            # Revert to full version if no commit msg or error
            version_desc = version['FN_VERSION_FULL']

        try:
            version_build = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
        except subprocess.CalledProcessError as e:
            version_build = "NOGIT"

        version['FN_VERSION_DESC'] = version_desc
        version['FN_VERSION_BUILD'] = version_build
        version['BUILD_DATE'] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Filename variables
        environment_name = config['meatloaf']['environment'].split()[0]
        firmware_date = datetime.now().strftime("%Y%m%d.%H")
        releasefile = firmware_dir+"/release.json"
        firmwarezip = firmware_dir+"/meatloaf."+environment_name+"."+firmware_date+".zip"

        # Copy filesystem image to firmware folder
        try:
            shutil.copy(env.subst("$BUILD_DIR/littlefs.bin"), firmware_dir+"/filesystem.bin")
        except: pass

        # Clean the firmware output dir
        try:
            if os.path.isfile(releasefile):
                os.unlink(releasefile)
        except Exception as e:
            print('Failed to delete %s. Reason: %s' % (releasefile, e))
        try:
            if os.path.isfile(firmwarezip):
                os.unlink(firmwarezip)
        except Exception as e:
            print('Failed to delete %s. Reason: %s' % (firmwarezip, e))

        # Create release JSON
        flash_size = config['meatloaf']['flash_size'].split()[0]
        json_contents = """{
    "version": "%s",
    "version_date": "%s",
    "build_date": "%s",
    "description": "%s",
    "git_commit": "%s",
    "files":
""" % (version['FN_VERSION_FULL'], 
       version['FN_VERSION_DATE'], 
       version['BUILD_DATE'], 
       version['FN_VERSION_DESC'].replace('"', '\\"').strip(),
       version['FN_VERSION_BUILD'])
        
        # Read the template file
        release_template = join(firmware_dir, "bin", f"release.{flash_size}.json") 
        with open(release_template, 'r') as file:
            json_contents += file.read()

        json_contents += """
}"""
        # Save Release JSON
        with open('firmware/release.json', 'w') as f:
            f.write(json_contents)

        # Create the ZIP File
        chip_name = "esp32"
        if environment_name.find("esp32-s3") != -1:
            chip_name = "esp32s3"

        try:
            with ZipFile(firmwarezip, 'w') as zip_object:
                zip_object.write(f"{firmware_dir}/release.json", "release.json")
                zip_object.write(f"{firmware_dir}/bin/bootloader.{chip_name}.{flash_size}.bin", "bootloader.bin")
                zip_object.write(env.subst("$BUILD_DIR/partitions.bin"), "partitions.bin")
                #zip_object.write(f"{firmware_dir}/bin/nvs.bin", "nvs.bin")
                zip_object.write(env.subst("$BUILD_DIR/firmware.bin"), "main.bin")
                zip_object.write(f"{firmware_dir}/bin/update.{chip_name}.{flash_size}.bin", "update.bin")
                zip_object.write(f"{firmware_dir}/filesystem.bin", "storage.bin")
        finally: 
            print("*" * 80)
            print("*")
            print("*   FIRMWARE ZIP CREATED AT: " + firmwarezip)
            print("*")
            print("*" * 80)
 
	
    else:
        print("Skipping making firmware ZIP due to error")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", makezip)
env.AddPostAction("buildfs", makezip)