#!/bin/sh

if [ "$1" = "--help" ]; then
    echo "$0: Generate the Debian Installer Manual in several different formats"
    echo "Usage: $0 [arch] [lang] [format]"
    echo "[format] may consist of multiple formats provided they are quoted (e.g. \"html pdf\")"
    echo "Supported formats: html, pdf, txt"
    exit 0
fi

DEBVERSION=`dpkg-parsechangelog -l ../debian/changelog -S Version`

arch=${1:-i386}
language=${2:-en}
formats=${3:-html}
lang_id="$(echo $language | tr A-Z a-z | sed "s/_/-/")"

## Configuration
basedir="$(cd "$(dirname $0)"; pwd)"
manual_path="$(echo $basedir | sed "s:/build$::")"
build_path="$manual_path/build"
cd $build_path

stylesheet_dir="$build_path/stylesheets"
stylesheet_profile="$stylesheet_dir/style-profile.xsl"
if [ ! "$web_build" ]; then
    stylesheet_html="$stylesheet_dir/style-html.xsl"
else
    stylesheet_html="$stylesheet_dir/style-html-web.xsl"
fi
stylesheet_html_single="$stylesheet_dir/style-html-single.xsl"
stylesheet_dsssl="$stylesheet_dir/style-print.dsl"
stylesheet_css="$stylesheet_dir/install.css"
stylesheet_images_dir="$stylesheet_dir/png"

entities_path="$build_path/entities"
source_path="$manual_path/$language"

if [ -z "$destdir" ]; then
    destdir="build.out"
fi

if [ -z "$tempdir" ]; then
    tempdir="build.tmp"
fi

dynamic="${tempdir}/dynamic.ent"

create_profiled () {

    [ -x "`which xsltproc 2>/dev/null`" ] || return 9

    echo "Info: creating temporary profiled .xml file..."

    if [ ! "$official_build" ]; then
        unofficial_build="FIXME;unofficial-build"
    else
        unofficial_build=""
    fi

    if [ -z "$manual_release" ]; then
        manual_release="bullseye"
    fi
    if [ -z "$manual_target" ]; then
        manual_target="for_cd"
    fi

    # Now we source the profiling information for the selected architecture
    [ -f "arch-options/${arch}" ] || {
        echo "Error: unknown architecture '$arch'" >&2
        return 1
    }
    . arch-options/$arch
    os=`dpkg-architecture -a$arch -qDEB_HOST_ARCH_OS 2> /dev/null`
    . arch-options/$os

    # Now we source the profiling information for the current language
    if [ -f "lang-options/${language}" ]; then
	. lang-options/$language
    fi

    # Join all architecture options into one big variable
    condition="$fdisk;$network;$boot;$smp;$frontend;$other;$goodies;$lowmem;$unofficial_build;$status;$manual_release"
    # Add language options
    condition="$condition;$optional_paras"
    # Add build options for the manual
    condition="$condition;$unofficial_build;$status;$manual_release;$manual_target"

    # Write dynamic non-profilable entities into the file
    echo "<!-- arch- and lang-specific non-profilable entities -->" > $dynamic
    echo "<!ENTITY debversion \"$DEBVERSION\">" >> $dynamic
    echo "<!ENTITY langext \".${language}\">" >> $dynamic
    echo "<!ENTITY architecture \"${arch}\">" >> $dynamic
    echo "<!ENTITY arch-kernel \"${arch_kernel}\">" >> $dynamic
    echo "<!ENTITY arch-listname \"${arch_listname}\">" >> $dynamic
    echo "<!ENTITY arch-porturl \"${arch_porturl}\">" >> $dynamic
    echo "<!ENTITY arch-parttype \"${arch_parttype}\">" >> $dynamic
    echo "<!ENTITY kernelversion \"${kernelversion}\">" >> $dynamic
    echo "<!ENTITY kernelpackage \"${kernelpackage}\">" >> $dynamic
    echo "<!ENTITY smp-config-section \"${smp_config_section}\">" >> $dynamic
    echo "<!ENTITY smp-config-option \"${smp_config_option}\">" >> $dynamic
    echo "<!ENTITY minimum-memory-strict \"${minimum_memory_strict}&notation-megabytes;\">" >> $dynamic
    echo "<!ENTITY minimum-memory \"${minimum_memory}&notation-megabytes;\">" >> $dynamic
    echo "<!ENTITY minimum-memory-gtk \"${minimum_memory_gtk}&notation-megabytes;\">" >> $dynamic

    sed "s:##SRCPATH##:$source_path:" templates/docstruct.ent >> $dynamic

    sed "s:##LANG##:$language:g" templates/install.xml.template | \
    sed "s:##LANG_ID##:$lang_id:g" | \
        sed "s:##TEMPDIR##:$tempdir:g" | \
        sed "s:##ENTPATH##:$entities_path:g" | \
        sed "s:##SRCPATH##:$source_path:" > $tempdir/install.${language}.xml

    # Create the profiled xml file
    xsltproc \
        --xinclude \
        --stringparam profile.arch "$archspec" \
        --stringparam profile.condition "$condition" \
        --output $tempdir/install.${language}.profiled.xml \
        $stylesheet_profile \
        $tempdir/install.${language}.xml
    RET=$?; [ $RET -ne 0 ] && return $RET

    return 0
}

