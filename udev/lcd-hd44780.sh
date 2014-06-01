#!/bin/bash

MAJOR=$(/bin/cat /proc/devices | /bin/grep lcd | /usr/bin/cut -f1 -d" ")
MINOR=0
PATH="/dev"
NAME="lcd"
TYPE="c"
LOG="/tmp/log"

/usr/bin/touch ${LOG}
exec > ${LOG}
exec 2>&1


/bin/date

echo "UDEV script invoked with ${0} ${1}"
echo "MAJOR ${MAJOR} MINOR ${MINOR}"

if [ "${1}" != "remove" ]
then
        echo "Adding c device"
        if [ "${MAJOR}" != "" ]
        then
                /bin/mknod ${PATH}/${NAME} ${TYPE} ${MAJOR} ${MINOR}
        fi
else
        echo "Removing c device"
        /bin/rm ${PATH}/${NAME}
fi
