Build Instructions for Windows:

git clone --recurse-submodules https://github.com/deeprey-marine/deeprey-ocharts.git

cd deeprey-ocharts

Run buildwin\win_deps.bat wx32

mkdir build

cd build

Configure the build environment ( use your own path instead of C:/Projects/): cmake -A Win32 -G "Visual Studio 17 2022" -DCMAKE_GENERATOR_PLATFORM=Win32 -DwxWidgets_LIB_DIR=C:/Projects/deeprey-ocharts/cache/wxWidgets-3.2.1/lib/vc14x_dll -DwxWidgets_ROOT_DIR=C:/Projects/deeprey-ocharts/cache/wxWidgets-3.2.1 ..

On windows, add -DDEPLOY_ON_BUILD=1 if you want plugin dll auto-copy into import location.

Build the plugin: cmake --build . --config Release --target tarball


o-charts_pi README
==================

This is a plugin for opencpn [1] providing support for encrypted charts
available from o-charts.org [2]. The plugin supports purchase, downloading
and rendering of these charts.

o-charts can handle charts which have been handled by the oesenc and oernc
plugins and thus obsoletes these plugins.

Building and installation is described in INSTALL.md

The plugin has a continous integration setup publishing artifacts
which can be used by the new opencpn plugin installer. See CI.md.

Licensing
---------

This software is copyright (c) David Register 2020-2021. It is distributed
under the terms of the Gnu Public License version 2 or, at your option, any 
later version. See COPYING.gplv2.

The oexserverd binary and libsgllnx64-2.29.02 libraries are closed source.
Re-distribution of these items is allowed.

The sources also contains dependencies distributed under various open-source
licenses including Expat, the Curl license, SGI-B, Zlib and Khronos. Refer
to the source files for details.


[1] https://www.opencpn.org <br>
[2] http://o-charts.org
