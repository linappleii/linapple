#   common.bash - common code used by other scripts in this directory
#
#   This is expected to be `source`d.

#####################################################################
#   Common setup

set -Eeuo pipefail
trap 'ec=$?; echo 1>&2 "INTERNAL ERROR: ec=$ec line=$LINENO cmd=$BASH_COMMAND";
    exit $ec;' ERR

export PROJDIR=$(command cd $(dirname "$0")/.. && pwd -P)

#####################################################################
#   Utility functions

error() { echo -e 1>&2 "● ERROR: $(basename "$0"):" "$@"; }
die()   { local ec=$1; shift; error "$@"; exit $ec; }

#####################################################################
#   Linux distribution/release handling

#   Set variables defined in /etc/os-release, in particular $ID and
#   $VERSION_ID. If /etc/os-release doesn't exist these may be synthesised
#   through other means, otherwise this exits with an error.
os_release() {
    [[ -r /etc/os-release ]] \
        || die 1 "/etc/os-release not found; cannot autodetect distro"
    source /etc/os-release
    [[ -n ${ID:-} && -n ${VERSION_ID:-} ]] \
        || die 5 '$ID and/or $VERSION_ID not set in /etc/os-release'
}

set_distro() {
    declare -g force_distro     # set by a -D option, if desired
    declare -g ID VERSION_ID

    if [[ -z $force_distro ]]; then
        os_release
    else
        if [[ $force_distro =~ ^([^:]+):([^:]+)$ ]]; then
            ID=${BASH_REMATCH[1]}
            VERSION_ID=${BASH_REMATCH[2]}
        else
            die 2 "DISTRO must be 'name:ver'"
        fi
    fi
}

#####################################################################
#   Frontend handling

#   We have ID and VERSION_ID but FRONTEND has not been specified. Choose
#   an appropriate default for FRONTEND or exit with an error indicating we
#   have no default.
default_frontend() {
    local rel=$VERSION_ID
    case $ID in
        debian)     [[ $rel -ge 13 ]]       && echo sdl3 || echo sdl2;;
        ubuntu)     [[ $rel  >  24.04 ]]    && echo sdl3 || echo sdl2;;
        #   SDL3 became available in Fedora 40, got stable in 41, and
        #   in 42 SDL2 was removed.
        fedora)     [[ $rel -ge 42 ]]       && echo sdl3 || echo sdl2;;
        arch)                                  echo sdl3             ;;
        *)          die 4 "No default frontends known for distro $ID";;
    esac
}

check_frontend() {
    declare -g FRONTEND

    [[ -n $FRONTEND ]] || FRONTEND=$(default_frontend)

    #   Ensure $FRONTEND is known value. There are unfortunately no checks
    #   to see that the rest of the code supports all of these, or that
    #   usage() correctly reflects these.
    case $FRONTEND in
        sdl[123])   return 0;;
        *)          die 2 "Unknown frontend: '$FRONTEND'. See --help.";;
    esac
}

usage_frontends() {
    cat <<_____
Known frontends:
  sdl3  Simple DirectMedia Layer, version 3
  sdl2  Simple DirectMedia Layer, version 2
  sdl1  Simple DirectMedia Layer, version 1
_____
}
