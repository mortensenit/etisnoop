/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://www.csp.it/)
    Copyright (C) 2017 Matthias P. Braendli (http://www.opendigitalradio.org)
    Copyright (C) 2015 Data Path

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    etisnoop.cpp
          Parse ETI NI G.703 file

    Authors:
         Sergio Sagliocco <sergio.sagliocco@csp.it>
         Matthias P. Braendli <matthias@mpb.li>
                   / |  |-  ')|)  |-|_ _ (|,_   .|  _  ,_ \
         Data Path \(|(||_(|/_| (||_||(a)_||||(|||.(_()|||/

*/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <cinttypes>
#include <string>
#include <regex>
#include <sstream>
#include <time.h>
#include <signal.h>

#include "etianalyse.hpp"
#include "dabplussnoop.hpp"
#include "utils.hpp"
#include "etiinput.hpp"
#include "figs.hpp"
#include "watermarkdecoder.hpp"
#include "repetitionrate.hpp"
#include "figalyser.hpp"

using namespace std;

static void handle_signal(int)
{
    quit.store(true);
}

#define no_argument 0
#define required_argument 1
#define optional_argument 2
const struct option longopts[] = {
    {"analyse-figs",       no_argument,        0, 'f'},
    {"decode-stream",      required_argument,  0, 'd'},
    {"filter-fig",         required_argument,  0, 'F'},
    {"help",               no_argument,        0, 'h'},
    {"ignore-error",       no_argument,        0, 'e'},
    {"input",              required_argument,  0, 'i'},
    {"input-file",         no_argument,        0, 'i'},
    {"num-frames",         required_argument,  0, 'n'},
    {"statistics",         required_argument,  0, 's'},
    {"verbose",            no_argument,        0, 'v'},
};

void usage(void)
{
    fprintf(stderr,
            "Opendigitalradio ETISnoop analyser %s compiled at %s, %s\n\n"
            "The ETISnoop analyser decodes and prints out a RAW ETI file in a\n"
            "form that makes analysis easier.\n"
            "\n"
            "  http://www.opendigitalradio.org\n"
            "\n"
            "Usage: etisnoop [options] [-i filename]\n"
            "\n"
            "   -v      increase verbosity (can be given more than once)\n"
            "   -d N    decode subchannel N into .msc file and if DAB+, decode to .wav file\n"
            "   -s <filename.yaml>\n"
            "           statistics mode: decode all subchannels and measure audio level, write statistics to file\n"
            "   -n N    stop analysing after N ETI frames\n"
            "   -f      analyse FIC carousel\n"
            "   -r      analyse FIG rates in FIGs per second\n"
            "   -R      analyse FIG rates in frames per FIG\n"
            "   -w      decode CRC-DABMUX and ODR-DabMux watermark.\n"
            "   -F <type>/<ext>\n"
            "           add FIG type/ext to list of FIGs to display.\n"
            "           if the option is not given, all FIGs are displayed.\n"
            "\n",
#if defined(GITVERSION)
            GITVERSION,
#else
            VERSION,
#endif
            __DATE__, __TIME__);
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    memset( &sa, 0, sizeof(sa) );
    sa.sa_handler = handle_signal;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    int index;
    int ch = 0;
    string file_name("-");

    eti_analyse_config_t config;

    while(ch != -1) {
        ch = getopt_long(argc, argv, "d:efF:hi:n:rRs:vw", longopts, &index);
        switch (ch) {
            case 'd':
                {
                int subchix = atoi(optarg);
                config.streams_to_decode.emplace(std::piecewise_construct,
                        std::make_tuple(subchix),
                        std::make_tuple(true)); // dump to file
                }
                break;
            case 'e':
                config.ignore_error = true;
                break;
            case 'f':
                config.analyse_fic_carousel = true;
                break;
            case 'F':
                {
                const string type_ext(optarg);
                const std::regex regex("^([0-9]+)/([0-9]+)$");
                std::smatch match;
                bool is_match = std::regex_search(type_ext, match, regex);
                if (not is_match) {
                    fprintf(stderr, "Incorrect -F format\n");
                    return 1;
                }

                const string type_str = match[1];
                const int type = std::atoi(type_str.c_str());
                const string extension_str = match[2];
                const int extension = std::atoi(extension_str.c_str());

                fprintf(stderr, "Adding FIG %d/%d to filter\n", type, extension);
                config.figs_to_display.emplace_back(type, extension);
                }
                break;
            case 'i':
                file_name = optarg;
                break;
            case 'n':
                config.num_frames_to_decode = std::atoi(optarg);
                break;
            case 'r':
                config.analyse_fig_rates = true;
                config.analyse_fig_rates_per_second = true;
                break;
            case 'R':
                config.analyse_fig_rates = true;
                config.analyse_fig_rates_per_second = false;
                break;
            case 's':
                config.statistics = true;
                config.statistics_filename = optarg;
                break;
            case 'v':
                set_verbosity(get_verbosity() + 1);
                break;
            case 'w':
                config.decode_watermark = true;
                break;
            case -1:
                break;
            default:
            case 'h':
                usage();
                return 1;
                break;
        }
    }

    FILE* etifd;

    if (file_name == "-") {
        printf("Analysing stdin\n");
        etifd = stdin;
    }
    else {
        etifd = fopen(file_name.c_str(), "r");
        if (etifd == NULL) {
            perror("File open failed");
            return 1;
        }
    }
    config.etifd = etifd;

    ETI_Analyser eti_analyser(config);
    eti_analyser.eti_analyse();
    fclose(etifd);
}


