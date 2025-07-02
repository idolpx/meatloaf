import datetime, re, subprocess, sys, configparser

Import("env")

ini_file = 'platformio.ini'
# this is specified with "-c /path/to/your.ini" when running pio
if env["PROJECT_CONFIG"] is not None:
    ini_file = env["PROJECT_CONFIG"]

print(f"Reading from config file {ini_file}")

config = configparser.ConfigParser()
config.read(ini_file)
environment = config['meatloaf']['environment'].split()[0]

# Need to run this command in the PIO virtual environment
#env.Execute("$PYTHONEXE -m pip list --format=json --disable-pip-version-check");
#exit(0);

# Disable automatic versioning
#if 1:
#    print("Automatic versioning disabled")
#    exit

# Don't do anything if nothing has changed
if len(subprocess.check_output(["git", "diff", "--name-only"], universal_newlines=True)) == 0:
    print("Nothing has changed")

else:
    try:
        ver_build = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
    except subprocess.CalledProcessError as e:
        ver_build = "NOGIT"
    
    header_file = "include/version.h"
    version_file = "version.txt"

    ver_date = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    fw_date = datetime.datetime.now().strftime("%Y%m%d.%H")

    rxs = ['^#define FN_VERSION_MAJOR (\w+)', '^#define FN_VERSION_MINOR (\w+)',
           '^(#define FN_VERSION_BUILD)', '^(#define FN_VERSION_DATE)', '^(#define FN_VERSION_FULL)', 
           '^(#define FW_VERSION)']

    ver_maj = ""
    ver_min = ""

    txt = [line for line in open(header_file)]

    fout = open(header_file, "w")

    for line in txt:

        for i in range(len(rxs)):
            m = re.match(rxs[i], line)
            if m is not None:
                break

        if m is not None:
            if i == 0:
                ver_maj = m.groups(0)[0]
                fout.write(line)
            elif i == 1:
                ver_min = m.groups(0)[0]
                fout.write(line)
            elif i == 2:
                line = m.groups(0)[0] + " \"" + ver_build + "\"\n"
                fout.write(line)
            elif i == 3:
                line = m.groups(0)[0] + " \"" + ver_date + "\"\n"
                fout.write(line)
            elif i == 4:
                line = m.groups(0)[0] + " \"" + ver_maj + "." + \
                    ver_min + "." + ver_build + "\"\n"
                fout.write(line)
            elif i == 5:
                line = m.groups(0)[0] + " \"" + fw_date + "\"\n"
                fout.write(line)
        else:
            fout.write(line)

    fout.close()

    # Write version.txt
    version_txt = ver_maj + "." + ver_min + "." + ver_build + "." + environment
    print(version_txt)
    fout = open(version_file, "w")
    fout.write(version_txt)
    fout.close()