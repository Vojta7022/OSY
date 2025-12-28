#!/usr/bin/env bash

usage() {
  echo 'This script processes paths from stdin and outputs their types.'
  echo '  -h  show this help and exit'
  echo '  -z  create output.tgz containing FILE entries'
}

zip_flag=0

# Parse command-line arguments
for arg in "$@"; do
  case $arg in
    -h) 
        usage; 
        exit 0 
        ;;
    -z) 
        zip_flag=1
        ;;
    *) 
        echo "Error: Unknown argument: $arg" >&2
        exit 2
  esac
done

error=0
files=()

# Read from stdin
while IFS= read -r line; do
  [[ $line != PATH\ * ]] && continue
  path="${line#PATH }"

  if [[ -L $path ]]; then
    target=$(readlink "$path")
    echo "LINK '$path' '$target'"
  elif [[ -f $path ]]; then
    count=$(wc -l <"$path") || { echo "Error reading $path" >&2; exit 2; }
    read -r first <"$path" || first=""
    echo "FILE '$path' $count '$first'"
    files+=("$path")
  elif [[ -d $path ]]; then
    echo "DIR '$path'"
  else
    echo "ERROR '$path'" >&2
    error=1
  fi
done

# Create archive if -z flag is set
if (( zip_flag )); then
  if ! tar czf output.tgz "${files[@]}"; then
    echo "Error creating archive" >&2
    exit 2
  fi
fi

exit $error
