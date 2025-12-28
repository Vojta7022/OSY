#!/usr/bin/env bash

help() {
cat <<'EOF'
Script with options:
  -h  Help
  -a  List PDFs (case-insensitive) in current directory
  -b  print lines that start with an integer, without that integer
  -c  split into sentences (A–Z ... [.!?]), each on new line
  -d <prefix>  add <prefix> inside all #include "…" and #include <…>
EOF
}

list_pdfs() { find . -maxdepth 1 -type f -iname '*.pdf' -printf '%f\n' | sort; }

b_strip() { sed -n '/^[+-]\{0,1\}[0-9][0-9]*/s/^[+-]\{0,1\}[0-9][0-9]*//p'; }

c_split() {
  tr '\n' ' ' \
  | sed -E 's/([A-Z][^.!?]*[.!?])/\1\n/g' \
  | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//' \
  | sed -n '/[.!?]$/p'
}

d_prefix() {
  prefix=$1

  esc=${prefix//\\/\\\\}
  esc=${esc//&/\\&}
  esc=${esc//|/\\|}

  sed -E \
    -e "s|(#[[:space:]]*include[[:space:]]*\")([^\"]*)(\")|\1${esc}\2\3|g" \
    -e "s|(#[[:space:]]*include[[:space:]]*<)([^>]*)(>)|\1${esc}\2\3|g"
}

act=""; pref=""; need_pref=0

# Parse command-line arguments
for i in "$@"; do
  if (( need_pref )); then pref="$i"; break; fi
  case "$i" in
    -h) act=h; break ;;
    -a) act=a; break ;;
    -b) act=b; break ;;
    -c) act=c; break ;;
    -d) act=d; need_pref=1 ;;
  esac
done

case "$act" in
  h) help; exit 0 ;;
  a) list_pdfs; exit 0 ;;
  b) b_strip; exit 0 ;;
  c) c_split; exit 0 ;;
  d) [[ -n "$pref" ]] || { echo "Error: -d needs <prefix>" >&2; help >&2; exit 1; }
     d_prefix "$pref"; exit 0 ;;
  *) help >&2; exit 1 ;;
esac
