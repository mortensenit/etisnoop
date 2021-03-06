ETISnoop analyser
=================

The ETISnoop analyser decodes and prints out a RAW ETI file in a
form that makes analysis easier.

It can show information about the signalling, details about the FIGs,
and extract a DAB+ subchannel into a file.

Build
-----

etisnoop is using autotools. If you do not have a release containing a ./configure script,
run ./bootstrap.sh

Install prerequisites: A C++ compiler with complete C++11 support.

Then do

    ./configure
    make
    sudo make install

About
-----

This is a contribution by CSP.it, is now developed by opendigitalradio,
and is published under the terms of the GNU GPL v3 or later.
See LICENCE for more information.


Faadalyse
=========

faadalyse can extract the audio from .dabp files created with ODR-AudioEnc,
and is designed to do more in-depth analysis of the AAC encoding using a modified
libfaad.

To install:

download and extract faad2-2.7 to a folder of the same name, and configure and compile it.
You can patch that faad library to display additional information you need.
Do not run make install.

Then run

    make -f Makefile.faadalyse
    ./faadalyse
