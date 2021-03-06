#!/bin/sh

set -e

[ -r ./po_functions ] || exit 1
. ./po_functions

manual_release=${manual_release:=bullseye}

if [ -z "$languages" ]; then
    # Buildlist of languages to be included on the official website
    # Based on list of languages used in official builds
    languages="$(cd ../debian; ./getfromlist langlist | tr $'\n' ' ')"
fi

if [ -z "$architectures" ]; then
    # Based on list of architectures used in official builds
    architectures="$(cd ../debian; ./getfromlist archlist | tr $'\n' ' ')"
fi

if [ -z "$destination" ]; then
    destination="/tmp/manual"
fi

if [ -z "$formats" ]; then
    formats="html pdf txt"
fi

[ -e "$destination" ] || mkdir -p "$destination"

export official_build="1"
export web_build="1"
export manual_target="for_wdo"

# Delete any old generated XML files
clear_xml

# We need to merge the XML files for English and update the POT files
export PO_USEBUILD="1"
update_templates

for lang in $languages; do
    echo "Language: $lang";

    # Update PO files and create XML files
    if [ ! -d ../$lang ] && uses_po; then
        generate_xml
    fi
done
    
make languages="$languages" architectures="$architectures" destination="$destination" formats="$formats" web=1

PRESEED="../en/appendix/preseed.xml"
if [ -f $PRESEED ] && [ -f preseed.pl ] ; then
    echo '#_preseed_V1' >$destination/example-preseed.txt
    ./preseed.pl -r $manual_release $PRESEED >>$destination/example-preseed.txt
fi

# Delete temporary PO files and generated XML files
clear_po
clear_xml
