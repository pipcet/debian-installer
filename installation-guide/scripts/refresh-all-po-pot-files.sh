#!/bin/sh
# Refresh all po|pot files from English

cd ..
./scripts/merge_xml en
./scripts/update_pot
./scripts/update_po ca
./scripts/update_po cs
./scripts/update_po da
./scripts/update_po de
./scripts/update_po el
./scripts/update_po es
./scripts/update_po fi
./scripts/update_po fr
./scripts/update_po hu
./scripts/update_po it
./scripts/update_po ja
./scripts/update_po kab
./scripts/update_po ko
./scripts/update_po nb
./scripts/update_po nl
./scripts/update_po nn
./scripts/update_po pt
./scripts/update_po ro
./scripts/update_po ru
./scripts/update_po sv
./scripts/update_po tl
./scripts/update_po uk
./scripts/update_po vi
./scripts/update_po zh_CN
./scripts/update_po zh_TW
