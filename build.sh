#!/bin/sh

dir=$(cd $(dirname "$0") && pwd -P )
cd ${dir} && make clean
cd ${dir}/linuxshell && sh ./linux.sh \
&& cd ${dir} && sh ./autogen-coin-man.sh "coin-debug"\
&& chmod +x ./share/genbuild.sh && make -j4
