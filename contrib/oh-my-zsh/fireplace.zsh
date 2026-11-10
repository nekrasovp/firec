#!/usr/bin/env zsh

# Thin launcher for the canonical firec renderer. The C executable owns the
# terminal lifecycle so this adapter cannot diverge into a second animation.

emulate -L zsh
setopt localoptions no_aliases

local firec_bin=''
local project_bin=${0:A:h:h:h}/fire
if [[ -n ${FIREC_BIN-} ]]; then
  firec_bin=$FIREC_BIN
elif [[ -x $project_bin ]]; then
  firec_bin=$project_bin
elif (( ${+commands[firec]} )); then
  firec_bin=${commands[firec]}
elif (( ${+commands[fire]} )); then
  firec_bin=${commands[fire]}
fi

if [[ -z $firec_bin || ! -x $firec_bin ]]; then
  print -u2 -r -- \
    'firec shellsaver: executable not found; install fire/firec or set FIREC_BIN'
  exit 127
fi

exec "$firec_bin" --shellsaver "$@"
