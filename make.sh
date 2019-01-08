#!/bin/sh
if [ -f "wav_encode.cbp" ]; then
$CODEBLOCK  /na /nd --no-splash-screen --build wav_encode.cbp --target=Release
# ssh-keygen -t rsa -C "1043623557@qq.com"
# pissh -T git@github.com
fi