create_html () {

    echo "Info: creating .html files..."

    xsltproc \
        --xinclude \
        --stringparam base.dir $destdir/html/ \
        $stylesheet_html \
        $tempdir/install.${language}.profiled.xml 2>&1
    RET=$?; [ $RET -ne 0 ] && return $RET

    # Copy the custom css stylesheet to the destination directory
    cp $stylesheet_css $destdir/html/
    mkdir $destdir/html/images
    cp $stylesheet_images_dir/* $destdir/html/images

    return 0
}

create_text () {

    [ -x "`which w3m 2>/dev/null`" ] || return 9

    echo "Info: creating temporary .html file..."

    xsltproc \
        --xinclude \
        --output $tempdir/install.${language}.html \
        $stylesheet_html_single \
        $tempdir/install.${language}.profiled.xml
    RET=$?; [ $RET -ne 0 ] && return $RET

    # Replace some unprintable characters
    sed "s:–:-:g        # n-dash
         s:—:--:g       # m-dash
         s:“:\&quot;:g  # different types of quotes
         s:”:\&quot;:g
         s:„:\&quot;:g
         s:…:...:g      # ellipsis
         s:™: (tm):g    # trademark" \
        $tempdir/install.${language}.html >$tempdir/install.${language}.corr.html
    RET=$?; [ $RET -ne 0 ] && return $RET

    echo "Info: creating .txt file..."

    # Set encoding for output file
    case "$language" in
        ru)  CHARSET=KOI8-R ;;
        *)   CHARSET=UTF-8 ;;
    esac
    
    HOME=$tempdir w3m -dump $tempdir/install.${language}.corr.html \
        -o display_charset=$CHARSET \
        >$destdir/install.${language}.txt
    RET=$?; [ $RET -ne 0 ] && return $RET
    
    return 0
}

create_pdf() {

    [ -x "`which dblatex 2>/dev/null`" ] || return 9

    echo "Info: creating .pdf file..."

    mkdir -p $tempdir/dblatex
    (TMPDIR=$tempdir/dblatex dblatex -q -V -T db2latex -b xetex -p ./stylesheets/dblatex.xsl \
    -o $tempdir/install.${language}.pdf \
    $tempdir/install.${language}.profiled.xml --param=lingua=${language} )
    RET=$?; [ $RET -ne 0 ] && return $RET
    mv $tempdir/install.${language}.pdf $destdir/

    return 0
}

## MAINLINE

# Clean old builds
rm -rf $tempdir
rm -rf $destdir

[ -d "$manual_path/$language" ] || {
    echo "Error: unknown language '$language'" >&2
    exit 1
}

mkdir -p $tempdir
mkdir -p $destdir

# Create profiled XML. This is needed for all output formats.
create_profiled
RET=$?; [ $RET -ne 0 ] && exit 1

BUILD_OK=""
BUILD_FAIL=""
for format in $formats ; do
    case "$language" in
        __)
            if [ "$format" = "pdf" ] ; then
                echo "Warning: pdf format is currently not supported for __."
                BUILD_SKIP="$BUILD_SKIP $format"
                continue
            fi
            ;;
    esac

    case $format in
        html)  create_html;;
        pdf)   create_pdf;;
        txt)   create_text;;
        *)
            echo "Error: format $format unknown or not yet supported!" >&2
            exit 1
            ;;
    esac

    RET=$?
    case $RET in
        0)
            BUILD_OK="$BUILD_OK $format"
            ;;
        9)
            BUILD_FAIL="$BUILD_FAIL $format"
            echo "Error: build of $format failed because of missing build dependencies" >&2
            if [ "$format" = "pdf" ] ; then
                echo "Error: (make sure you have ghostscript and dblatex installed for PDF builds)" >&2
            fi
            ;;
        *)
            BUILD_FAIL="$BUILD_FAIL $format"
            echo "Error: build of $format failed with error code $RET" >&2
            ;;
    esac
done

# Clean up
rm -r $tempdir

# Evaluate the overall results
[ -n "$BUILD_SKIP" ] && echo "Info: The following formats were skipped:$BUILD_SKIP"
[ -z "$BUILD_FAIL" ] && exit 0            # Build successful for all formats
echo "Warning: The following formats failed to build:$BUILD_FAIL"
[ -n "$BUILD_OK" ] && exit 2              # Build failed for some formats
exit 1                                    # Build failed for all formats
