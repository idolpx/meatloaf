#!/usr/bin/env bash

# an interface to running pio builds
# args can be combined, e.g. '-cbufm' and in any order.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

RUN_BUILD=0
ENV_NAME=""
DO_CLEAN=0
SHOW_MONITOR=0
TARGET_NAME=""
UPLOAD_IMAGE=0
UPLOAD_FS=0
DEV_MODE=0
AUTOCLEAN=1
ANSWER_YES=0
INI_FILE="${SCRIPT_DIR}/platformio.ini"

function show_help {
  echo "Usage: $(basename $0) [options]"
  echo ""
  echo "(pio) options:"
  echo "   -c       # run clean before build"
  echo "   -b       # run build"
  echo "   -u       # upload firmware"
  echo "   -f       # upload filesystem (webUI etc)"
  echo "   -m       # run monitor after build"
  echo "   -d       # add dev flag to build"
  echo "   -e ENV   # use specific environment"
  echo "   -t TGT   # run target task (default of none means do build, but -b must be specified"
  echo "   -n       # do not autoclean"
  echo ""
  echo "other options:"  
  echo "   -h       # this help"
  echo ""
  echo "Simple firmware builds:"
  echo "    ./build.sh -cb        # for CLEAN + BUILD"
  echo "    ./build.sh -m         # show Monitor logs"
  echo "    ./build.sh -cbum      # Clean/Build/Upload/Monitor"
  exit 1
}

if [ $# -eq 0 ] ; then
  show_help
fi

while getopts "bcde:fhmnt:u" flag
do
  case "$flag" in
    b) RUN_BUILD=1 ;;
    c) DO_CLEAN=1 ;;
    d) DEV_MODE=1 ;;
    e) ENV_NAME=${OPTARG} ;;
    f) UPLOAD_FS=1 ;;
    m) SHOW_MONITOR=1 ;;
    n) AUTOCLEAN=0 ;;
    t) TARGET_NAME=${OPTARG} ;;
    u) UPLOAD_IMAGE=1 ;;
    h) show_help ;;
    *) show_help ;;
  esac
done
shift $((OPTIND - 1))

##############################################################
# NORMAL BUILD MODES USING pio

ENV_ARG=""
if [ -n "${ENV_NAME}" ] ; then
  ENV_ARG="-e ${ENV_NAME}"
fi

TARGET_ARG=""
if [ -n "${TARGET_NAME}" ] ; then
  TARGET_ARG="-t ${TARGET_NAME}"
fi

DEV_MODE_ARG=""
if [ ${DEV_MODE} -eq 1 ] ; then
  DEV_MODE_ARG="-a dev"
fi

if [ ${DO_CLEAN} -eq 1 ] ; then
  pio run -c $INI_FILE -t clean ${ENV_ARG}
fi

AUTOCLEAN_ARG=""
if [ ${AUTOCLEAN} -eq 0 ] ; then
  AUTOCLEAN_ARG="--disable-auto-clean"
fi

if [ ${RUN_BUILD} -eq 1 ] ; then
  pio run -c $INI_FILE ${DEV_MODE_ARG} $ENV_ARG $TARGET_ARG $AUTOCLEAN_ARG 2>&1
fi

if [ ${UPLOAD_FS} -eq 1 ] ; then
  pio run -c $INI_FILE ${DEV_MODE_ARG} -t uploadfs 2>&1
fi

if [ ${UPLOAD_IMAGE} -eq 1 ] ; then
  pio run -c $INI_FILE ${DEV_MODE_ARG} -t upload 2>&1
fi

if [ ${SHOW_MONITOR} -eq 1 ] ; then
  # The device monitor hard codes to using platformio.ini for its values, let's grab all the data it would use directly from the named ini.
  # This is useful if you use a different ini file to platformio.ini. (Currently they are the same file in meatloaf project)
  MONITOR_PORT=$(grep ^monitor_port $INI_FILE | cut -d= -f2 | cut -d\; -f1 | awk '{print $1}')
  MONITOR_SPEED=$(grep ^monitor_speed $INI_FILE | cut -d= -f2 | cut -d\; -f1 | awk '{print $1}')
  MONITOR_FILTERS=$(grep ^monitor_filters $INI_FILE | cut -d= -f2 | cut -d\; -f1 | tr ',' '\n' | sed 's/^ *//; s/ *$//' | awk '{printf("-f %s ", $1)}')  
  pio device monitor -p $MONITOR_PORT -b $MONITOR_SPEED $MONITOR_FILTERS 2>&1
fi
