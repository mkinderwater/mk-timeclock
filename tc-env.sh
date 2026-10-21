# Shared, literal parser for /etc/timeclock/timeclock.env (sourced by the run scripts).
# Values are taken verbatim: no $ expansion, no backticks, no word splitting, so
# passwords may contain $, spaces, quotes or # safely. One optional matching pair of
# surrounding single or double quotes is removed. Later lines override earlier ones.
# Usage: tc_load_env FILE   (exports every KEY=VALUE line)
#        tc_val FILE KEY    (prints one value without exporting)
tc_parse_line() {
  local line=$1
  line=${line%$'\r'}
  [[ $line =~ ^[[:space:]]*([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]] || return 1
  TC_KEY=${BASH_REMATCH[1]}
  TC_VALUE=${BASH_REMATCH[2]}
  if [[ ${#TC_VALUE} -ge 2 ]]; then
    local first=${TC_VALUE:0:1} last=${TC_VALUE: -1}
    if [[ ( $first == '"' && $last == '"' ) || ( $first == "'" && $last == "'" ) ]]; then
      TC_VALUE=${TC_VALUE:1:${#TC_VALUE}-2}
    fi
  fi
  return 0
}
tc_load_env() {
  local file=$1 line
  [[ -r $file ]] || { echo "Cannot read $file" >&2; return 1; }
  while IFS= read -r line || [[ -n $line ]]; do
    tc_parse_line "$line" || continue
    export "$TC_KEY=$TC_VALUE"
  done < "$file"
}
tc_val() {
  local file=$1 want=$2 line out=
  while IFS= read -r line || [[ -n $line ]]; do
    tc_parse_line "$line" || continue
    [[ $TC_KEY == "$want" ]] && out=$TC_VALUE
  done < "$file"
  printf '%s' "$out"
}
