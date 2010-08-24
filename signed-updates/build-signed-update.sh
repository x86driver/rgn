#! /bin/bash

usage()
{
	cat<<-EOF
Usage: $(basename ${0}) [OPTIONS]

        -i <input_file>
                File containing the update to be signed.  (Required)
        -o <output_file>
                File to which to write the signed update data.  (Required)
        -u <gpg_id>
                Name of GPG key to use for signing.  (Required)
        --pw_file <file>
                File containing GPG password.  (Required)
        -c <chunk_size>
                Size of data (in bytes) to put in each chunk for signing.
                (Default 524288 = 512K)
        -f <offset>
                Offset into flash target to begin writing.
                (Default 0)
        -t <target>
                Product-specific field to determine where in flash to write.
                (Default 0)
        -v <version>
                Specify packet format version number.
                (Default 1)

EOF
	exit
}

# Command to invoke gpg
GPG_COMMAND="gpg"

# Constants
SIGSIZE=512
PGP_SIGNED_VIRT_RGN=512

# Blank required options
INPUT_FILE=""
OUTPUT_FILE=""
GPG_USERNAME=""
GPG_PASSPHRASE_FILE=""

# Defaults
HEADER_LEN=16
TARGET=0
OFFSET_INTO_TARGET=0
CHUNK_SIZE=524288

while [ $# -gt 0 ]; do
	case "${1}" in
		-h)
		    usage
		    ;;
		--help)
		    usage
		    ;;
		-u)
		    [ $# -ge 2 ] || usage
		    GPG_USERNAME="${2}"
		    shift; shift
		    ;;
		--pw_file)
		    [ $# -ge 2 ] || usage
		    GPG_PASSPHRASE_FILE="${2}"
		    shift; shift
		    ;;
		--homedir)
		    [ $# -ge 2 ] || usage
		    GPG_COMMAND="${GPG_COMMAND} --homedir ${2}"
		    shift; shift
		    ;;
		-c)
		    [ $# -ge 2 ] || usage
		    CHUNK_SIZE="${2}"
		    shift; shift
		    ;;
		-f)
		    [ $# -ge 2 ] || usage
		    OFFSET_INTO_TARGET="${2}"
		    shift; shift
		    ;;
		-i)
		    [ $# -ge 2 ] || usage
		    INPUT_FILE="${2}"
		    shift; shift
		    ;;
		-o)
		    [ $# -ge 2 ] || usage
		    OUTPUT_FILE="${2}"
		    shift; shift
		    ;;
		-t)
		    [ $# -ge 2 ] || usage
		    TARGET="${2}"
		    shift; shift
		    ;;
                -v)
                    [ $# -ge 2 ] || usage
                    if [ ${2} -eq 1 ]; then
                        HEADER_LEN=16
                    elif [ ${2} -eq 2 ]; then
                        HEADER_LEN=24
                    else
                        echo "No such version: ${2}"
                        exit 1
                    fi
                    shift; shift
                    ;;
		*)
		    usage
		    ;;
	esac
done

if [ -z "${GPG_USERNAME}" ]; then
	echo "Please specify a GPG key by user id.  (-h for help)"
	exit 1
fi

if [ -z "${GPG_PASSPHRASE_FILE}" ]; then
	echo "Please specify a file containing the gpg passphrase.  (-h for help)"
	exit 1
fi

if [ -z "${INPUT_FILE}" ]; then
	echo "Please specify an input file.  (-h for help)"
	exit 1
fi

if [ -z "${OUTPUT_FILE}" ]; then
	echo "Please specify an output file.  (-h for help)"
	exit 1
fi

if ( ! ${GPG_COMMAND} --list-secret-keys "${GPG_USERNAME}" > /dev/null 2> /dev/null ); then
	echo "GPG secret key for \"${GPG_USERNAME}\" does not exist."
	exit 1
fi

if [ ! -e "${GPG_PASSPHRASE_FILE}" ]; then
	echo "GPG passphrase file \"${GPG_PASSPHRASE_FILE}\" does not exist."
	exit 1
fi

_filesize=`stat -c "%s" "${INPUT_FILE}" 2> /dev/null`
if [ ${?} -ne 0 ]; then
	echo "Could not read \"${INPUT_FILE}\""
	exit
fi

_numchunks=`expr ${_filesize} / ${CHUNK_SIZE}`
_remainder=`expr ${_filesize} % ${CHUNK_SIZE}`
if [ ${_remainder} -ne 0 ]; then
	let "_numchunks += 1"
fi
_datafile=`mktemp`

# Create virtual region header field
if [ ${HEADER_LEN} -eq 16 ]; then
    HEADER_FIELDS="${PGP_SIGNED_VIRT_RGN} \
                   ${OFFSET_INTO_TARGET} \
                   ${CHUNK_SIZE} \
                   ${SIGSIZE}"
elif [ ${HEADER_LEN} -eq 24 ]; then
    HEADER_FIELDS="${PGP_SIGNED_VIRT_RGN} \
                   ${HEADER_LEN} \
                   ${TARGET} \
                   ${OFFSET_INTO_TARGET} \
                   ${CHUNK_SIZE} \
                   ${SIGSIZE}"
else
    echo "Invalid PGP region header size"
    exit 1
fi

perl -e 'for (@ARGV) { print pack "I", $_;}' ${HEADER_FIELDS} > "${OUTPUT_FILE}"

echo "Signing:"

# Copy chunk into a temporary file and create a signature.  Pad the signature
#  out to SIGSIZE if needed, then cat both onto the end of the update file.
for _chunks in `seq 0 $((${_numchunks}-1))`; do
	echo -n $((${_chunks}+1)) / ${_numchunks}
	dd bs=${CHUNK_SIZE} count=1 if="${INPUT_FILE}" of=${_datafile} skip=${_chunks} 2> /dev/null
	cat ${GPG_PASSPHRASE_FILE} | ${GPG_COMMAND} --passphrase-fd 0 --batch -q -b -u "${GPG_USERNAME}" "${_datafile}" 2> /dev/null > /dev/null

	_sigsize=`stat -c "%s" ${_datafile}.sig`
	if [ ${_sigsize} -gt ${SIGSIZE} ]; then
		echo; echo ERROR!  Sig larger than max size!
		exit 1
	fi
	for byte in `seq $((${_sigsize}+1)) ${SIGSIZE}`; do
		perl -e 'for (@ARGV) { print pack "C", $_;}' 0 >> ${_datafile}.sig
	done
		
	cat ${_datafile} ${_datafile}.sig >> ${OUTPUT_FILE}
	rm ${_datafile}.sig
	echo -en "\\r"
done

rm ${_datafile}

echo
