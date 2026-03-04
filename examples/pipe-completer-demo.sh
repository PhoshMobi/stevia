#!/bin/bash

# demo script for phosh-osk-stub pipe completer
# more infos: https://phosh.mobi/posts/osk-completion/
#
# gsettings:
#
# set the script as completer:
# gsettings set mobi.phosh.osk.Completers.Pipe command '/path/to/phosh-osk-demo.sh'
#
# set completer 'pipe':
# gsettings set mobi.phosh.osk.Completers default pipe
#
# optional - set mode when to enable completion
# gsettings set mobi.phosh.osk completion-mode "['manual','hint']"
#
# For other auto completion related options see `man 1 phosh-osk-stub`

MYPID=$BASHPID
logger -t$0 "start PID='$MYPID'"

# read STDIN
read STRING
logger -t$0 "STRING='$STRING'"

# completion-demo: return string with first letter uppercase
echo "$STRING" | sed 's/.*/\u&/'
# completion-demo: return string uppercase
echo "$STRING" | tr 'a-z' 'A-Z'

logger -t$0 "end: PID='$MYPID'"
