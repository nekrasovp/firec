#!/usr/bin/env zsh

emulate -L zsh
setopt errexit pipefail no_aliases

readonly project_root=${0:A:h:h}
readonly launcher=$project_root/contrib/oh-my-zsh/fireplace.zsh

fail() {
  print -u2 -r -- "omz adapter tests: FAIL: $*"
  exit 1
}

[[ -r $launcher ]] || fail "launcher is not readable: $launcher"

local source_text=$(<$launcher)
[[ $source_text == *'--shellsaver'* ]] || \
  fail 'launcher does not select canonical shellsaver mode'
[[ $source_text != *'_zshell_fireplace'* ]] || \
  fail 'launcher contains a second fireplace renderer'

local output
output=$(FIREC_BIN=$project_root/fire zsh "$launcher" --help)
[[ $output == *'--shellsaver'* ]] || fail 'launcher did not delegate --help'

set +e
FIREC_BIN=/definitely/missing/firec zsh "$launcher" --help >/dev/null 2>&1
local exit_code=$?
set -e
(( exit_code == 127 )) || fail "missing binary returned $exit_code instead of 127"

print -r -- 'omz adapter tests: OK'
